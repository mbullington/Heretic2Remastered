#include "ref_soft_local.h"

#if defined(__GNUC__) && defined(__i386__)
#include <mmintrin.h>
#define SW_HAVE_MMX 1
#endif

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint16_t *sw_color;
qboolean sw_blend_pending;
static bool sw_color_owns;
static byte *sw_present_lut;
static uint16_t sw_blend_rgmap[2048];
#define SW_BLEND_LUT_SLOTS 16
typedef struct sw_blend_lut_slot_s {
	uint32_t key;
	unsigned stamp;
	uint16_t *values;
} sw_blend_lut_slot_t;
static sw_blend_lut_slot_t sw_blend_lut[SW_BLEND_LUT_SLOTS];
static unsigned sw_blend_lut_stamp;
uint32_t *sw_depth;
int sw_width;
int sw_height;
h2dos_soft_stats_t sw_stats;
qboolean SW_SavePPM(const char *path);

static SDL_Window *sw_window;
static SDL_Surface *sw_surface;

uint16_t SW_RGB565(const unsigned r, const unsigned g, const unsigned b)
{
	// RGB565 in-memory order: the low 16 bits of the little-endian u16 are the
	// VESA LFB pixel. Presenting to the RGB565 fullscreen surface collapses to
	// a straight 2-byte/pixel copy.
	return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

qboolean SW_FrameBufferInit(void *window)
{
	if (window == NULL)
		return false;

	sw_window = window;
	// B2: request the VESA 320x240 RGB565 mode so the final presentation is a
	// 2-byte/pixel LFB copy instead of 4-byte. The internal framebuffer stays
	// XRGB8888 for now (conversion happens in SDL_ConvertPixels at present).
	{
		// Mode probe: resolution overridable via harness env (avoids the
		// engine +set token path); defaults stay 320x240.
		int req_w = 320;
		int req_h = 240;
		{
			const char *ew = getenv("H2DOS_SW_WIDTH");
			const char *eh = getenv("H2DOS_SW_HEIGHT");
			if (ew != NULL && eh != NULL)
			{
				const int tw = (int)strtol(ew, NULL, 10);
				const int th = (int)strtol(eh, NULL, 10);
				// The rasterer's fixed world coverage mask is currently sized for
				// at most 640x480. Do not advertise modes that can overrun it.
				if (tw >= 160 && tw <= 640 && th >= 100 && th <= 480)
				{
					req_w = tw;
					req_h = th;
				}
			}
		}
		SDL_DisplayMode want;
		SDL_zero(want);
		// Prefer direct color at the requested resolution; indexed is the fallback.
		want.format = SDL_PIXELFORMAT_RGB565;
		want.w = req_w;
		want.h = req_h;
		sw_ri.Con_Printf(0, "ref_soft: sw mode request %dx%d\n", want.w, want.h);
		// Probe ladder: same resolution in the alternate pixel format first
		// (video-mode changes are resolution-first), then the classic 320x240.
		if (!SDL_SetWindowFullscreenMode(sw_window, &want))
		{
			want.format = want.format == SDL_PIXELFORMAT_INDEX8 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_INDEX8;
			if (!SDL_SetWindowFullscreenMode(sw_window, &want))
			{
				want.format = SDL_PIXELFORMAT_RGB565;
				want.w = 320;
				want.h = 240;
				if (!SDL_SetWindowFullscreenMode(sw_window, &want))
					want.format = 0;
			}
		}
		if (want.format != 0)
			(void)SDL_SetWindowFullscreen(sw_window, true);
	}
	sw_surface = SDL_GetWindowSurface(sw_window);
	if (sw_surface == NULL)
	{
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: SDL_GetWindowSurface failed: %s\n", SDL_GetError());
		return false;
	}

	sw_width = sw_surface->w;
	sw_height = sw_surface->h;
	if (sw_width <= 0 || sw_height <= 0 || (size_t)sw_width > SIZE_MAX / (size_t)sw_height)
		return false;
	if (sw_width > 640 || sw_height > 480)
	{
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: mode %dx%d exceeds fixed raster masks (max 640x480)\n",
			sw_width, sw_height);
		return false;
	}

	const size_t pixels = (size_t)sw_width * (size_t)sw_height;
	if (sw_surface->format == SDL_PIXELFORMAT_INDEX8)
	{
		// Palettized present (320x200 sibling mode): fixed 3-3-2 palette and a
		// 64KB 565->index LUT; conversion happens per row at present.
		sw_present_lut = malloc(65536);
		if (sw_present_lut == NULL)
		{
			sw_ri.Con_Printf(PRINT_ALL, "ref_soft: could not allocate indexed present LUT\n");
			return false;
		}
		for (int i = 0; i < 65536; ++i)
			sw_present_lut[i] = (byte)((((i >> 8) & 0xE0) | ((i >> 6) & 0x1C) | ((i >> 3) & 0x03)));
		SDL_Palette *pal = SDL_CreatePalette(256);
		if (pal != NULL)
		{
			SDL_Color colors[256];
			for (int i = 0; i < 256; ++i)
			{
				colors[i].r = (Uint8)(((i >> 5) & 7) * 255 / 7);
				colors[i].g = (Uint8)(((i >> 2) & 7) * 255 / 7);
				colors[i].b = (Uint8)((i & 3) * 85);
				colors[i].a = 255;
			}
			SDL_SetPaletteColors(pal, colors, 0, 256);
			SDL_SetSurfacePalette(sw_surface, pal);
			SDL_DestroyPalette(pal);
			sw_ri.Con_Printf(PRINT_ALL, "ref_soft: indexed present active (332 palette)\n");
		}
		else
		{
			free(sw_present_lut);
			sw_present_lut = NULL;
			sw_ri.Con_Printf(PRINT_ALL, "ref_soft: could not create indexed palette\n");
			return false;
		}
	}
	// Fast path: with a shader-native RGB565 surface and tight pitch, render
	// straight into the surface bytes; SDL_UpdateWindowSurface does the only
	// copy.
	if (sw_surface->format == SDL_PIXELFORMAT_RGB565 &&
		sw_surface->pitch == sw_width * (int)sizeof(*sw_color))
	{
		sw_color = (uint16_t *)sw_surface->pixels;
		sw_color_owns = false;
	}
	else
	{
		sw_color = calloc(pixels, sizeof(*sw_color));
		sw_color_owns = true;
	}
	sw_depth = malloc(pixels * sizeof(*sw_depth));
	if (sw_color == NULL || sw_depth == NULL)
	{
		SW_FrameBufferShutdown();
		return false;
	}
	SW_Clear(SW_RGB565(0, 0, 0));
	return true;
}

