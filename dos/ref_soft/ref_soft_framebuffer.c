#include "ref_soft_local.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint16_t *sw_color;
static bool sw_color_owns;
static byte *sw_present_lut;
uint32_t *sw_depth;
int sw_width;
int sw_height;
h2dos_soft_stats_t sw_stats;

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
				if (tw >= 160 && tw <= 1280 && th >= 100 && th <= 960)
				{
					req_w = tw;
					req_h = th;
				}
			}
		}
		SDL_DisplayMode want;
		SDL_zero(want);
		want.format = req_w == 320 && req_h == 240 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_INDEX8;
		want.w = req_w;
		want.h = req_h;
		sw_ri.Con_Printf(0, "ref_soft: sw mode request %dx%d\n", want.w, want.h);
		// Probe ladder: the DOS VESA driver's mode list is sparse; try 32/24-bit
		// at the requested resolution, then RGB565 at the classic 320x240.
		if (!SDL_SetWindowFullscreenMode(sw_window, &want))
		{
			want.format = SDL_PIXELFORMAT_RGB565;
			want.w = 320;
			want.h = 240;
			if (!SDL_SetWindowFullscreenMode(sw_window, &want))
				want.format = 0;
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

	const size_t pixels = (size_t)sw_width * (size_t)sw_height;
	if (sw_surface->format == SDL_PIXELFORMAT_INDEX8)
	{
		// Palettized present (320x200 sibling mode): fixed 3-3-2 palette and a
		// 64KB 565->index LUT; conversion happens per row at present.
		sw_present_lut = malloc(65536);
		for (int i = 0; i < 65536; ++i)
			sw_present_lut[i] = (byte)((((i >> 8) & 0xE0) | ((i >> 6) & 0x1C) | ((i >> 3) & 0x03)));
		SDL_Palette *pal = SDL_CreatePalette(256);
		if (pal != NULL && sw_present_lut != NULL)
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
	}
	// Fast path: with a shader-native RGB565 surface and tight pitch, render
	// straight into the surface bytes; SDL_UpdateWindowSurface does the only
	// copy. Otherwise keep a private framebuffer and convert at present.
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

void SW_Clear(const uint16_t color)
{
	const uclock_t start = uclock();
	const size_t pixels = (size_t)sw_width * (size_t)sw_height;
	sw_clear_color = color;
	// Color fill is deferred: SW_RasterFrame re-fills only frames without a
	// worldmodel. Worldmodel frames are fully repainted by world/sky spans.
	// Depth stores inverse-z (larger = closer), so the far plane is zero.
	memset(sw_depth, 0, pixels * sizeof(*sw_depth));
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
	uint32_t depth_key;
	if (depth > 1e30f)
		depth_key = 0xFFFFFFFFu;
	else
		depth_key = (uint32_t)(depth * SW_DEPTH_SCALE);
	if (depth_key < sw_depth[index])
		return;

	if (alpha == 255 && !additive)
	{
		sw_color[index] = color;
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
		sw_color[index] = SW_RGB565(r, g, b);
	}

	if (depth_write)
	{
		sw_depth[index] = depth_key;
		++sw_stats.depth_writes;
	}
}

void SW_BlendRect(const int x, const int y, const int w, const int h, const uint16_t color, const byte alpha)
{
	if (w <= 0 || h <= 0)
		return;
	const int start_x = (int)max(0ll, -(long long)x);
	const int start_y = (int)max(0ll, -(long long)y);
	const int end_x = (int)min((long long)w, (long long)sw_width - x);
	const int end_y = (int)min((long long)h, (long long)sw_height - y);
	for (int row = start_y; row < end_y; ++row)
		for (int column = start_x; column < end_x; ++column)
			SW_PutPixel(x + column, y + row, FLT_MAX, color, alpha, false, false);
}

void SW_Present(void)
{
	const uclock_t start = uclock();
	if (sw_surface == NULL || sw_color == NULL)
		return;
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