void SW_FrameBufferShutdown(void)
{
	for (int i = 0; i < SW_BLEND_LUT_SLOTS; ++i)
	{
		free(sw_blend_lut[i].values);
		sw_blend_lut[i].values = NULL;
		sw_blend_lut[i].key = 0;
		sw_blend_lut[i].stamp = 0;
	}
	sw_blend_lut_stamp = 0;
	free(sw_present_lut);
	sw_present_lut = NULL;
	free(sw_depth);
	if (sw_color_owns)
		free(sw_color);
	sw_color_owns = false;
	sw_depth = NULL;
	sw_color = NULL;
	sw_surface = NULL;
	sw_window = NULL;
	sw_width = 0;
	sw_height = 0;
}

static uint16_t sw_clear_color;
int sw_dbg_clear_cyan;
int sw_dbg_zcollide;
int sw_dbg_zviz;
int sw_dbg_zstrict;
int sw_dbg_skydiag;
int sw_dbg_nospan;
int sw_dbg_tag_x = -1;
int sw_dbg_tag_src = -1;
int sw_dbg_tag_y = -1;
int sw_dbg_tag_face = -1;

static void sw_debug_env_init(void)
{
	static int done;
	if (done)
		return;
	done = 1;
	sw_dbg_clear_cyan = getenv("H2DOS_SW_CLEAR_CYAN") != NULL;
	sw_dbg_zcollide = getenv("H2DOS_SW_ZCOLLIDE") != NULL;
	sw_dbg_zviz = getenv("H2DOS_SW_ZVIZ") != NULL;
	sw_dbg_zstrict = getenv("H2DOS_SW_ZSTRICT") != NULL;
	sw_dbg_skydiag = getenv("H2DOS_SW_SKYDIAG") != NULL;
	sw_dbg_nospan = getenv("H2DOS_SW_NOSPAN") != NULL; // diagnostic: force naive rasters
	{ // diagnostic pixel attribution: H2DOS_SW_TAGXY=x,y / face: H2DOS_SW_TAGFACE=n
		const char *tag = getenv("H2DOS_SW_TAGXY");
		if (tag != NULL)
		{
			sw_dbg_tag_x = (int)atof(tag);
			const char *comma = strchr(tag, ',');
			if (comma != NULL)
				sw_dbg_tag_y = (int)atof(comma + 1);
		}
		const char *tagface = getenv("H2DOS_SW_TAGFACE");
		if (tagface != NULL)
			sw_dbg_tag_face = (int)atof(tagface);
	}
}

void SW_TagProbe(const char *where)
{
	if (sw_dbg_tag_x < 0 || sw_dbg_tag_y < 0 || !sw_color || !sw_depth)
		return;
	sw_debug_env_init();
	const size_t index = (size_t)sw_dbg_tag_y * (size_t)sw_width + (size_t)sw_dbg_tag_x;
	sw_ri.Con_Printf(PRINT_ALL, "tagxy probe %s bf=%u zb=%u pix=%04x\n", where,
		sw_stats.bench_frames, sw_depth[index], sw_color[index]);
}

void SW_Clear(const uint16_t color)
{
	const uclock_t start = uclock();
	const size_t pixels = (size_t)sw_width * (size_t)sw_height;
	sw_debug_env_init();
	sw_clear_color = color;
	SW_TagProbe("clear");
	if (sw_dbg_clear_cyan)
	{
		// Debug: clear BOTH targets every frame to a conspicuous color so
		// coverage holes/raster cracks appear as stable cyan instead of
		// stale previous-frame pixels.
		size_t i;
		for (i = 0; i < pixels; ++i)
			sw_color[i] = 0x07FFu;
	}
	// Color fill is deferred: SW_RasterFrame re-fills only frames without a
	// worldmodel. Worldmodel frames are fully repainted by world/sky spans.
	// Depth stores inverse-z (larger = closer), so the far plane is zero.
	if (!sw_scan2_active())
		memset(sw_depth, 0, pixels * sizeof(*sw_depth));
	else
	{
		// Checkerboard stripe-interleave (r_swscan2): only the current-phase
		// 2-px column pairs get this frame's depth; last frame's OTHER columns
		// keep their keys (and pixels) so depth stays self-consistent per pair.
		for (int yy = 0; yy < sw_height; ++yy)
		{
			uint32_t *zrow = sw_depth + (size_t)yy * (size_t)sw_width;
			for (int xx = sw_scan_phase * 2; xx < sw_width; xx += 4)
			{
				zrow[xx] = 0;
				if (xx + 1 < sw_width)
					zrow[xx + 1] = 0;
			}
		}
	}
	sw_stats.bench_us_clear += (unsigned long long)(uclock() - start);
}

void SW_ClearColorFill(void)
{
	const size_t pixels = (size_t)sw_width * (size_t)sw_height;
	for (size_t i = 0; i < pixels; ++i)
		sw_color[i] = sw_clear_color;
}

void SW_PutPixel(const int x, const int y, const float depth, const uint16_t color, const byte alpha,
	const qboolean depth_write, const qboolean additive)
{
	// depth is inverse-z (larger = closer); FLT_MAX means "always nearest".
	if ((unsigned)x >= (unsigned)sw_width || (unsigned)y >= (unsigned)sw_height || depth <= 0.0f)
		return;

	const size_t index = (size_t)y * (size_t)sw_width + (size_t)x;
	// All stored depths are 31-bit linear fixed-point inverse-z keys (see
	// SW_DEPTH_SCALE in ref_soft_local.h); larger = closer, 0 = far/sky.
	sw_debug_env_init();
	if (sw_blend_pending)
		++sw_stats.bench_post_blend_writes;
	uint32_t depth_key;
	if (depth > 1e30f)
		depth_key = 0xFFFFFFFFu;
	else
		depth_key = (uint32_t)(depth * SW_DEPTH_SCALE);
	if (sw_dbg_tag_x == x && sw_dbg_tag_y == y)
		sw_ri.Con_Printf(PRINT_ALL, "tagxy putpixel bf=%u src=%d key=%u zb=%u color=%04x a=%d dw=%d add=%d\n",
			sw_stats.bench_frames, sw_dbg_tag_src, depth_key, sw_depth[index], color, (int)alpha,
			(int)depth_write, (int)additive);
	if (sw_dbg_zcollide && sw_depth[index] != 0)
	{
		const int64_t delta = (int64_t)depth_key - (int64_t)sw_depth[index];
		if (delta == 0)
			++sw_stats.bench_zcollide_eq;
		else if (delta >= -4 && delta <= 4)
			++sw_stats.bench_zcollide_near;
		if (sw_dbg_zviz && delta >= -4 && delta <= 4)
		{
			sw_color[index] = 0xFFE0u; // bright yellow collision marker
			return;
		}
	}
	if (sw_dbg_zstrict ? (depth_key <= sw_depth[index]) : (depth_key < sw_depth[index]))
		return;

	if (alpha == 255 && !additive)
	{
		sw_color[index] = color;
		sw_stats.bench_mag_putpixel += (color == 0xF81Fu);
	}
	else if (alpha != 0)
	{
		const uint16_t destination = sw_color[index];
		const int source_weight = alpha;
		const int destination_weight = additive ? 255 : 255 - alpha;
		const unsigned sr = (color >> 8) & 0xF8u;
		const unsigned sg = (color >> 3) & 0xFCu;
		const unsigned sb = (color << 3) & 0xF8u;
		const unsigned dr = (destination >> 8) & 0xF8u;
		const unsigned dg = (destination >> 3) & 0xFCu;
		const unsigned db = (destination << 3) & 0xF8u;
		const unsigned r = min(255u, (sr * (unsigned)source_weight + dr * (unsigned)destination_weight) / 255u);
		const unsigned g = min(255u, (sg * (unsigned)source_weight + dg * (unsigned)destination_weight) / 255u);
		const unsigned b = min(255u, (sb * (unsigned)source_weight + db * (unsigned)destination_weight) / 255u);
		const uint16_t out = SW_RGB565(r, g, b);
		sw_color[index] = out;
		sw_stats.bench_mag_putpixel += (out == 0xF81Fu);
	}

	if (depth_write)
	{
		sw_depth[index] = depth_key;
		++sw_stats.depth_writes;
	}
}

#ifdef SW_HAVE_MMX
// B1/ideas MMX 4-wide exact blend (P55C ISA: PMULHUW is PII+, so the exact
// (n*0x8081)>>23 = PSRLW(PMULHW(n,magic) + (n<0?magic:0) + (magic<0?n:0), 7);
// magic<0-signed always => add n; sweep-verified offline per (d,s,alpha)).
// n = s*aw + d*dw <= 63,240 fits u16; quotient <= 248 needs no clamp. mag
// accumulates PCMPEQW-vs-0xF81F lanes. EMMS once per call.
__attribute__((target("mmx")))
static void sw_blend_rows_mmx(uint16_t *const fb, const size_t pitch, const int x, const int y,
	const int start_y, const int end_y, const int start_x, const int end_x, const unsigned sr_w, const unsigned sg_w,
	const unsigned sb_w, const unsigned dw, unsigned *mag)
{
	const __m64 vdw = _mm_set1_pi16((short)dw);
	const __m64 vsr = _mm_set1_pi16((short)sr_w);
	const __m64 vsg = _mm_set1_pi16((short)sg_w);
	const __m64 vsb = _mm_set1_pi16((short)sb_w);
	const __m64 vmagic = _mm_set1_pi16((short)0x8081u);
	const __m64 vmask8 = _mm_set1_pi16((short)0xF8u);
	const __m64 vmag = _mm_set1_pi16((short)0xF81Fu);
	__m64 vacc = _mm_setzero_si64();
	for (int row = start_y; row < end_y; ++row)
	{
		uint16_t *const dst = fb + (size_t)(y + row) * pitch + x;
		for (int column = start_x; column + 4 <= end_x; column += 4)
		{
			const __m64 d = *(const __m64 *)(dst + column);
			__m64 dr = _mm_and_si64(_mm_srli_pi16(d, 8), vmask8);
			__m64 nr = _mm_add_pi16(_mm_mullo_pi16(dr, vdw), vsr);
			dr = _mm_add_pi16(_mm_mulhi_pi16(nr, vmagic), nr);
			dr = _mm_add_pi16(dr, _mm_and_si64(_mm_srai_pi16(nr, 15), vmagic));
			dr = _mm_slli_pi16(_mm_srli_pi16(dr, 7), 8);
			__m64 dg = _mm_and_si64(_mm_srli_pi16(d, 3), _mm_set1_pi16((short)0xFCu));
			__m64 ng = _mm_add_pi16(_mm_mullo_pi16(dg, vdw), vsg);
			dg = _mm_add_pi16(_mm_mulhi_pi16(ng, vmagic), ng);
			dg = _mm_add_pi16(dg, _mm_and_si64(_mm_srai_pi16(ng, 15), vmagic));
			dg = _mm_slli_pi16(_mm_srli_pi16(dg, 7), 5);
			__m64 db = _mm_and_si64(_mm_slli_pi16(d, 3), vmask8);
			__m64 nb = _mm_add_pi16(_mm_mullo_pi16(db, vdw), vsb);
			db = _mm_add_pi16(_mm_mulhi_pi16(nb, vmagic), nb);
			db = _mm_add_pi16(db, _mm_and_si64(_mm_srai_pi16(nb, 15), vmagic));
			db = _mm_srli_pi16(db, 7 + 3);
			const __m64 out = _mm_or_si64(_mm_or_si64(dr, dg), db);
			*(__m64 *)(dst + column) = out;
			vacc = _mm_sub_pi16(vacc, _mm_cmpeq_pi16(out, vmag));
		}
	}
	_mm_empty();
	const int m0 = _mm_cvtsi64_si32(vacc);
	const int m1 = _mm_cvtsi64_si32(_mm_srli_si64(vacc, 32));
	*mag += (unsigned)(-((short)m0 + (short)(m0 >> 16) + (short)m1 + (short)(m1 >> 16)));
}
#endif

static const uint16_t *sw_blend_lut_get(const uint32_t key, const uint16_t bmap[32])
{
	const unsigned stamp = ++sw_blend_lut_stamp;
	int victim = 0;
	for (int i = 0; i < SW_BLEND_LUT_SLOTS; ++i)
	{
		if (sw_blend_lut[i].values != NULL && sw_blend_lut[i].key == key)
		{
			sw_blend_lut[i].stamp = stamp;
			++sw_stats.bench_blend_lut_hits;
			return sw_blend_lut[i].values;
		}
		if (sw_blend_lut[i].values == NULL ||
			(sw_blend_lut[victim].values != NULL && sw_blend_lut[i].stamp < sw_blend_lut[victim].stamp))
			victim = i;
	}
	++sw_stats.bench_blend_lut_misses;
	if (sw_blend_lut[victim].values == NULL)
	{
		sw_blend_lut[victim].values = malloc(65536u * sizeof(*sw_blend_lut[victim].values));
		if (sw_blend_lut[victim].values == NULL)
			return NULL;
	}
	for (unsigned destination = 0; destination < 65536u; ++destination)
		sw_blend_lut[victim].values[destination] = (uint16_t)(
			sw_blend_rgmap[destination >> 5] | bmap[destination & 31u]);
	sw_blend_lut[victim].key = key;
	sw_blend_lut[victim].stamp = stamp;
	++sw_stats.bench_blend_lut_builds;
	return sw_blend_lut[victim].values;
}

void SW_BlendRect(const int x, const int y, const int w, const int h, const uint16_t color, const byte alpha)
{
	if (w <= 0 || h <= 0)
		return;
	sw_debug_env_init();
	const int start_x = (int)max(0ll, -(long long)x);
	const int start_y = (int)max(0ll, -(long long)y);
	const int end_x = (int)min((long long)w, (long long)sw_width - x);
	const int end_y = (int)min((long long)h, (long long)sw_height - y);
	// Diagnostics (TAGXY prints, ZCOLLIDE counters, ZVIZ markers, ZSTRICT
	// compare mode) keep the general per-pixel path so their behavior is
	// byte-identical to before; production runs take the specialized loops.
	if (sw_dbg_tag_x >= 0 || sw_dbg_zcollide || sw_dbg_zviz || sw_dbg_zstrict)
	{
		for (int row = start_y; row < end_y; ++row)
			for (int column = start_x; column < end_x; ++column)
				SW_PutPixel(x + column, y + row, FLT_MAX, color, alpha, false, false);
		return;
	}
	// In the specialized loops: FLT_MAX depth keys always pass the z
	// comparison (0xFFFFFFFF is never below a stored 31-bit key), no depth is
	// written (depth_write=false), additive=false.
	if (alpha == 0)
		return;
	if (alpha == 255)
	{
		for (int row = start_y; row < end_y; ++row)
		{
			uint16_t *const dst = sw_color + (size_t)(y + row) * (size_t)sw_width + x;
			for (int column = start_x; column < end_x; ++column)
				dst[column] = color;
		}
		sw_stats.bench_mag_putpixel +=
			(color == 0xF81Fu) ? (unsigned)((end_x - start_x) * (end_y - start_y)) : 0u;
		return;
	}
	// n = sr*alpha + dr*(255-alpha) reaches at most 248*255 = 63240 because
	// the 565 channel masks cap sr/dr at 0xF8/0xFC, and (n*0x8081)>>23 is
	// n/255 exactly for every n <= 63240 (swept exhaustively; 0x8081 =
	// ceil(2^23/255)). SW_PutPixel's min(255, ...) is dead on this range
	// (max quotient 248). Bit-identical to the per-pixel path it replaces.
	const unsigned src_weight = alpha;
	const unsigned dst_weight = 255u - alpha;
	const unsigned sr = ((color >> 8) & 0xF8u) * src_weight;
	const unsigned sg = ((color >> 3) & 0xFCu) * src_weight;
	const unsigned sb = ((color << 3) & 0xF8u) * src_weight;
	uint16_t rmap[32], gmap[64], bmap[32];
	for (unsigned d = 0; d < 32; ++d)
	{
		const unsigned r = ((sr + (d << 3) * dst_weight) * 0x8081u) >> 23;
		const unsigned b = ((sb + (d << 3) * dst_weight) * 0x8081u) >> 23;
		rmap[d] = (uint16_t)((r & 0xF8u) << 8);
		bmap[d] = (uint16_t)(b >> 3);
	}
	for (unsigned d = 0; d < 64; ++d)
	{
		const unsigned g = ((sg + (d << 2) * dst_weight) * 0x8081u) >> 23;
		gmap[d] = (uint16_t)((g & 0xFCu) << 3);
	}
	const int pixel_count = (end_x - start_x) * (end_y - start_y);
	const qboolean use_rgmap = pixel_count >= 4096;
	if (use_rgmap)
		for (unsigned d = 0; d < 2048; ++d)
			sw_blend_rgmap[d] = (uint16_t)(rmap[d >> 6] | gmap[d & 63u]);
	const uint16_t *const transfer = use_rgmap ?
		sw_blend_lut_get(((uint32_t)color << 8) | alpha, bmap) : NULL;
	unsigned mag = 0;
	if (transfer != NULL)
	{
		for (int row = start_y; row < end_y; ++row)
		{
			uint16_t *const dst = sw_color + (size_t)(y + row) * (size_t)sw_width + x;
			for (int column = start_x; column < end_x; ++column)
			{
				const uint16_t out = transfer[dst[column]];
				dst[column] = out;
				mag += (out == 0xF81Fu);
			}
		}
	}
	else if (use_rgmap)
	{
		for (int row = start_y; row < end_y; ++row)
		{
			uint16_t *const dst = sw_color + (size_t)(y + row) * (size_t)sw_width + x;
			for (int column = start_x; column < end_x; ++column)
			{
				const uint16_t d = dst[column];
				const uint16_t out = (uint16_t)(sw_blend_rgmap[d >> 5] | bmap[d & 31u]);
				dst[column] = out;
				mag += (out == 0xF81Fu);
			}
		}
	}
	else
	{
		for (int row = start_y; row < end_y; ++row)
		{
			uint16_t *const dst = sw_color + (size_t)(y + row) * (size_t)sw_width + x;
			for (int column = start_x; column < end_x; ++column)
			{
				const uint16_t d = dst[column];
				const uint16_t out = (uint16_t)(rmap[d >> 11] | gmap[(d >> 5) & 63u] | bmap[d & 31u]);
				dst[column] = out;
				mag += (out == 0xF81Fu);
			}
		}
	}
	sw_stats.bench_mag_putpixel += mag;
	if (sw_blend_pending)
		sw_stats.bench_post_blend_writes += (unsigned)((end_x - start_x) * (end_y - start_y));
}

void SW_Present(void)
{
	sw_blend_pending = false; // census F: UI window for this frame is closed
	const uclock_t start = uclock();
	if (sw_surface == NULL || sw_color == NULL)
		return;
	// Magenta accounting lives only at the write paths (world/alpha/flex/
	// putpixel counters) which now have full per-pixel coverage; the two
	// full-framebuffer scans that used to sit here cost ~7ms/frame for zero
	// additional observability and were removed with the user's sign-off.
	if (sw_stats.bench_magenta_px > 0)
	{
		if (sw_stats.bench_magenta_max < sw_stats.bench_magenta_px)
			sw_stats.bench_magenta_max = sw_stats.bench_magenta_px;
		if (sw_stats.bench_mag_frame == 0)
			sw_stats.bench_mag_frame = sw_stats.frames_presented + 1;
		if (sw_stats.bench_magenta_saved == 0)
		{
			sw_stats.bench_magenta_saved = 1;
			SW_SavePPM("MAGENTA.PPM");
			sw_ri.Con_Printf(PRINT_ALL, "ref_soft: frame %u magenta-px=%u\n",
				sw_stats.bench_mag_frame, sw_stats.bench_magenta_px);
		}
	}
	bool ready = true;
	if (sw_surface->format == SDL_PIXELFORMAT_INDEX8 && sw_present_lut != NULL)
	{
		if (!SDL_LockSurface(sw_surface))
		{
			sw_ri.Con_Printf(PRINT_ALL, "ref_soft: SDL_LockSurface failed: %s\n", SDL_GetError());
			return;
		}
		for (int y = 0; y < sw_height; ++y)
		{
			const uint16_t *src = sw_color + (size_t)y * (size_t)sw_width;
			byte *dst = (byte *)sw_surface->pixels + (size_t)y * (size_t)sw_surface->pitch;
			for (int x = 0; x < sw_width; ++x)
				dst[x] = sw_present_lut[src[x]];
		}
		SDL_UnlockSurface(sw_surface);
	}
	else if (sw_color_owns)
	{
		if (!SDL_LockSurface(sw_surface))
		{
			sw_ri.Con_Printf(PRINT_ALL, "ref_soft: SDL_LockSurface failed: %s\n", SDL_GetError());
			return;
		}
		ready = SDL_ConvertPixels(sw_width, sw_height,
			SDL_PIXELFORMAT_RGB565, sw_color, sw_width * (int)sizeof(*sw_color),
			sw_surface->format, sw_surface->pixels, sw_surface->pitch);
		SDL_UnlockSurface(sw_surface);
	}
	if (!ready || !SDL_UpdateWindowSurface(sw_window))
	{
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: framebuffer presentation failed: %s\n", SDL_GetError());
		return;
	}
	++sw_stats.frames_presented;
	sw_stats.bench_us_present += (unsigned long long)(uclock() - start);
}

unsigned H2DOS_SoftFramebufferChecksum(void)
{
	unsigned hash = 2166136261u;
	const size_t pixels = (size_t)sw_width * (size_t)sw_height;
	for (size_t i = 0; i < pixels; ++i)
	{
		hash ^= sw_color[i];
		hash *= 16777619u;
	}
	return hash;
}

qboolean SW_SavePPM(const char *path)
{
	FILE *file = fopen(path, "wb");
	if (file == NULL)
		return false;
	fprintf(file, "P6\n%d %d\n255\n", sw_width, sw_height);
	for (int y = 0; y < sw_height; ++y)
	{
		for (int x = 0; x < sw_width; ++x)
		{
			const uint16_t pixel = sw_color[(size_t)y * (size_t)sw_width + (size_t)x];
			const unsigned r5 = (pixel >> 11) & 31u;
			const unsigned g6 = (pixel >> 5) & 63u;
			const unsigned b5 = pixel & 31u;
			const byte rgb[3] = {
				(byte)((r5 << 3) | (r5 >> 2)),
				(byte)((g6 << 2) | (g6 >> 4)),
				(byte)((b5 << 3) | (b5 >> 2)) };
			if (fwrite(rgb, 1, sizeof(rgb), file) != sizeof(rgb))
			{
				fclose(file);
				return false;
			}
		}
	}
	return fclose(file) == 0;
}
