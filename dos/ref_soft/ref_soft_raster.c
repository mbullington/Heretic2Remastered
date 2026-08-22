#include "ref_soft_local.h"
#include "Vector.h"
#include "Matrix.h"
#include "turbsin.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

refdef_t sw_refdef;
sw_model_t *sw_world_model;

static vec3_t view_forward;
static vec3_t view_right;
static vec3_t view_up;
static float projection_x;
static float projection_y;

#define SW_SHADEDOT_QUANT 16
extern float r_avertexnormal_dots[SW_SHADEDOT_QUANT][256];

typedef struct screen_vertex_s {
	float x;
	float y;
	float inverse_z;
	float s_over_z;
	float t_over_z;
	float light_s_over_z;
	float light_t_over_z;
} screen_vertex_t;

static int little_ushort(const ushort value)
{
	return (int)(ushort)LittleShort((short)value);
}

static int read_little_int(const byte *source)
{
	int value;
	memcpy(&value, source, sizeof(value));
	return LittleLong(value);
}

static float dot3(const vec3_t a, const vec3_t b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static qboolean sw_sprite_no_depth_test;
// Bench attribution for shared triangle code: 0=other, 1=flex model, 2=sprite.
static int sw_bench_tri_category;
static qboolean normalize_checked(vec3_t vector);
static void draw_flex_triangle(const vec3_t points[3], const float uv[3][2], const float colors[3][4],
	const sw_image_t *image, const qboolean depth_write, const qboolean additive);

static void setup_view(const refdef_t *fd)
{
	AngleVectors(fd->viewangles, view_forward, view_right, view_up);
	static int cached_width = -1, cached_height = -1;
	static float cached_fov_x, cached_fov_y;
	if (fd->width != cached_width || fd->height != cached_height ||
		fd->fov_x != cached_fov_x || fd->fov_y != cached_fov_y)
	{
		projection_x = fd->width * 0.5f / tanf(fd->fov_x * (float)M_PI / 360.0f);
		projection_y = fd->height * 0.5f / tanf(fd->fov_y * (float)M_PI / 360.0f);
		cached_width = fd->width;
		cached_height = fd->height;
		cached_fov_x = fd->fov_x;
		cached_fov_y = fd->fov_y;
	}
}

static sw_vertex_t world_to_view(const vec3_t point, const float s, const float t)
{
	vec3_t delta = {
		point[0] - sw_refdef.vieworg[0],
		point[1] - sw_refdef.vieworg[1],
		point[2] - sw_refdef.vieworg[2]
	};
	sw_vertex_t output;
	output.x = dot3(delta, view_right);
	output.y = dot3(delta, view_up);
	output.z = dot3(delta, view_forward);
	output.s = s;
	output.t = t;
	output.light_s = s;
	output.light_t = t;
	return output;
}

static int clip_near(const sw_vertex_t *input, const int input_count, sw_vertex_t *output)
{
	int output_count = 0;
	for (int i = 0; i < input_count; ++i)
	{
		const sw_vertex_t current = input[i];
		const sw_vertex_t previous = input[(i + input_count - 1) % input_count];
		const qboolean current_inside = current.z >= SW_NEAR_CLIP;
		const qboolean previous_inside = previous.z >= SW_NEAR_CLIP;
		if (current_inside != previous_inside)
		{
			const float fraction = (SW_NEAR_CLIP - previous.z) / (current.z - previous.z);
			sw_vertex_t intersection;
			intersection.x = previous.x + (current.x - previous.x) * fraction;
			intersection.y = previous.y + (current.y - previous.y) * fraction;
			intersection.z = SW_NEAR_CLIP;
			intersection.s = previous.s + (current.s - previous.s) * fraction;
			intersection.t = previous.t + (current.t - previous.t) * fraction;
			intersection.light_s = previous.light_s + (current.light_s - previous.light_s) * fraction;
			intersection.light_t = previous.light_t + (current.light_t - previous.light_t) * fraction;
			output[output_count++] = intersection;
		}
		if (current_inside)
			output[output_count++] = current;
	}
	return output_count;
}

static screen_vertex_t project(const sw_vertex_t vertex)
{
	const float inverse_z = 1.0f / vertex.z;
	screen_vertex_t output;
	output.x = sw_refdef.x + sw_refdef.width * 0.5f + vertex.x * projection_x * inverse_z;
	output.y = sw_refdef.y + sw_refdef.height * 0.5f - vertex.y * projection_y * inverse_z;
	output.inverse_z = inverse_z;
	output.s_over_z = vertex.s * inverse_z;
	output.t_over_z = vertex.t * inverse_z;
	output.light_s_over_z = vertex.light_s * inverse_z;
	output.light_t_over_z = vertex.light_t * inverse_z;
	return output;
}

static float edge(const screen_vertex_t a, const screen_vertex_t b, const float x, const float y)
{
	return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

static uint16_t modulate(const uint16_t color, const float red, const float green, const float blue,
	const float alpha, byte *out_alpha)
{
	// 565 colorkey: source 0x0000 is fully transparent; anything else has
	// alpha 255, so the out alpha collapses to (255 * alpha) or zero.
	if (color == 0)
	{
		*out_alpha = 0;
		return 0;
	}
	*out_alpha = (byte)ClampI((int)(255.0f * alpha), 0, 255);
	return SW_RGB565(
		ClampI((int)(((color >> 8) & 0xF8u) * red), 0, 255),
		ClampI((int)(((color >> 3) & 0xFCu) * green), 0, 255),
		ClampI((int)(((color << 3) & 0xF8u) * blue), 0, 255));
}

static void lightmap_color(const sw_bsp_t *bsp, const dface_t *face, const float s, const float t,
	const float texture_min_s, const float texture_min_t, const int light_width, const int light_height,
	float *red, float *green, float *blue)
{
	*red = *green = *blue = 1.0f;
	if (face == NULL)
		return;
	const int light_offset = LittleLong(face->lightofs);
	if (light_offset < 0 || bsp->lighting_length == 0)
		return;
	const int light_x = ClampI((int)((s - texture_min_s) / 16.0f), 0, light_width - 1);
	const int light_y = ClampI((int)((t - texture_min_t) / 16.0f), 0, light_height - 1);
	const size_t layer_size = (size_t)light_width * (size_t)light_height * 3;
	float sum[3] = { 0.0f, 0.0f, 0.0f };
	qboolean sampled = false;
	for (int style = 0; style < MAXLIGHTMAPS && face->styles[style] != 255; ++style)
	{
		const size_t offset = (size_t)light_offset + (size_t)style * layer_size +
			((size_t)light_y * (size_t)light_width + (size_t)light_x) * 3;
		if (offset + 3 > (size_t)bsp->lighting_length)
			break;
		const int style_index = face->styles[style];
		const lightstyle_t *lightstyle = sw_refdef.lightstyles != NULL ? &sw_refdef.lightstyles[style_index] : NULL;
		sum[0] += bsp->lighting[offset] / 255.0f * (lightstyle != NULL ? lightstyle->rgb[0] : 1.0f);
		sum[1] += bsp->lighting[offset + 1] / 255.0f * (lightstyle != NULL ? lightstyle->rgb[1] : 1.0f);
		sum[2] += bsp->lighting[offset + 2] / 255.0f * (lightstyle != NULL ? lightstyle->rgb[2] : 1.0f);
		sampled = true;
	}
	if (sampled)
	{
		*red = min(2.0f, sum[0]);
		*green = min(2.0f, sum[1]);
		*blue = min(2.0f, sum[2]);
	}
}

static qboolean screen_vertex_valid(const screen_vertex_t vertex)
{
	return isfinite(vertex.x) && isfinite(vertex.y) && isfinite(vertex.inverse_z) &&
		fabsf(vertex.x) <= 10000000.0f && fabsf(vertex.y) <= 10000000.0f;
}

// ---------------------------------------------------------------------------
// Span engine: convex-polygon scan conversion with 16-pixel subdivided affine
// texture stepping (Quake2-style), integer fixed-point inner loops, and the
// inverse-z depth convention. Replaces per-pixel barycentric division.

#define SW_SPAN_STEP 32
// World faces: 32px subdivision quantum halves the FDIV+FIST-heavy block
// prologue count (~11.4K/frame); perspective linearization drift stays inside
// the documented 2-LSB budget (gate-verified).
#define SW_WORLD_SPAN_STEP 128
#define SW_LIGHT_CACHE_MAX 64
// Dense build scratch (lw x lh) and the band-packed working buffer (lw+2 x lh+2).
static uint32_t sw_face_light_dense[SW_LIGHT_CACHE_MAX * SW_LIGHT_CACHE_MAX];
static uint32_t sw_face_light_cache[(SW_LIGHT_CACHE_MAX + 2) * (SW_LIGHT_CACHE_MAX + 2)];

// Reciprocal table replaces the per-block integer divide of the span stepping
// code ((u1-u0)*65536/len) with one float multiply: an fdiv is ~39 cycles on a
// 486/Pentium and is worth hundreds of cycles per triangle on small spans.
#define SW_INV_WIDTH_MAX 128
static float sw_inv_width[SW_INV_WIDTH_MAX + 1];
static void sw_init_inv_width(void)
{
	if (sw_inv_width[1] != 0.0f)
		return;
	sw_inv_width[1] = 1.0f;
	for (int i = 2; i <= SW_INV_WIDTH_MAX; ++i)
		sw_inv_width[i] = 1.0f / (float)i;
}
#define SW_INV_WIDTH(len) ((len) <= SW_INV_WIDTH_MAX ? sw_inv_width[len] : 1.0f / (float)(len))

// Pack face lighting into 10-bit-per-channel fixed point ([0..512], 256 == 1.0)
// once per face per frame instead of running the style loop per pixel.

// Persistent per-face caches: the lightstyle table rarely changes between
// frames, so rebuilding every miniature every frame costs thousands of integer
// divides for nothing. Samples persist per face index and are invalidated only
// when the lightstyle rgb values actually change.
typedef struct sw_face_light_store_s {
	int light_width;
	int light_height;
	uint32_t *samples;
	unsigned generation;
	int num_styles;
	byte styles[MAXLIGHTMAPS];
	int style_fixed[MAXLIGHTMAPS][3];
} sw_face_light_store_t;
static sw_face_light_store_t *sw_face_light_stores;
static int sw_face_light_store_count;
static unsigned sw_face_light_generation;
static const void *sw_face_light_world;

// Pack the dense lw x lh miniature into a (lw+2) x (lh+2) band with
// edge-extended rows and columns. Spans can then index light cells without
// any per-pixel clamping (sub-texel overshoot never exceeds one cell).
static void band_pack_light(const uint32_t *dense, const int lw, const int lh, uint32_t *band)
{
	const int bw = lw + 2;
	for (int r = 0; r < lh; ++r)
		memcpy(band + (r + 1) * bw + 1, dense + r * lw, sizeof(uint32_t) * (size_t)lw);
	for (int r = 1; r <= lh; ++r)
	{
		band[r * bw] = band[r * bw + 1];
		band[r * bw + lw + 1] = band[r * bw + lw];
	}
	memcpy(band, band + bw, sizeof(uint32_t) * (size_t)bw);
	memcpy(band + (lh + 1) * bw, band + lh * bw, sizeof(uint32_t) * (size_t)bw);
}

// E22/ideas#22: shade-row LUTs for the pow2 FTB lane. span_modulate3's per
// channel min(31,(c5*l)>>8) / min(63,(c6*l)>>8) over the full (channel,light)
// domain is 96KB of tables, but the working set per cell = 3 rows of 32/64
// bytes: hot. Built once, row pointers refreshed at light-class moves.
// min(q,(c*l)>>8) per entry == span_modulate3's exact form (E19c-verified
// nested-floor identity), bit-identical by construction.
static byte sw_mul5shade[1024][32];
static byte sw_mul6shade[1024][64];
static qboolean sw_shade_ready;
static void sw_shade_init(void)
{
	sw_shade_ready = true;
	for (int l = 0; l < 1024; ++l)
		for (int c = 0; c < 64; ++c)
		{
			const unsigned v = ((unsigned)c * (unsigned)l) >> 8;
			if (c < 32)
				sw_mul5shade[l][c] = (byte)min(v, 31u);
			sw_mul6shade[l][c] = (byte)min(v, 63u);
		}
}

// span_modulate with the light channels pre-unpacked (avoids re-extracting
// them per pixel; the caller keeps them in registers per light cell).
static inline uint16_t span_modulate3(const uint16_t texel, const unsigned lr, const unsigned lg,
	const unsigned lb)
{
	// E19c/ideas#19(c): the 8-bit expansion cancels algebraically. With
	// r5 = texel>>11, old R = min(255,(r5*lr)>>5)>>3 == min(31,(r5*lr)>>8)
	// (nested floor(floor(x/32)/8) = floor(x/256) for x <= 31713; clamp classes
	// agree at both ends). Green/blue identically. Bit-identical output, six
	// fewer ops per px. The 128KB product-LUT form stays CLOSED (run #5).
	unsigned r = (((texel >> 11) & 31u) * lr) >> 8;
	unsigned g = (((texel >> 5) & 63u) * lg) >> 8;
	unsigned b = ((texel & 31u) * lb) >> 8;
	if (r > 31u) r = 31u;
	if (g > 63u) g = 63u;
	if (b > 31u) b = 31u;
	return (uint16_t)((r << 11) | (g << 5) | b);
}

// Style factors (style * 128, fixed point) plus the style layer indexes a
// face references. Doubles as the per-face rebuild signature for the
// cross-frame light store: a face is rebuilt only when its own styles
// changed, so torch flicker no longer invalidates the whole pool.
static int face_style_signature(const dface_t *face, int style_fixed[MAXLIGHTMAPS][3],
	byte styles[MAXLIGHTMAPS])
{
	int num_styles = 0;
	for (int style = 0; style < MAXLIGHTMAPS && face->styles[style] != 255; ++style)
	{
		const int style_index = face->styles[style];
		const lightstyle_t *lightstyle = sw_refdef.lightstyles != NULL ? &sw_refdef.lightstyles[style_index] : NULL;
		for (int channel = 0; channel < 3; ++channel)
			style_fixed[num_styles][channel] = (int)((lightstyle != NULL ? lightstyle->rgb[channel] : 1.0f) * 128.0f + 0.5f);
		styles[num_styles] = face->styles[style];
		++num_styles;
	}
	return num_styles;
}

static qboolean build_face_light(const sw_bsp_t *bsp, const dface_t *face, const int light_width,
	const int light_height, uint32_t *target)
{
	if (face == NULL)
	{
		const uint32_t white = (256u << 20) | (256u << 10) | 256u;
		const int count = light_width * light_height > 0 ? light_width * light_height : SW_LIGHT_CACHE_MAX * SW_LIGHT_CACHE_MAX;
		for (int i = 0; i < count; ++i)
			target[i] = white;
		return true;
	}
	if (light_width > SW_LIGHT_CACHE_MAX || light_height > SW_LIGHT_CACHE_MAX)
		return false;
	const int light_offset = LittleLong(face->lightofs);
	if (light_offset < 0 || bsp->lighting_length == 0)
	{
		const uint32_t white = (256u << 20) | (256u << 10) | 256u;
		for (int i = 0; i < light_width * light_height; ++i)
			target[i] = white;
		return true;
	}
	const size_t layer_size = (size_t)light_width * (size_t)light_height * 3;
	byte styles[MAXLIGHTMAPS];
	int style_fixed[MAXLIGHTMAPS][3];
	const int num_styles = face_style_signature(face, style_fixed, styles);
	if (num_styles == 0)
	{
		const uint32_t white = (256u << 20) | (256u << 10) | 256u;
		for (int i = 0; i < light_width * light_height; ++i)
			target[i] = white;
		return true;
	}
	const long sample_count = light_width * light_height;
	for (int sample = 0; sample < sample_count; ++sample)
	{
		long acc_r = 0, acc_g = 0, acc_b = 0;
		for (int style = 0; style < num_styles; ++style)
		{
			const size_t offset = (size_t)light_offset + (size_t)style * layer_size + (size_t)sample * 3;
			if (offset + 3 > (size_t)bsp->lighting_length)
				break;
			acc_r += bsp->lighting[offset] * (long)style_fixed[style][0];
			acc_g += bsp->lighting[offset + 1] * (long)style_fixed[style][1];
			acc_b += bsp->lighting[offset + 2] * (long)style_fixed[style][2];
		}
		// acc units: byte * (style * 128); target units: light * 256 clamped to [0..512]
		// L = acc*2/255 approximated with a reciprocal multiply: (acc*2+127)*0x8081>>23
		const long r = min(512, ((acc_r * 2 + 127) * 257) >> 16);
		const long g = min(512, ((acc_g * 2 + 127) * 257) >> 16);
		const long b = min(512, ((acc_b * 2 + 127) * 257) >> 16);
		target[sample] = (uint32_t)r << 20 | (uint32_t)g << 10 | (uint32_t)b;
	}
	return true;
}

static inline uint16_t span_modulate(const uint16_t texel, const uint32_t light_packed)
{
	unsigned r = ((((texel >> 8) & 0xF8u) * ((light_packed >> 20) & 1023u)) + 128u) >> 8;
	unsigned g = ((((texel >> 3) & 0xFCu) * ((light_packed >> 10) & 1023u)) + 128u) >> 8;
	unsigned b = ((((texel << 3) & 0xF8u) * (light_packed & 1023u)) + 128u) >> 8;
	if (r > 255u) r = 255u;
	if (g > 255u) g = 255u;
	if (b > 255u) b = 255u;
	return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

// E59b S-buffer coverage mask: one bit per pixel, set when a world pixel
// accepts. Because world faces draw front-to-back (BSP plane order = depth
// order for disjoint convex content), a set bit MEANS 'the z-buffer value
// here is >= this candidate key' — so the mask test is bit-exactly the
// z-test outcome, and the world loop never reads zb at all. zb writes stay
// for entities/warp/sky. Alpha-test faces take the per-px lane instead.
#define SW_COVER_WORDS 20 // 640 px max / 32
extern uint32_t sw_covermask[480][SW_COVER_WORDS];

// The coverage mask is only equivalent to a z-test while the static world is
// emitted in strict front-to-back BSP order. Brush entities do not satisfy
// that invariant.
static qboolean sw_world_ftb_mask_active;
static qboolean sw_brush_span_active;
static qboolean project_point(const vec3_t point, int *x, int *y, float *depth);

#define SW_WORLD_SHADE_SLOTS 4096
typedef struct sw_world_shade_slot_s {
	const sw_image_t *image;
	uint32_t light;
	uint16_t row[256];
} sw_world_shade_slot_t;

static sw_world_shade_slot_t sw_world_shade_cache[SW_WORLD_SHADE_SLOTS];
static uint16_t sw_world_shade_scratch[256];
static const sw_image_t *sw_world_shade_image;

static void sw_world_build_shade_row(uint16_t row[256], const sw_image_t *image, const uint32_t light)
{
	if (!sw_shade_ready)
		sw_shade_init();
	const byte *const mul5r = sw_mul5shade[(light >> 20) & 1023u];
	const byte *const mul6g = sw_mul6shade[(light >> 10) & 1023u];
	const byte *const mul5b = sw_mul5shade[light & 1023u];
	for (int i = 0; i < 256; ++i)
	{
		const uint16_t texel = image->pal565[i];
		row[i] = (uint16_t)((mul5r[(texel >> 11) & 31u] << 11) |
			(mul6g[(texel >> 5) & 63u] << 5) | mul5b[texel & 31u]);
	}
}

const uint16_t *sw_world_shade_row(const uint32_t light)
{
	unsigned slot = (unsigned)(((uintptr_t)sw_world_shade_image >> 4) ^
		((uintptr_t)sw_world_shade_image >> 13) ^ light ^ (light >> 16)) & (SW_WORLD_SHADE_SLOTS - 1);
	for (unsigned probe = 0; probe < SW_WORLD_SHADE_SLOTS; ++probe)
	{
		sw_world_shade_slot_t *const entry = &sw_world_shade_cache[slot];
		if (entry->image == sw_world_shade_image && entry->light == light)
		{
			++sw_stats.bench_world_shade_hits;
			return entry->row;
		}
		if (entry->image == NULL)
		{
			entry->image = sw_world_shade_image;
			entry->light = light;
			sw_world_build_shade_row(entry->row, sw_world_shade_image, light);
			++sw_stats.bench_world_shade_misses;
			++sw_stats.bench_world_shade_builds;
			return entry->row;
		}
		slot = (slot + 1) & (SW_WORLD_SHADE_SLOTS - 1);
	}
	++sw_stats.bench_world_shade_misses;
	++sw_stats.bench_world_shade_builds;
	++sw_stats.bench_world_shade_overflow;
	sw_world_build_shade_row(sw_world_shade_scratch, sw_world_shade_image, light);
	return sw_world_shade_scratch;
}

void SW_WorldShadeCacheReset(void)
{
	memset(sw_world_shade_cache, 0, sizeof(sw_world_shade_cache));
	sw_world_shade_image = NULL;
}

static int sw_tag_face_paint_count;
static int sw_tag_face_paint_last;

typedef struct sw_world_paint_ctx_s {
	uint16_t *fb;
	uint32_t *zb;
	const uint16_t *pixels;
	const uint32_t *light_cache;
	const byte *mul5base;
	const byte *mul6base;
	const byte *mul5r;
	const byte *mul6g;
	const byte *mul5b;
	int texture_width;
	int mask_w;
	int mask_h;
	int light_stride;
	int run;
	int iu;
	int it;
	int ils;
	int ilt;
	int iu_step;
	int it_step;
	int ils_step;
	int ilt_step;
	uint32_t ikey;
	int ikey_step;
	int light_prev_idx;
	unsigned mag;
	unsigned accepted;
} sw_world_paint_ctx_t;

typedef char sw_world_paint_ctx_size_check[sizeof(sw_world_paint_ctx_t) == 108 ? 1 : -1];
typedef char sw_world_paint_ctx_iu_check[offsetof(sw_world_paint_ctx_t, iu) == 56 ? 1 : -1];
typedef char sw_world_paint_ctx_key_check[offsetof(sw_world_paint_ctx_t, ikey) == 88 ? 1 : -1];

static sw_world_paint_ctx_t sw_world_paint_ctx;
extern void sw_world_paint_run_pmmx(void);
extern void sw_world_paint_m8_run_pmmx(void);
extern void sw_world_paint_m8_packed_pmmx(void);
extern void sw_world_paint_zrun_pmmx(void);
extern uint32_t *sw_world_patch_sites[8];
extern uint32_t *sw_world_packed_patch_sites[20];

__asm__(
	".intel_syntax noprefix\n"
	".align 16\n"
	".globl _sw_world_paint_run_pmmx\n"
	"_sw_world_paint_run_pmmx:\n"
	"push ebp\n"
	"push ebx\n"
	"push esi\n"
	"push edi\n"
	"mov edi, DWORD PTR [_sw_world_paint_ctx+0]\n"
	"mov esi, DWORD PTR [_sw_world_paint_ctx+4]\n"
	"mov ebp, DWORD PTR [_sw_world_paint_ctx+88]\n"
	".globl _sw_world_kernel_px_prefix\n"
	"_sw_world_kernel_px_prefix:\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+60]\n"
	"sar eax, 16\n"
	".byte 0x25\n"
	".Lsw_patch_mask_h: .long 0x12345678\n"
	".byte 0x69, 0xc0\n"
	".Lsw_patch_width: .long 0x12345678\n"
	"mov ecx, DWORD PTR [_sw_world_paint_ctx+56]\n"
	"sar ecx, 16\n"
	".byte 0x81, 0xe1\n"
	".Lsw_patch_mask_w: .long 0x12345678\n"
	"add eax, ecx\n"
	".byte 0x0f, 0xb7, 0x1c, 0x45\n"
	".Lsw_patch_pixels: .long 0x12345678\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+68]\n"
	"shr eax, 20\n"
	"imul eax, DWORD PTR [_sw_world_paint_ctx+48]\n"
	"mov ecx, DWORD PTR [_sw_world_paint_ctx+64]\n"
	"shr ecx, 20\n"
	"add eax, ecx\n"
	"cmp eax, DWORD PTR [_sw_world_paint_ctx+96]\n"
	"jne _sw_world_kernel_light_reload\n"
	"jmp _sw_world_kernel_shade_store\n"
	".globl _sw_world_kernel_light_reload\n"
	"_sw_world_kernel_light_reload:\n"
	"mov DWORD PTR [_sw_world_paint_ctx+96], eax\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+12]\n"
	"mov edx, DWORD PTR [edx+eax*4]\n"
	"mov eax, edx\n"
	"shr eax, 20\n"
	"and eax, 1023\n"
	"shl eax, 5\n"
	"add eax, DWORD PTR [_sw_world_paint_ctx+16]\n"
	"mov DWORD PTR [_sw_world_paint_ctx+24], eax\n"
	"mov eax, edx\n"
	"shr eax, 10\n"
	"and eax, 1023\n"
	"shl eax, 6\n"
	"add eax, DWORD PTR [_sw_world_paint_ctx+20]\n"
	"mov DWORD PTR [_sw_world_paint_ctx+28], eax\n"
	"and edx, 1023\n"
	"shl edx, 5\n"
	"add edx, DWORD PTR [_sw_world_paint_ctx+16]\n"
	"mov DWORD PTR [_sw_world_paint_ctx+32], edx\n"
	".globl _sw_world_kernel_shade_store\n"
	"_sw_world_kernel_shade_store:\n"
	"mov eax, ebx\n"
	"shr eax, 5\n"
	"and eax, 63\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+28]\n"
	"movzx ecx, BYTE PTR [edx+eax]\n"
	"shl ecx, 5\n"
	"mov eax, ebx\n"
	"shr eax, 11\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+24]\n"
	"movzx eax, BYTE PTR [edx+eax]\n"
	"shl eax, 11\n"
	"or ecx, eax\n"
	"and ebx, 31\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+32]\n"
	"movzx eax, BYTE PTR [edx+ebx]\n"
	"or eax, ecx\n"
	"xor edx, edx\n"
	"cmp ax, 0xf81f\n"
	"sete dl\n"
	"add DWORD PTR [_sw_world_paint_ctx+100], edx\n"
	"mov WORD PTR [edi], ax\n"
	"mov DWORD PTR [esi], ebp\n"
	".globl _sw_world_kernel_advance\n"
	"_sw_world_kernel_advance:\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+72]\n"
	"add DWORD PTR [_sw_world_paint_ctx+56], eax\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+76]\n"
	"add DWORD PTR [_sw_world_paint_ctx+60], eax\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+80]\n"
	"add DWORD PTR [_sw_world_paint_ctx+64], eax\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+84]\n"
	"add DWORD PTR [_sw_world_paint_ctx+68], eax\n"
	"add ebp, DWORD PTR [_sw_world_paint_ctx+92]\n"
	"add edi, 2\n"
	"add esi, 4\n"
	"dec DWORD PTR [_sw_world_paint_ctx+52]\n"
	"jnz _sw_world_kernel_px_prefix\n"
	".globl _sw_world_kernel_end\n"
	"_sw_world_kernel_end:\n"
	"mov DWORD PTR [_sw_world_paint_ctx+88], ebp\n"
	"pop edi\n"
	"pop esi\n"
	"pop ebx\n"
	"pop ebp\n"
	"ret\n"
	".align 16\n"
	".globl _sw_world_paint_m8_run_pmmx\n"
	"_sw_world_paint_m8_run_pmmx:\n"
	"push ebp\n"
	"push ebx\n"
	"push esi\n"
	"push edi\n"
	"mov edi, DWORD PTR [_sw_world_paint_ctx+0]\n"
	"mov esi, DWORD PTR [_sw_world_paint_ctx+4]\n"
	"mov ebp, DWORD PTR [_sw_world_paint_ctx+88]\n"
	"_sw_world_m8kernel_px_prefix:\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+60]\n"
	"sar eax, 16\n"
	".byte 0x25\n"
	".Lsw_m8_patch_mask_h: .long 0x12345678\n"
	".byte 0x69, 0xc0\n"
	".Lsw_m8_patch_width: .long 0x12345678\n"
	"mov ecx, DWORD PTR [_sw_world_paint_ctx+56]\n"
	"sar ecx, 16\n"
	".byte 0x81, 0xe1\n"
	".Lsw_m8_patch_mask_w: .long 0x12345678\n"
	"add eax, ecx\n"
	".byte 0x0f, 0xb6, 0x1c, 0x05\n"
	".Lsw_m8_patch_pixels: .long 0x12345678\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+68]\n"
	"shr eax, 20\n"
	"imul eax, DWORD PTR [_sw_world_paint_ctx+48]\n"
	"mov ecx, DWORD PTR [_sw_world_paint_ctx+64]\n"
	"shr ecx, 20\n"
	"add eax, ecx\n"
	"cmp eax, DWORD PTR [_sw_world_paint_ctx+96]\n"
	"je _sw_world_m8kernel_shade_store\n"
	"mov DWORD PTR [_sw_world_paint_ctx+96], eax\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+12]\n"
	"mov edx, DWORD PTR [edx+eax*4]\n"
	"push edx\n"
	"call _sw_world_shade_row\n"
	"add esp, 4\n"
	"mov DWORD PTR [_sw_world_paint_ctx+24], eax\n"
	"_sw_world_m8kernel_shade_store:\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+24]\n"
	"movzx eax, WORD PTR [edx+ebx*2]\n"
	"xor edx, edx\n"
	"cmp ax, 0xf81f\n"
	"sete dl\n"
	"add DWORD PTR [_sw_world_paint_ctx+100], edx\n"
	"mov WORD PTR [edi], ax\n"
	"mov DWORD PTR [esi], ebp\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+72]\n"
	"add DWORD PTR [_sw_world_paint_ctx+56], eax\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+76]\n"
	"add DWORD PTR [_sw_world_paint_ctx+60], eax\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+80]\n"
	"add DWORD PTR [_sw_world_paint_ctx+64], eax\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+84]\n"
	"add DWORD PTR [_sw_world_paint_ctx+68], eax\n"
	"add ebp, DWORD PTR [_sw_world_paint_ctx+92]\n"
	"add edi, 2\n"
	"add esi, 4\n"
	"dec DWORD PTR [_sw_world_paint_ctx+52]\n"
	"jnz _sw_world_m8kernel_px_prefix\n"
	"mov DWORD PTR [_sw_world_paint_ctx+88], ebp\n"
	"pop edi\n"
	"pop esi\n"
	"pop ebx\n"
	"pop ebp\n"
	"ret\n"
	".align 16\n"
	".globl _sw_world_paint_zrun_pmmx\n"
	"_sw_world_paint_zrun_pmmx:\n"
	"push ebp\n"
	"push ebx\n"
	"push esi\n"
	"push edi\n"
	"mov edi, DWORD PTR [_sw_world_paint_ctx+0]\n"
	"mov esi, DWORD PTR [_sw_world_paint_ctx+4]\n"
	"mov ebp, DWORD PTR [_sw_world_paint_ctx+88]\n"
	"_sw_world_zkernel_px_prefix:\n"
	"cmp ebp, DWORD PTR [esi]\n"
	"jb _sw_world_zkernel_advance\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+60]\n"
	"sar eax, 16\n"
	"and eax, DWORD PTR [_sw_world_paint_ctx+44]\n"
	"imul eax, DWORD PTR [_sw_world_paint_ctx+36]\n"
	"mov ecx, DWORD PTR [_sw_world_paint_ctx+56]\n"
	"sar ecx, 16\n"
	"and ecx, DWORD PTR [_sw_world_paint_ctx+40]\n"
	"add eax, ecx\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+8]\n"
	"movzx ebx, WORD PTR [edx+eax*2]\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+68]\n"
	"shr eax, 20\n"
	"imul eax, DWORD PTR [_sw_world_paint_ctx+48]\n"
	"mov ecx, DWORD PTR [_sw_world_paint_ctx+64]\n"
	"shr ecx, 20\n"
	"add eax, ecx\n"
	"cmp eax, DWORD PTR [_sw_world_paint_ctx+96]\n"
	"je _sw_world_zkernel_shade_store\n"
	"mov DWORD PTR [_sw_world_paint_ctx+96], eax\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+12]\n"
	"mov edx, DWORD PTR [edx+eax*4]\n"
	"mov eax, edx\n"
	"shr eax, 20\n"
	"and eax, 1023\n"
	"shl eax, 5\n"
	"add eax, DWORD PTR [_sw_world_paint_ctx+16]\n"
	"mov DWORD PTR [_sw_world_paint_ctx+24], eax\n"
	"mov eax, edx\n"
	"shr eax, 10\n"
	"and eax, 1023\n"
	"shl eax, 6\n"
	"add eax, DWORD PTR [_sw_world_paint_ctx+20]\n"
	"mov DWORD PTR [_sw_world_paint_ctx+28], eax\n"
	"and edx, 1023\n"
	"shl edx, 5\n"
	"add edx, DWORD PTR [_sw_world_paint_ctx+16]\n"
	"mov DWORD PTR [_sw_world_paint_ctx+32], edx\n"
	"_sw_world_zkernel_shade_store:\n"
	"mov eax, ebx\n"
	"shr eax, 5\n"
	"and eax, 63\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+28]\n"
	"movzx ecx, BYTE PTR [edx+eax]\n"
	"shl ecx, 5\n"
	"mov eax, ebx\n"
	"shr eax, 11\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+24]\n"
	"movzx eax, BYTE PTR [edx+eax]\n"
	"shl eax, 11\n"
	"or ecx, eax\n"
	"and ebx, 31\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+32]\n"
	"movzx eax, BYTE PTR [edx+ebx]\n"
	"or eax, ecx\n"
	"test ax, ax\n"
	"jz _sw_world_zkernel_advance\n"
	"xor edx, edx\n"
	"cmp ax, 0xf81f\n"
	"sete dl\n"
	"add DWORD PTR [_sw_world_paint_ctx+100], edx\n"
	"mov WORD PTR [edi], ax\n"
	"mov DWORD PTR [esi], ebp\n"
	"inc DWORD PTR [_sw_world_paint_ctx+104]\n"
	"_sw_world_zkernel_advance:\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+72]\n"
	"add DWORD PTR [_sw_world_paint_ctx+56], eax\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+76]\n"
	"add DWORD PTR [_sw_world_paint_ctx+60], eax\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+80]\n"
	"add DWORD PTR [_sw_world_paint_ctx+64], eax\n"
	"mov eax, DWORD PTR [_sw_world_paint_ctx+84]\n"
	"add DWORD PTR [_sw_world_paint_ctx+68], eax\n"
	"add ebp, DWORD PTR [_sw_world_paint_ctx+92]\n"
	"add edi, 2\n"
	"add esi, 4\n"
	"dec DWORD PTR [_sw_world_paint_ctx+52]\n"
	"jnz _sw_world_zkernel_px_prefix\n"
	"mov DWORD PTR [_sw_world_paint_ctx+88], ebp\n"
	"pop edi\n"
	"pop esi\n"
	"pop ebx\n"
	"pop ebp\n"
	"ret\n"
	".data\n"
	".align 4\n"
	".globl _sw_world_patch_sites\n"
	"_sw_world_patch_sites:\n"
	".long .Lsw_patch_mask_h, .Lsw_patch_width, .Lsw_patch_mask_w, .Lsw_patch_pixels\n"
	".long .Lsw_m8_patch_mask_h, .Lsw_m8_patch_width, .Lsw_m8_patch_mask_w, .Lsw_m8_patch_pixels\n"
	".text\n"
	".att_syntax prefix\n");

__asm__(
	".intel_syntax noprefix\n"
	".align 16\n"
	".globl _sw_world_paint_m8_packed_pmmx\n"
	"_sw_world_paint_m8_packed_pmmx:\n"
	"push ebp\n"
	"push ebx\n"
	"push esi\n"
	"push edi\n"
	"sub esp, 16\n"
	"mov edi, DWORD PTR [_sw_world_paint_ctx+0]\n"
	"mov esi, DWORD PTR [_sw_world_paint_ctx+4]\n"
	"mov ebp, DWORD PTR [_sw_world_paint_ctx+88]\n"
	"movq mm0, QWORD PTR [_sw_world_paint_ctx+56]\n"
	"movq mm1, QWORD PTR [_sw_world_paint_ctx+72]\n"
	"movq mm2, QWORD PTR [_sw_world_paint_ctx+64]\n"
	"movq mm3, QWORD PTR [_sw_world_paint_ctx+80]\n"
	"_sw_world_m8packed_px_prefix:\n"
	// Resolve the current light cell once for either a four-pixel group or a scalar tail.
	"movq QWORD PTR [esp], mm2\n"
	"mov ecx, DWORD PTR [esp]\n"
	"mov eax, DWORD PTR [esp+4]\n"
	"shr ecx, 20\n"
	"shr eax, 20\n"
	"mov DWORD PTR [esp+8], ecx\n"
	"mov DWORD PTR [esp+12], eax\n"
	"imul eax, DWORD PTR [_sw_world_paint_ctx+48]\n"
	"add eax, ecx\n"
	"cmp eax, DWORD PTR [_sw_world_paint_ctx+96]\n"
	"je _sw_world_m8packed_row_ready\n"
	"mov DWORD PTR [_sw_world_paint_ctx+96], eax\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+12]\n"
	"mov edx, DWORD PTR [edx+eax*4]\n"
	"push edx\n"
	"call _sw_world_shade_row\n"
	"add esp, 4\n"
	"mov DWORD PTR [_sw_world_paint_ctx+24], eax\n"
	"_sw_world_m8packed_row_ready:\n"
	"cmp DWORD PTR [_sw_world_paint_ctx+52], 4\n"
	"jl _sw_world_m8packed_scalar\n"
	// A constant signed step is monotone in the bounded light-coordinate range.
	// Equal S and T cells at pixels 0 and 3 therefore prove all four share a row.
	"movq mm4, mm3\n"
	"paddd mm4, mm3\n"
	"paddd mm4, mm3\n"
	"paddd mm4, mm2\n"
	"movq QWORD PTR [esp], mm4\n"
	"mov ecx, DWORD PTR [esp]\n"
	"shr ecx, 20\n"
	"cmp ecx, DWORD PTR [esp+8]\n"
	"jne _sw_world_m8packed_scalar\n"
	"mov eax, DWORD PTR [esp+4]\n"
	"shr eax, 20\n"
	"cmp eax, DWORD PTR [esp+12]\n"
	"jne _sw_world_m8packed_scalar\n"
	"_sw_world_m8packed_four_0:\n"
	"movq QWORD PTR [esp], mm0\n"
	"mov ecx, DWORD PTR [esp]\n"
	"mov eax, DWORD PTR [esp+4]\n"
	"sar eax, 16\n"
	".byte 0x25\n"
	".Lsw_m8packed_patch_mask_h: .long 0x12345678\n"
	".byte 0x69, 0xc0\n"
	".Lsw_m8packed_patch_width: .long 0x12345678\n"
	"sar ecx, 16\n"
	".byte 0x81, 0xe1\n"
	".Lsw_m8packed_patch_mask_w: .long 0x12345678\n"
	"add eax, ecx\n"
	".byte 0x0f, 0xb6, 0x1c, 0x05\n"
	".Lsw_m8packed_patch_pixels: .long 0x12345678\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+24]\n"
	"movzx eax, WORD PTR [edx+ebx*2]\n"
	"mov WORD PTR [edi], ax\n"
	"mov DWORD PTR [esi], ebp\n"
	"paddd mm0, mm1\n"
	"add ebp, DWORD PTR [_sw_world_paint_ctx+92]\n"
	"add edi, 2\n"
	"add esi, 4\n"
	"_sw_world_m8packed_four_1:\n"
	"movq QWORD PTR [esp], mm0\n"
	"mov ecx, DWORD PTR [esp]\n"
	"mov eax, DWORD PTR [esp+4]\n"
	"sar eax, 16\n"
	".byte 0x25\n"
	".Lsw_m8packed1_patch_mask_h: .long 0x12345678\n"
	".byte 0x69, 0xc0\n"
	".Lsw_m8packed1_patch_width: .long 0x12345678\n"
	"sar ecx, 16\n"
	".byte 0x81, 0xe1\n"
	".Lsw_m8packed1_patch_mask_w: .long 0x12345678\n"
	"add eax, ecx\n"
	".byte 0x0f, 0xb6, 0x1c, 0x05\n"
	".Lsw_m8packed1_patch_pixels: .long 0x12345678\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+24]\n"
	"movzx eax, WORD PTR [edx+ebx*2]\n"
	"mov WORD PTR [edi], ax\n"
	"mov DWORD PTR [esi], ebp\n"
	"paddd mm0, mm1\n"
	"add ebp, DWORD PTR [_sw_world_paint_ctx+92]\n"
	"add edi, 2\n"
	"add esi, 4\n"
	"_sw_world_m8packed_four_2:\n"
	"movq QWORD PTR [esp], mm0\n"
	"mov ecx, DWORD PTR [esp]\n"
	"mov eax, DWORD PTR [esp+4]\n"
	"sar eax, 16\n"
	".byte 0x25\n"
	".Lsw_m8packed2_patch_mask_h: .long 0x12345678\n"
	".byte 0x69, 0xc0\n"
	".Lsw_m8packed2_patch_width: .long 0x12345678\n"
	"sar ecx, 16\n"
	".byte 0x81, 0xe1\n"
	".Lsw_m8packed2_patch_mask_w: .long 0x12345678\n"
	"add eax, ecx\n"
	".byte 0x0f, 0xb6, 0x1c, 0x05\n"
	".Lsw_m8packed2_patch_pixels: .long 0x12345678\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+24]\n"
	"movzx eax, WORD PTR [edx+ebx*2]\n"
	"mov WORD PTR [edi], ax\n"
	"mov DWORD PTR [esi], ebp\n"
	"paddd mm0, mm1\n"
	"add ebp, DWORD PTR [_sw_world_paint_ctx+92]\n"
	"add edi, 2\n"
	"add esi, 4\n"
	"_sw_world_m8packed_four_3:\n"
	"movq QWORD PTR [esp], mm0\n"
	"mov ecx, DWORD PTR [esp]\n"
	"mov eax, DWORD PTR [esp+4]\n"
	"sar eax, 16\n"
	".byte 0x25\n"
	".Lsw_m8packed3_patch_mask_h: .long 0x12345678\n"
	".byte 0x69, 0xc0\n"
	".Lsw_m8packed3_patch_width: .long 0x12345678\n"
	"sar ecx, 16\n"
	".byte 0x81, 0xe1\n"
	".Lsw_m8packed3_patch_mask_w: .long 0x12345678\n"
	"add eax, ecx\n"
	".byte 0x0f, 0xb6, 0x1c, 0x05\n"
	".Lsw_m8packed3_patch_pixels: .long 0x12345678\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+24]\n"
	"movzx eax, WORD PTR [edx+ebx*2]\n"
	"mov WORD PTR [edi], ax\n"
	"mov DWORD PTR [esi], ebp\n"
	"paddd mm0, mm1\n"
	"add ebp, DWORD PTR [_sw_world_paint_ctx+92]\n"
	"add edi, 2\n"
	"add esi, 4\n"
	"paddd mm4, mm3\n"
	"movq mm2, mm4\n"
	"sub DWORD PTR [_sw_world_paint_ctx+52], 4\n"
	"jnz _sw_world_m8packed_px_prefix\n"
	"jmp _sw_world_m8packed_end\n"
	"_sw_world_m8packed_scalar:\n"
	"movq QWORD PTR [esp], mm0\n"
	"mov ecx, DWORD PTR [esp]\n"
	"mov eax, DWORD PTR [esp+4]\n"
	"sar eax, 16\n"
	".byte 0x25\n"
	".Lsw_m8packed4_patch_mask_h: .long 0x12345678\n"
	".byte 0x69, 0xc0\n"
	".Lsw_m8packed4_patch_width: .long 0x12345678\n"
	"sar ecx, 16\n"
	".byte 0x81, 0xe1\n"
	".Lsw_m8packed4_patch_mask_w: .long 0x12345678\n"
	"add eax, ecx\n"
	".byte 0x0f, 0xb6, 0x1c, 0x05\n"
	".Lsw_m8packed4_patch_pixels: .long 0x12345678\n"
	"mov edx, DWORD PTR [_sw_world_paint_ctx+24]\n"
	"movzx eax, WORD PTR [edx+ebx*2]\n"
	"mov WORD PTR [edi], ax\n"
	"mov DWORD PTR [esi], ebp\n"
	"paddd mm0, mm1\n"
	"paddd mm2, mm3\n"
	"add ebp, DWORD PTR [_sw_world_paint_ctx+92]\n"
	"add edi, 2\n"
	"add esi, 4\n"
	"dec DWORD PTR [_sw_world_paint_ctx+52]\n"
	"jnz _sw_world_m8packed_px_prefix\n"
	"_sw_world_m8packed_end:\n"
	"movq QWORD PTR [_sw_world_paint_ctx+56], mm0\n"
	"movq QWORD PTR [_sw_world_paint_ctx+64], mm2\n"
	"mov DWORD PTR [_sw_world_paint_ctx+88], ebp\n"
	"emms\n"
	"add esp, 16\n"
	"pop edi\n"
	"pop esi\n"
	"pop ebx\n"
	"pop ebp\n"
	"ret\n"
	".data\n"
	".align 4\n"
	".globl _sw_world_packed_patch_sites\n"
	"_sw_world_packed_patch_sites:\n"
	".long .Lsw_m8packed_patch_mask_h, .Lsw_m8packed_patch_width\n"
	".long .Lsw_m8packed_patch_mask_w, .Lsw_m8packed_patch_pixels\n"
	".long .Lsw_m8packed1_patch_mask_h, .Lsw_m8packed1_patch_width\n"
	".long .Lsw_m8packed1_patch_mask_w, .Lsw_m8packed1_patch_pixels\n"
	".long .Lsw_m8packed2_patch_mask_h, .Lsw_m8packed2_patch_width\n"
	".long .Lsw_m8packed2_patch_mask_w, .Lsw_m8packed2_patch_pixels\n"
	".long .Lsw_m8packed3_patch_mask_h, .Lsw_m8packed3_patch_width\n"
	".long .Lsw_m8packed3_patch_mask_w, .Lsw_m8packed3_patch_pixels\n"
	".long .Lsw_m8packed4_patch_mask_h, .Lsw_m8packed4_patch_width\n"
	".long .Lsw_m8packed4_patch_mask_w, .Lsw_m8packed4_patch_pixels\n"
	".text\n"
	".att_syntax prefix\n");

static void sw_world_patch_material(const uint16_t *pixels, const byte *src8,
	const int width, const int mask_w, const int mask_h)
{
	static const uint16_t *patched_pixels;
	static const byte *patched_src8;
	static int patched_width, patched_mask_w, patched_mask_h;
	if (patched_pixels == pixels && patched_src8 == src8 && patched_width == width &&
		patched_mask_w == mask_w && patched_mask_h == mask_h)
		return;
	*(volatile uint32_t *)sw_world_patch_sites[0] = (uint32_t)mask_h;
	*(volatile uint32_t *)sw_world_patch_sites[1] = (uint32_t)width;
	*(volatile uint32_t *)sw_world_patch_sites[2] = (uint32_t)mask_w;
	*(volatile uint32_t *)sw_world_patch_sites[3] = (uint32_t)(uintptr_t)pixels;
	*(volatile uint32_t *)sw_world_patch_sites[4] = (uint32_t)mask_h;
	*(volatile uint32_t *)sw_world_patch_sites[5] = (uint32_t)width;
	*(volatile uint32_t *)sw_world_patch_sites[6] = (uint32_t)mask_w;
	*(volatile uint32_t *)sw_world_patch_sites[7] = (uint32_t)(uintptr_t)src8;
	for (int lane = 0; lane < 5; ++lane)
	{
		*(volatile uint32_t *)sw_world_packed_patch_sites[lane * 4 + 0] = (uint32_t)mask_h;
		*(volatile uint32_t *)sw_world_packed_patch_sites[lane * 4 + 1] = (uint32_t)width;
		*(volatile uint32_t *)sw_world_packed_patch_sites[lane * 4 + 2] = (uint32_t)mask_w;
		*(volatile uint32_t *)sw_world_packed_patch_sites[lane * 4 + 3] = (uint32_t)(uintptr_t)src8;
	}
	patched_pixels = pixels;
	patched_src8 = src8;
	patched_width = width;
	patched_mask_w = mask_w;
	patched_mask_h = mask_h;
	__asm__ volatile("jmp 1f\n1:" ::: "memory");
}

static void draw_face_spans(const screen_vertex_t *verts, const int n, const sw_image_t *image,
	const sw_bsp_t *bsp, const dface_t *face, const float texture_min_s, const float texture_min_t,
	const int light_width, const int light_height, const float alpha, const int face_index,
	const qboolean alpha_test)
{
	sw_tag_face_paint_count = 0;
	if (n < 3 || image == NULL || image->pixels == NULL)
	{
		if (face_index == sw_dbg_tag_face)
			sw_ri.Con_Printf(PRINT_ALL, "tagface abort n3 bf=%u face=%d n=%d img=%p\n",
				sw_stats.bench_frames, face_index, n, (const void *)image);
		return;
	}
	// Cheap reject before gradient math: whole polygon outside the viewport.
	float minx = verts[0].x, maxx = verts[0].x, miny = verts[0].y, maxy = verts[0].y;
	{
		for (int i = 1; i < n; ++i)
		{
			minx = min(minx, verts[i].x); maxx = max(maxx, verts[i].x);
			miny = min(miny, verts[i].y); maxy = max(maxy, verts[i].y);
		}
		if (maxx < 0.0f || minx >= (float)sw_width || maxy < 0.0f || miny >= (float)sw_height)
		{
			if (face_index == sw_dbg_tag_face)
				sw_ri.Con_Printf(PRINT_ALL, "tagface abort bbox bf=%u face=%d minx=%g maxx=%g miny=%g maxy=%g\n",
					sw_stats.bench_frames, face_index, minx, maxx, miny, maxy);
			return;
		}
		if (face_index == sw_dbg_tag_face)
			sw_ri.Con_Printf(PRINT_ALL, "tagface pregrad bf=%u face=%d minx=%g maxx=%g miny=%g maxy=%g n=%d\n",
				sw_stats.bench_frames, face_index, minx, maxx, miny, maxy, n);
	}

	// Plane gradients in screen space for the five interpolated attributes.
	// u/z, v/z and 1/z are exactly affine in (x, y) for a planar polygon.
	const screen_vertex_t *v0 = &verts[0];
	const screen_vertex_t *v1 = &verts[1];
	const screen_vertex_t *v2 = &verts[2];
	float x10 = v1->x - v0->x, y10 = v1->y - v0->y;
	float x20 = v2->x - v0->x, y20 = v2->y - v0->y;
	float denom = x10 * y20 - x20 * y10;
	// The first three clipped vertices are arbitrary; on thin wall slivers they
	// can lie almost on one polygon edge, making their triangle degenerate and
	// dropping the whole face (holes flickering in/out as the denom crosses the
	// threshold). Re-anchor on the largest-area vertex triple in that case.
	const float bbox_area = (maxx - minx) * (maxy - miny);
	if (fabsf(denom) < 0.05f * bbox_area)
	{
		float best = fabsf(denom);
		const float target = bbox_area > 0.0f ? 0.2f * bbox_area : 0.0f;
		qboolean done = false;
		for (int i = 0; i < n - 2 && !done; ++i)
			for (int j = i + 1; j < n - 1 && !done; ++j)
				for (int k = j + 1; k < n && !done; ++k)
				{
					const float a = (verts[j].x - verts[i].x) * (verts[k].y - verts[i].y) -
						(verts[k].x - verts[i].x) * (verts[j].y - verts[i].y);
					if (fabsf(a) > best)
					{
						best = fabsf(a);
						v0 = &verts[i];
						v1 = &verts[j];
						v2 = &verts[k];
						denom = a;
						done = best >= target;
					}
				}
		x10 = v1->x - v0->x; y10 = v1->y - v0->y;
		x20 = v2->x - v0->x; y20 = v2->y - v0->y;
	}
	if (!isfinite(denom) || fabsf(denom) < 0.0001f)
	{
		if (face_index == sw_dbg_tag_face)
			sw_ri.Con_Printf(PRINT_ALL, "tagface abort denom bf=%u face=%d denom=%g v0=(%g,%g,w=%g) v1=(%g,%g,w=%g) v2=(%g,%g,w=%g) n=%d light=%dx%d\n",
				sw_stats.bench_frames, face_index, denom, v0->x, v0->y, v0->inverse_z, v1->x, v1->y,
				v1->inverse_z, v2->x, v2->y, v2->inverse_z, n, light_width, light_height);
		return;
	}
	if (face_index == sw_dbg_tag_face)
		sw_ri.Con_Printf(PRINT_ALL, "tagface enter bf=%u face=%d denom=%g light=%dx%d n=%d\n",
			sw_stats.bench_frames, face_index, denom, light_width, light_height, n);
	float gradient_uz_x, gradient_uz_y, gradient_vz_x, gradient_vz_y;
	const float inv_denom = 1.0f / denom;
	float gradient_w_x, gradient_w_y, gradient_lsz_x, gradient_lsz_y, gradient_ltz_x, gradient_ltz_y;
#define SW_GRADIENT(attr, out_x, out_y) do { \
		const float p10 = v1->attr - v0->attr; \
		const float p20 = v2->attr - v0->attr; \
		out_x = (p10 * y20 - p20 * y10) * inv_denom; \
		out_y = (p20 * x10 - p10 * x20) * inv_denom; \
	} while (0)
	// E17/ideas#17: draw_face sets vertex.light_s/light_t = base_s/base_t for
	// every face class (only SURF_WARP diverges), and the clipper lerps both
	// attribute pairs with the same fractions, so when the projected verts
	// carry light == texture coords bitwise, the two light gradient chains
	// are bit-identical to the texture chains and alias away (FLOWING's
	// constant scroll breaks the equality and keeps the full path).
	qboolean light_alias = true;
	for (int i = 0; i < n; ++i)
		if (verts[i].s_over_z != verts[i].light_s_over_z ||
			verts[i].t_over_z != verts[i].light_t_over_z)
		{
			light_alias = false;
			break;
		}
	SW_GRADIENT(s_over_z, gradient_uz_x, gradient_uz_y);
	SW_GRADIENT(t_over_z, gradient_vz_x, gradient_vz_y);
	SW_GRADIENT(inverse_z, gradient_w_x, gradient_w_y);
	if (light_alias)
	{
		gradient_lsz_x = gradient_uz_x; gradient_lsz_y = gradient_uz_y;
		gradient_ltz_x = gradient_vz_x; gradient_ltz_y = gradient_vz_y;
	}
	else
	{
		SW_GRADIENT(light_s_over_z, gradient_lsz_x, gradient_lsz_y);
		SW_GRADIENT(light_t_over_z, gradient_ltz_x, gradient_ltz_y);
	}
#undef SW_GRADIENT

	// Lighting miniature for this face, persisted across frames while the
	// lightstyle table is stable (rebuild costs thousands of int divides).
	const uint32_t *light_cache;
	if (face != NULL && face_index >= 0 && light_width <= SW_LIGHT_CACHE_MAX &&
		light_height <= SW_LIGHT_CACHE_MAX)
	{
		if (face_index >= sw_face_light_store_count)
		{
			sw_face_light_store_t *grown = realloc(sw_face_light_stores,
				sizeof(*sw_face_light_stores) * (size_t)(face_index + 1));
			if (grown == NULL)
				return;
			memset(grown + sw_face_light_store_count, 0,
				sizeof(*grown) * (size_t)(face_index + 1 - sw_face_light_store_count));
			sw_face_light_stores = grown;
			sw_face_light_store_count = face_index + 1;
		}
		sw_face_light_store_t *store = &sw_face_light_stores[face_index];
		byte want_styles[MAXLIGHTMAPS];
		int want_fixed[MAXLIGHTMAPS][3];
		const int want_num_styles = face_style_signature(face, want_fixed, want_styles);
		qboolean stale = store->generation != sw_face_light_generation || store->samples == NULL ||
			store->light_width != light_width || store->light_height != light_height ||
			store->num_styles != want_num_styles;
		if (!stale)
			for (int s = 0; s < want_num_styles; ++s)
				if (store->styles[s] != want_styles[s] || store->style_fixed[s][0] != want_fixed[s][0] ||
					store->style_fixed[s][1] != want_fixed[s][1] || store->style_fixed[s][2] != want_fixed[s][2])
				{
					stale = true;
					break;
				}
		if (stale)
		{
			uint32_t *grown = realloc(store->samples,
				sizeof(*store->samples) * (size_t)(light_width + 2) * (size_t)(light_height + 2));
			if (grown == NULL)
				return;
			store->samples = grown;
			store->light_width = light_width;
			store->light_height = light_height;
			if (!build_face_light(bsp, face, light_width, light_height, sw_face_light_dense))
				return;
			band_pack_light(sw_face_light_dense, light_width, light_height, store->samples);
			store->num_styles = want_num_styles;
			memcpy(store->styles, want_styles, sizeof(store->styles));
			memcpy(store->style_fixed, want_fixed, sizeof(store->style_fixed));
			store->generation = sw_face_light_generation;
		}
		light_cache = store->samples;
	}
	else
	{
		if (!build_face_light(bsp, face, light_width, light_height, sw_face_light_dense))
			return; // oversized lightmaps: caller falls back to the per-pixel path
		band_pack_light(sw_face_light_dense, light_width, light_height, sw_face_light_cache);
		light_cache = sw_face_light_cache;
	}
	const int light_min_s = (int)texture_min_s;
	const int light_min_t = (int)texture_min_t;
	const int light_stride = light_width + 2;

	const qboolean twist_pow2 = (image->width & (image->width - 1)) == 0;
	const qboolean theight_pow2 = (image->height & (image->height - 1)) == 0;
	const int mask_w = image->width - 1;
	const int mask_h = image->height - 1;
	if (sw_world_ftb_mask_active && twist_pow2 && theight_pow2)
		sw_world_patch_material(image->pixels, image->src8, image->width, mask_w, mask_h);

	// Scan conversion of the convex polygon (verts are ordered around the perimeter).
	int top = 0;
	int bottom = 0;
	for (int i = 1; i < n; ++i)
	{
		if (verts[i].y < verts[top].y)
			top = i;
		if (verts[i].y > verts[bottom].y)
			bottom = i;
	}
	int scan_first = ClampI((int)ceilf(verts[top].y - 0.5f), 0, sw_height - 1);
	int scan_last = ClampI((int)ceilf(verts[bottom].y - 0.5f) - 1, 0, sw_height - 1);
	if (scan_first > scan_last)
	{
		if (face_index == sw_dbg_tag_face)
			sw_ri.Con_Printf(PRINT_ALL, "tagface abort scanrange bf=%u face=%d first=%d last=%d top=%d(%g) bottom=%d(%g)\n",
				sw_stats.bench_frames, face_index, scan_first, scan_last, top, verts[top].y, bottom, verts[bottom].y);
		return;
	}

	const qboolean stripe_checker = sw_scan2_active();
	int left_prev = top, left_cur = top;
	int right_prev = top, right_cur = top;
	float left_x = 0.0f, right_x = 0.0f, left_slope = 0.0f, right_slope = 0.0f;
	for (int y = scan_first; y <= scan_last; ++y)
	{
		++sw_stats.bench_world_spans;
		const float y_center = y + 0.5f - v0->y;
		const int old_left_prev = left_prev, old_right_prev = right_prev;
		while (left_cur != bottom && verts[left_cur].y <= (y + 0.5f))
		{
			left_prev = left_cur;
			left_cur = (left_cur + 1) % n;
		}
		while (right_cur != bottom && verts[right_cur].y <= (y + 0.5f))
		{
			right_prev = right_cur;
			right_cur = (right_cur + n - 1) % n;
		}
		if (left_cur == bottom && right_cur == bottom && verts[bottom].y < (y + 0.5f) - 1.0f)
			break;
		const screen_vertex_t *lp0 = &verts[left_prev], *lp1 = &verts[left_cur];
		const screen_vertex_t *rp0 = &verts[right_prev], *rp1 = &verts[right_cur];
		if (left_prev != old_left_prev || y == scan_first)
		{
			left_slope = (lp1->y > lp0->y) ? (lp1->x - lp0->x) / (lp1->y - lp0->y) : 0.0f;
			left_x = lp0->x + (lp1->y > lp0->y ? ((y + 0.5f) - lp0->y) * left_slope : 0.0f);
		}
		else
			left_x += left_slope;
		if (right_prev != old_right_prev || y == scan_first)
		{
			right_slope = (rp1->y > rp0->y) ? (rp1->x - rp0->x) / (rp1->y - rp0->y) : 0.0f;
			right_x = rp0->x + (rp1->y > rp0->y ? ((y + 0.5f) - rp0->y) * right_slope : 0.0f);
		}
		else
			right_x += right_slope;
		int xa = ClampI((int)ceilf(min(left_x, right_x) - 0.5f), 0, sw_width - 1);
		int xb = ClampI((int)ceilf(max(left_x, right_x) - 0.5f) - 1, 0, sw_width - 1);
		if (xa > xb)
		{
			if (face_index == sw_dbg_tag_face)
				sw_ri.Con_Printf(PRINT_ALL, "tagface emptyrow bf=%u face=%d y=%d lx=%g rx=%g lp=%d rp=%d lc=%d rc=%d\n",
					sw_stats.bench_frames, face_index, y, left_x, right_x, left_prev, right_prev, left_cur, right_cur);
			continue;
		}
		const float x_center = xa + 0.5f - v0->x;
		float uz = v0->s_over_z + gradient_uz_x * x_center + gradient_uz_y * y_center;
		float vz = v0->t_over_z + gradient_vz_x * x_center + gradient_vz_y * y_center;
		float w = v0->inverse_z + gradient_w_x * x_center + gradient_w_y * y_center;
		float lsz, ltz;
		if (light_alias)
		{
			lsz = uz;
			ltz = vz;
		}
		else
		{
			lsz = v0->light_s_over_z + gradient_lsz_x * x_center + gradient_lsz_y * y_center;
			ltz = v0->light_t_over_z + gradient_ltz_x * x_center + gradient_ltz_y * y_center;
		}

		uint16_t *const fb_row = sw_color + (size_t)y * sw_width;
		uint32_t *const zb_row = sw_depth + (size_t)y * sw_width;
		int x = xa;
		int remaining = xb - xa + 1;
		while (remaining > 0)
		{
			++sw_stats.bench_world_blocks;
			const int block = (x & (SW_WORLD_SPAN_STEP - 1)) ? min(remaining, SW_WORLD_SPAN_STEP - (x & (SW_WORLD_SPAN_STEP - 1))) : min(remaining, SW_WORLD_SPAN_STEP);
			if (w <= 0.0f)
			{
				if (face_index == sw_dbg_tag_face)
					sw_ri.Con_Printf(PRINT_ALL, "tagface abort w bf=%u face=%d y=%d x=%d w=%g paint=%d\n",
						sw_stats.bench_frames, face_index, y, x, w, sw_tag_face_paint_count);
				return;
			}
			const float w_end = w + gradient_w_x * block;
			const float iz0 = 1.0f / w;
			const float iz1 = 1.0f / max(w_end, 1e-20f);
			const float u0 = uz * iz0;
			const float v0b = vz * iz0;
			const float u1 = (uz + gradient_uz_x * block) * iz1;
			const float v1b = (vz + gradient_vz_x * block) * iz1;
			int iu = (int)(u0 * 65536.0f);
			int it = (int)(v0b * 65536.0f);
			const int iu_step = (int)((u1 - u0) * 65536.0f * SW_INV_WIDTH(block));
			const int it_step = (int)((v1b - v0b) * 65536.0f * SW_INV_WIDTH(block));
			// +1 cell bias into the band-packed light map => no per-pixel clamps
			int ils, ilt, ils_step, ilt_step;
			if (light_alias)
			{
				// identical ints to the ls0/ls1-chain form: ls0 == u0 bitwise.
				ils = iu - (light_min_s << 16) + (1 << 20);
				ilt = it - (light_min_t << 16) + (1 << 20);
				ils_step = iu_step;
				ilt_step = it_step;
			}
			else
			{
				const float ls0 = lsz * iz0;
				const float lt0 = ltz * iz0;
				const float ls1 = (lsz + gradient_lsz_x * block) * iz1;
				const float lt1 = (ltz + gradient_ltz_x * block) * iz1;
				ils = (int)(ls0 * 65536.0f) - (light_min_s << 16) + (1 << 20);
				ilt = (int)(lt0 * 65536.0f) - (light_min_t << 16) + (1 << 20);
				ils_step = (int)((ls1 - ls0) * 65536.0f * SW_INV_WIDTH(block));
				ilt_step = (int)((lt1 - lt0) * 65536.0f * SW_INV_WIDTH(block));
			}
			const int light_max_s_fixed = ((light_width + 2) << 20) - 1;
			const int light_max_t_fixed = ((light_height + 2) << 20) - 1;
			if (ils > light_max_s_fixed) ils = light_max_s_fixed;
			else if (ils < 0) ils = 0;
			if (ilt > light_max_t_fixed) ilt = light_max_t_fixed;
			else if (ilt < 0) ilt = 0;
			// All stored depths are 31-bit linear fixed-point inverse-z keys
			// (see SW_DEPTH_SCALE): positive float bits are monotonic but not
			// affine, so linearly stepping the bit pattern inverted depth order
			// against other spans mid-block. Block-exact endpoints, monotone
			// within a block; ties can flip only on exact key equality.
			uint32_t ikey = (uint32_t)(w * SW_DEPTH_SCALE);
			uint32_t ikey_end = (uint32_t)(max(w_end, 0.0f) * SW_DEPTH_SCALE);
			const int ikey_step = ((int)ikey_end - (int)ikey) / block;

			if (sw_world_ftb_mask_active && alpha >= 1.0f && twist_pow2 && theight_pow2)
			{
				uint32_t *const cw = sw_covermask[y];
				unsigned depth_writes = 0;
				int light_prev_idx = -1;
				int m8_light_prev_idx = -1;
				const uint16_t *shade_row = NULL;
				unsigned light_prev = 0;
				unsigned light_r = 256u, light_g = 256u, light_b = 256u;
				if (!sw_shade_ready)
					sw_shade_init();
				const byte *mul5r = sw_mul5shade[light_r], *mul5b = sw_mul5shade[light_b];
				const byte *mul6g = sw_mul6shade[light_g];
				int i = 0;
				while (i < block)
				{
					const int run = min(block - i, 32 - (x & 31));
					const uint32_t runmask = (run == 32) ? 0xFFFFFFFFu : (((1u << run) - 1u) << (x & 31));
					uint32_t *const word = &cw[x >> 5];
					const uint32_t covered = *word & runmask;
					if (covered == runmask)
					{
						sw_stats.bench_world_zreject += (unsigned)run; // the z-test would have rejected all of these
						iu += iu_step * run; it += it_step * run; ils += ils_step * run; ilt += ilt_step * run;
						ikey += (uint32_t)(ikey_step * run);
						x += run;
						i += run;
						continue;
					}
					const uint32_t per_px_mask = (covered == 0) ? 0u : covered;
					uint32_t outmask = 0;
					if (covered == 0 && !stripe_checker && !alpha_test &&
						(sw_dbg_tag_y != y || sw_dbg_tag_x < x || sw_dbg_tag_x >= x + run))
					{
						if (image->src8 != NULL)
						{
							sw_world_shade_image = image;
							sw_world_paint_ctx = (sw_world_paint_ctx_t){
								fb_row + x, zb_row + x, (const uint16_t *)image->src8, light_cache,
								&sw_mul5shade[0][0], &sw_mul6shade[0][0], (const byte *)shade_row, mul6g, mul5b,
								image->width, mask_w, mask_h, light_stride, run,
								iu, it, ils, ilt, iu_step, it_step, ils_step, ilt_step,
								ikey, ikey_step, m8_light_prev_idx, 0
							};
							sw_world_paint_m8_packed_pmmx();
							m8_light_prev_idx = sw_world_paint_ctx.light_prev_idx;
							shade_row = (const uint16_t *)sw_world_paint_ctx.mul5r;
						}
						else
						{
							sw_world_paint_ctx = (sw_world_paint_ctx_t){
								fb_row + x, zb_row + x, image->pixels, light_cache,
								&sw_mul5shade[0][0], &sw_mul6shade[0][0], mul5r, mul6g, mul5b,
								image->width, mask_w, mask_h, light_stride, run,
								iu, it, ils, ilt, iu_step, it_step, ils_step, ilt_step,
								ikey, ikey_step, light_prev_idx, 0
							};
							sw_world_paint_run_pmmx();
							light_prev_idx = sw_world_paint_ctx.light_prev_idx;
							mul5r = sw_world_paint_ctx.mul5r; mul6g = sw_world_paint_ctx.mul6g;
							mul5b = sw_world_paint_ctx.mul5b;
						}
						iu = sw_world_paint_ctx.iu; it = sw_world_paint_ctx.it;
						ils = sw_world_paint_ctx.ils; ilt = sw_world_paint_ctx.ilt;
						ikey = sw_world_paint_ctx.ikey;
						sw_stats.bench_mag_world += sw_world_paint_ctx.mag;
						sw_tag_face_paint_last = face_index;
						sw_tag_face_paint_count += run;
						depth_writes += (unsigned)run;
						sw_stats.bench_world_zaccepted += (unsigned)run;
						*word |= runmask;
						x += run;
						i += run;
						continue;
					}
					for (int k = 0; k < run; ++k, ++i, ++x)
					{
						const size_t idx = (size_t)x;
						// per-px coverage bit: in a mixed word, only empty bits paint
						const uint32_t bit = 1u << (x & 31);
						if ((per_px_mask & bit) == 0 && (!stripe_checker || ((x >> 1) & 1) == sw_scan_phase))
						{
							const uint16_t texel = image->pixels[((it >> 16) & mask_h) * image->width + ((iu >> 16) & mask_w)];
							// Alpha-test faces (SURF_ALPHA_TEXTURE): texel 0x0000 is the
							// colorkey. Writing it produced a black occluder that also
							// stomped the z-buffer behind genuine geometry.
							if (!alpha_test || texel != 0)
							{
							const int lidx = (int)((unsigned)ilt >> 20) * light_stride + (int)((unsigned)ils >> 20);
							if (lidx != light_prev_idx)
							{
								light_prev_idx = lidx;
								light_prev = light_cache[lidx];
								light_r = (light_prev >> 20) & 1023u;
								light_g = (light_prev >> 10) & 1023u;
								light_b = light_prev & 1023u;
								mul5r = sw_mul5shade[light_r];
								mul6g = sw_mul6shade[light_g];
								mul5b = sw_mul5shade[light_b];
							}
								const uint16_t wpx = (uint16_t)((mul5r[(texel >> 11) & 31u] << 11) |
									(mul6g[(texel >> 5) & 63u] << 5) | mul5b[texel & 31u]);
								sw_stats.bench_mag_world += (wpx == 0xF81Fu);
								sw_tag_face_paint_last = face_index;
								++sw_tag_face_paint_count;
								if (sw_dbg_tag_x == x && sw_dbg_tag_y == y)
									sw_ri.Con_Printf(PRINT_ALL, "tagxy worldspan bf=%u face=%d texel=%04x out=%04x key=%u zb=%u light=%08x\n",
										sw_stats.bench_frames, face_index, texel, wpx, ikey, zb_row[idx], light_prev);
								fb_row[idx] = wpx;
								zb_row[idx] = ikey;
								++depth_writes;
								++sw_stats.bench_world_zaccepted;
								outmask |= bit;
							}
						}
						else
							++sw_stats.bench_world_zreject;
						iu += iu_step; it += it_step; ils += ils_step; ilt += ilt_step; ikey += (uint32_t)ikey_step;
					}
					*word |= outmask;
				}
				sw_stats.depth_writes += depth_writes;
				sw_stats.bench_pixels_world += (unsigned)block;
			}
			else
			{
				const qboolean use_z_kernel = sw_brush_span_active && alpha >= 1.0f &&
					twist_pow2 && theight_pow2 && !stripe_checker && !alpha_test &&
					(sw_dbg_tag_y != y || sw_dbg_tag_x < x || sw_dbg_tag_x >= x + block);
				if (use_z_kernel)
				{
					if (!sw_shade_ready)
						sw_shade_init();
					sw_world_paint_ctx = (sw_world_paint_ctx_t){
						fb_row + x, zb_row + x, image->pixels, light_cache,
						&sw_mul5shade[0][0], &sw_mul6shade[0][0],
						sw_mul5shade[256], sw_mul6shade[256], sw_mul5shade[256],
						image->width, mask_w, mask_h, light_stride, block,
						iu, it, ils, ilt, iu_step, it_step, ils_step, ilt_step,
						ikey, ikey_step, -1, 0, 0
					};
					sw_world_paint_zrun_pmmx();
					iu = sw_world_paint_ctx.iu; it = sw_world_paint_ctx.it;
					ils = sw_world_paint_ctx.ils; ilt = sw_world_paint_ctx.ilt;
					ikey = sw_world_paint_ctx.ikey;
					sw_stats.bench_mag_world_alpha += sw_world_paint_ctx.mag;
					sw_stats.depth_writes += sw_world_paint_ctx.accepted;
					sw_stats.bench_world_zaccepted += sw_world_paint_ctx.accepted;
					sw_stats.bench_pixels_world += (unsigned)block;
					sw_tag_face_paint_last = face_index;
					sw_tag_face_paint_count += (int)sw_world_paint_ctx.accepted;
					x += block;
				}
				else
				{
					// Preserves the old (255 * alpha_fixed) >> 8 source-alpha scale.
					const int sa_base = (int)((255u * (unsigned)(int)(alpha * 256.0f + 0.5f)) >> 8);
					for (int i = 0; i < block; ++i, ++x)
				{
					const size_t idx = (size_t)x;
					int tx, ty;
					if (twist_pow2 && theight_pow2)
					{
						tx = (iu >> 16) & mask_w;
						ty = (it >> 16) & mask_h;
					}
					else
					{
						const float fu = (float)iu / 65536.0f;
						const float fv = (float)it / 65536.0f;
						tx = ((int)floorf(fu) % image->width + image->width) % image->width;
						ty = ((int)floorf(fv) % image->height + image->height) % image->height;
					}
					const uint16_t texel = image->pixels[ty * image->width + tx];
					const uint16_t lit = span_modulate(texel,
						light_cache[(int)((unsigned)ilt >> 20) * light_stride + (int)((unsigned)ils >> 20)]);
					const int sa = lit != 0 ? sa_base : 0;
					if (sa != 0 && ikey >= zb_row[idx] && (!stripe_checker || ((x >> 1) & 1) == sw_scan_phase))
					{
						const uint16_t dst = fb_row[idx];
						const int dw = 255 - sa;
						const unsigned lr8 = (lit >> 8) & 0xF8u;
						const unsigned lg8 = (lit >> 3) & 0xFCu;
						const unsigned lb8 = (lit << 3) & 0xF8u;
						const unsigned dr8 = (dst >> 8) & 0xF8u;
						const unsigned dg8 = (dst >> 3) & 0xFCu;
						const unsigned db8 = (dst << 3) & 0xF8u;
						const unsigned r = min(255u, (lr8 * (unsigned)sa + dr8 * (unsigned)dw) / 255u);
						const unsigned g = min(255u, (lg8 * (unsigned)sa + dg8 * (unsigned)dw) / 255u);
						const unsigned bb = min(255u, (lb8 * (unsigned)sa + db8 * (unsigned)dw) / 255u);
						const uint16_t apx = SW_RGB565(r, g, bb);
						sw_stats.bench_mag_world_alpha += (apx == 0xF81Fu);
						sw_tag_face_paint_last = face_index;
						++sw_tag_face_paint_count;
						fb_row[idx] = apx;
						if (sa == 255 && alpha >= 1.0f)
						{
							zb_row[idx] = ikey;
							++sw_stats.depth_writes;
							++sw_stats.bench_world_zaccepted;
						}
					}
					++sw_stats.bench_pixels_world;
						iu += iu_step; it += it_step; ils += ils_step; ilt += ilt_step; ikey += (uint32_t)ikey_step;
					}
				}
			}
			(void)gradient_w_x;
			uz += gradient_uz_x * block;
			vz += gradient_vz_x * block;
			if (!light_alias)
			{
				lsz += gradient_lsz_x * block;
				ltz += gradient_ltz_x * block;
			}
			w = w_end;
			remaining -= block;
		}
		sw_stats.bench_walked_world += (unsigned)(xb - xa + 1);
	}
	++sw_stats.triangles;
}

static void raster_triangle(const screen_vertex_t a, const screen_vertex_t b, const screen_vertex_t c,
	const sw_image_t *image, const sw_bsp_t *bsp, const dface_t *face,
	const float texture_min_s, const float texture_min_t, const int light_width, const int light_height,
	const float alpha)
{
	if (!screen_vertex_valid(a) || !screen_vertex_valid(b) || !screen_vertex_valid(c))
		return;
	const float area = edge(a, b, c.x, c.y);
	if (!isfinite(area) || fabsf(area) < 0.0001f || image == NULL)
		return;
	const float inverse_area = 1.0f / area;
	const int min_x = ClampI((int)floorf(min(a.x, min(b.x, c.x))), 0, sw_width - 1);
	const int max_x = ClampI((int)ceilf(max(a.x, max(b.x, c.x))), 0, sw_width - 1);
	const int min_y = ClampI((int)floorf(min(a.y, min(b.y, c.y))), 0, sw_height - 1);
	const int max_y = ClampI((int)ceilf(max(a.y, max(b.y, c.y))), 0, sw_height - 1);
	sw_stats.bench_walked_world += (unsigned)((max_x - min_x + 1) * (max_y - min_y + 1));
	for (int y = min_y; y <= max_y; ++y)
	{
		for (int x = min_x; x <= max_x; ++x)
		{
			const float px = x + 0.5f;
			const float py = y + 0.5f;
			const float wa = edge(b, c, px, py) * inverse_area;
			const float wb = edge(c, a, px, py) * inverse_area;
			const float wc = edge(a, b, px, py) * inverse_area;
			if (wa < 0.0f || wb < 0.0f || wc < 0.0f)
				continue;
			++sw_stats.bench_pixels_world;
			const float inverse_z = wa * a.inverse_z + wb * b.inverse_z + wc * c.inverse_z;
			if (inverse_z <= 0.0f)
				continue;
			const float z = 1.0f / inverse_z;
			const float s = (wa * a.s_over_z + wb * b.s_over_z + wc * c.s_over_z) * z;
			const float t = (wa * a.t_over_z + wb * b.t_over_z + wc * c.t_over_z) * z;
			const float light_s = (wa * a.light_s_over_z + wb * b.light_s_over_z + wc * c.light_s_over_z) * z;
			const float light_t = (wa * a.light_t_over_z + wb * b.light_t_over_z + wc * c.light_t_over_z) * z;
			const int tx = ((int)floorf(s) % image->width + image->width) % image->width;
			const int ty = ((int)floorf(t) % image->height + image->height) % image->height;
			float red, green, blue;
			lightmap_color(bsp, face, light_s, light_t, texture_min_s, texture_min_t, light_width, light_height, &red, &green, &blue);
			byte brush_alpha;
			const uint16_t color = modulate(image->pixels[ty * image->width + tx], red, green, blue, alpha, &brush_alpha);
			SW_PutPixel(x, y, inverse_z, color, brush_alpha, alpha >= 1.0f && brush_alpha == 255, false);
		}
	}
	++sw_stats.triangles;
}

static sw_image_t *texture_animation(const sw_bsp_t *bsp, const int root,
	const int entity_frame, int *frame_count)
{
	if (bsp->texinfo_images[root] == NULL)
	{
		*frame_count = 1;
		return NULL;
	}
	int count = 1;
	int current = root;
	while (count < bsp->num_texinfo)
	{
		const int next = LittleLong(bsp->texinfo[current].nexttexinfo);
		if (next == -1 || next == root)
			break;
		current = next;
		++count;
	}
	*frame_count = count;
	int frame = entity_frame;
	if (LittleLong(bsp->texinfo[root].flags) & SURF_ANIMSPEED)
		frame = (int)(sw_refdef.time * bsp->texinfo_images[root]->num_frames);
	frame = ((frame % count) + count) % count;
	current = root;
	for (int i = 0; i < frame; ++i)
	{
		const int next = LittleLong(bsp->texinfo[current].nexttexinfo);
		if (next == -1 || next == root)
			break;
		current = next;
	}
	return bsp->texinfo_images[current];
}

static float quake_surface_amount(void)
{
	static cvar_t *quake_amount;
	if (quake_amount == NULL)
		quake_amount = sw_ri.Cvar_Get("quake_amount", "0", 0);
	if (quake_amount == NULL || !isfinite(quake_amount->value))
		return 0.0f;
	return Clamp(quake_amount->value, -1000.0f, 1000.0f) * 0.05f;
}

// Translucent world faces (world and inline models) deferred from draw_face
// until after sky/entities; bounds-checked, overflow draws immediately.
typedef struct sw_deferred_face_s {
	const sw_bsp_t *bsp;
	int face_index;
	vec3_t origin;
	int entity_frame;
} sw_deferred_face_t;
#define SW_MAX_DEFERRED_FACES 1024
static sw_deferred_face_t sw_deferred_faces[SW_MAX_DEFERRED_FACES];
static int sw_deferred_face_count;
static qboolean sw_deferral_bypass;

static void sw_tag_report_face(const screen_vertex_t *verts, const int n, const sw_image_t *image,
	const int face_index, const int flags)
{
	if (sw_dbg_tag_x < 0 || sw_dbg_tag_y < 0)
		return;
	const float px = sw_dbg_tag_x + 0.5f, py = sw_dbg_tag_y + 0.5f;
	qboolean neg = false, pos = false;
	for (int i = 0; i < n; ++i)
	{
		const float e = edge(verts[i], verts[(i + 1) % n], px, py);
		neg |= e < 0.0f;
		pos |= e > 0.0f;
	}
	if (neg && pos)
	{
		// Convex test failed; still report faces whose bbox contains the pixel
		// (degenerate/non-convex fan triangles can paint it anyway).
		float minx = verts[0].x, maxx = verts[0].x, miny = verts[0].y, maxy = verts[0].y;
		qboolean wild = false;
		for (int i = 1; i < n; ++i)
		{
			minx = min(minx, verts[i].x); maxx = max(maxx, verts[i].x);
			miny = min(miny, verts[i].y); maxy = max(maxy, verts[i].y);
		}
		for (int i = 0; i < n; ++i)
			wild |= !isfinite(verts[i].x) || !isfinite(verts[i].y) || !isfinite(verts[i].inverse_z) ||
				fabsf(verts[i].x) > 1e6f || fabsf(verts[i].y) > 1e6f || verts[i].inverse_z > 0.02f;
		if (wild || (px >= minx && px <= maxx && py >= miny && py <= maxy))
			sw_ri.Con_Printf(PRINT_ALL, "tagxy worldface-bbox bf=%u face=%d tex=%s n=%d wild=%d w=(%g %g %g) v0=(%g,%g) v1=(%g,%g) v2=(%g,%g)\n",
				sw_stats.bench_frames, face_index, image != NULL ? image->name : "?", n, (int)wild,
				verts[0].inverse_z, verts[1].inverse_z, verts[2].inverse_z,
				verts[0].x, verts[0].y, verts[1].x, verts[1].y, verts[2].x, verts[2].y);
		return;
	}
	sw_ri.Con_Printf(PRINT_ALL, "tagxy worldface bf=%u face=%d tex=%s flags=%x w0=%g w1=%g w2=%g\n",
		sw_stats.bench_frames, face_index, image != NULL ? image->name : "?", flags,
		verts[0].inverse_z, verts[1].inverse_z, verts[2].inverse_z);
}

static void draw_face(const sw_bsp_t *bsp, const int face_index, const vec3_t origin, const int entity_frame)
{
	if (face_index < 0 || face_index >= bsp->num_faces)
		return;
	sw_dbg_tag_src = face_index;
	++sw_stats.bench_world_faces;
	const dface_t *face = &bsp->faces[face_index];
	const int first_edge = LittleLong(face->firstedge);
	const int edge_count = LittleShort(face->numedges);
	const int texinfo_index = LittleShort(face->texinfo);
	if (edge_count < 3 || edge_count > SW_MAX_FACE_VERTS || first_edge < 0 ||
		first_edge > bsp->num_surfedges - edge_count || texinfo_index < 0 || texinfo_index >= bsp->num_texinfo)
		return;
	const texinfo_t *texture = &bsp->texinfo[texinfo_index];
	const int flags = LittleLong(texture->flags);
	if (flags & (SURF_NODRAW | SURF_SKY))
		return;
	// Translucent world faces blend but write no depth, so they must render
	// LAST (after sky and entities); in the BSP walk they would paint over
	// whatever shows through them. Defer them to the post-entity flush.
	if (!sw_deferral_bypass && (flags & (SURF_TRANS33 | SURF_TRANS66)) &&
		sw_deferred_face_count < SW_MAX_DEFERRED_FACES)
	{
		sw_deferred_face_t *entry = &sw_deferred_faces[sw_deferred_face_count++];
		entry->bsp = bsp;
		entry->face_index = face_index;
		VectorCopy(origin, entry->origin);
		entry->entity_frame = entity_frame;
		return;
	}
	if ((flags & (SURF_TRANS33 | SURF_TRANS66 | SURF_ALPHA_TEXTURE | SURF_WARP | SURF_UNDULATE)) == 0)
	{
		// Opaque, non-warped faces are single-sided (GL culls with gl_cull=1):
		// a viewer strictly behind the plane sees nothing. Translucent and
		// wobbling surfaces stay two-sided: visible from the back half-space.
		const dplane_t *plane = &bsp->planes[little_ushort(face->planenum)];
		float nx = LittleFloat(plane->normal[0]);
		float ny = LittleFloat(plane->normal[1]);
		float nz = LittleFloat(plane->normal[2]);
		float pdist = LittleFloat(plane->dist);
		if (LittleShort(face->side))
		{
			nx = -nx; ny = -ny; nz = -nz; pdist = -pdist;
		}
		const float pos = nx * (sw_refdef.vieworg[0] - origin[0]) +
			ny * (sw_refdef.vieworg[1] - origin[1]) +
			nz * (sw_refdef.vieworg[2] - origin[2]) - pdist;
		if (pos < -0.01f)
			return;
	}
	int animation_frames;
	const sw_image_t *image = texture_animation(bsp, texinfo_index, entity_frame, &animation_frames);
	if (image == NULL)
		return;
	const float quake_amount = (flags & SURF_QUAKE) ? quake_surface_amount() : 0.0f;
	const float flowing_scroll = (flags & SURF_FLOWING) ?
		-64.0f * (sw_refdef.time * 0.5f - floorf(sw_refdef.time * 0.5f)) : 0.0f;
	sw_vertex_t view_vertices[SW_MAX_FACE_VERTS];
	int light_width;
	int light_height;
	float minimum_s = FLT_MAX;
	float minimum_t = FLT_MAX;
	float maximum_s = -FLT_MAX;
	float maximum_t = -FLT_MAX;
	const sw_face_packet_t *packet = bsp->packets != NULL ? &bsp->packets[face_index] : NULL;
	if (packet == NULL || packet->verts == NULL || packet->num_verts != edge_count)
	{
	for (int i = 0; i < edge_count; ++i)
	{
		const int surface_edge = LittleLong(bsp->surfedges[first_edge + i]);
		if (surface_edge == INT_MIN)
			return;
		const int edge_index = abs(surface_edge);
		if (edge_index >= bsp->num_edges)
			return;
		const int vertex_index = little_ushort(bsp->edges[edge_index].v[surface_edge >= 0 ? 0 : 1]);
		if (vertex_index < 0 || vertex_index >= bsp->num_vertices)
			return;
		vec3_t local;
		vec3_t point;
		for (int axis = 0; axis < 3; ++axis)
		{
			local[axis] = LittleFloat(bsp->vertices[vertex_index].point[axis]);
			point[axis] = local[axis] + origin[axis];
			if (!isfinite(point[axis]))
				return;
		}
		// Texture and lightmap coordinates belong to the model's local BSP
		// geometry; translating a brush model must move its position only, not
		// swim its texture or shift its quantized lightmap minima.
		const float base_s = local[0] * LittleFloat(texture->vecs[0][0]) +
			local[1] * LittleFloat(texture->vecs[0][1]) + local[2] * LittleFloat(texture->vecs[0][2]) +
			LittleFloat(texture->vecs[0][3]);
		const float base_t = local[0] * LittleFloat(texture->vecs[1][0]) +
			local[1] * LittleFloat(texture->vecs[1][1]) + local[2] * LittleFloat(texture->vecs[1][2]) +
			LittleFloat(texture->vecs[1][3]);
		if (!isfinite(base_s) || !isfinite(base_t))
			return;
		if (flags & SURF_UNDULATE)
			point[2] += turbsin[TURBSIN_V0(local[0], local[1], sw_refdef.time)] * 0.25f +
				turbsin[TURBSIN_V1(local[0], local[1], sw_refdef.time)] * 0.125f;
		if (quake_amount != 0.0f)
			point[2] += turbsin[TURBSIN_V0(local[0], local[1], sw_refdef.time)] * quake_amount * 0.5f +
				turbsin[TURBSIN_V1(local[0], local[1], sw_refdef.time)] * quake_amount * 0.25f;
		float s = base_s + flowing_scroll;
		float t = base_t;
		if (flags & SURF_WARP)
		{
			s = (base_s + turbsin[(int)((base_t * 0.125f + sw_refdef.time) * TURBSCALE) & 255] +
				flowing_scroll) * image->width / 64.0f;
			t = (base_t + turbsin[(int)((base_s * 0.125f + sw_refdef.time) * TURBSCALE) & 255]) *
				image->height / 64.0f;
		}
		minimum_s = min(minimum_s, base_s);
		minimum_t = min(minimum_t, base_t);
		maximum_s = max(maximum_s, base_s);
		maximum_t = max(maximum_t, base_t);
		view_vertices[i] = world_to_view(point, s, t);
		view_vertices[i].light_s = base_s;
		view_vertices[i].light_t = base_t;
	}
	minimum_s = floorf(minimum_s / 16.0f) * 16.0f;
	minimum_t = floorf(minimum_t / 16.0f) * 16.0f;
	{
		const float light_width_value = (ceilf(maximum_s / 16.0f) * 16.0f - minimum_s) / 16.0f + 1.0f;
		const float light_height_value = (ceilf(maximum_t / 16.0f) * 16.0f - minimum_t) / 16.0f + 1.0f;
		if (!isfinite(light_width_value) || !isfinite(light_height_value) ||
			light_width_value < 1.0f || light_height_value < 1.0f ||
			light_width_value > 4096.0f || light_height_value > 4096.0f)
			return;
		light_width = (int)light_width_value;
		light_height = (int)light_height_value;
	}
	}
	else // E58: baked render packet available
	{
		for (int i = 0; i < edge_count; ++i)
		{
			const float *pv = &packet->verts[i * 5];
			vec3_t point;
			for (int axis = 0; axis < 3; ++axis)
			{
				point[axis] = pv[axis] + origin[axis];
				if (!isfinite(point[axis]))
					return;
			}
			const float base_s = pv[3];
			const float base_t = pv[4];
			if (flags & SURF_UNDULATE)
				point[2] += turbsin[TURBSIN_V0(pv[0], pv[1], sw_refdef.time)] * 0.25f +
				turbsin[TURBSIN_V1(pv[0], pv[1], sw_refdef.time)] * 0.125f;
			if (quake_amount != 0.0f)
				point[2] += turbsin[TURBSIN_V0(pv[0], pv[1], sw_refdef.time)] * quake_amount * 0.5f +
				turbsin[TURBSIN_V1(pv[0], pv[1], sw_refdef.time)] * quake_amount * 0.25f;
			float s = base_s + flowing_scroll;
			float t = base_t;
			if (flags & SURF_WARP)
			{
				s = (base_s + turbsin[(int)((base_t * 0.125f + sw_refdef.time) * TURBSCALE) & 255] +
					flowing_scroll) * image->width / 64.0f;
				t = (base_t + turbsin[(int)((base_s * 0.125f + sw_refdef.time) * TURBSCALE) & 255]) *
					image->height / 64.0f;
			}
			view_vertices[i] = world_to_view(point, s, t);
			view_vertices[i].light_s = base_s;
			view_vertices[i].light_t = base_t;
		}
		minimum_s = packet->light_min_s;
		minimum_t = packet->light_min_t;
		light_width = packet->light_width;
		light_height = packet->light_height;
	}
	sw_vertex_t clipped[SW_MAX_FACE_VERTS + 2];
	const int clipped_count = clip_near(view_vertices, edge_count, clipped);
	if (clipped_count < 3)
		return;
	float alpha = 1.0f;
	if (flags & SURF_TRANS33)
		alpha = 0.33f;
	else if (flags & SURF_TRANS66)
		alpha = 0.66f;
	const dface_t *lighting_face = (flags & SURF_FULLBRIGHT) ? NULL : face;
	static cvar_t *sw_spans;
	if (sw_spans == NULL)
		sw_spans = sw_ri.Cvar_Get("r_swspans", "1", 0);
	screen_vertex_t projected[SW_MAX_FACE_VERTS + 2];
	for (int i = 0; i < clipped_count; ++i)
		projected[i] = project(clipped[i]);
	sw_tag_report_face(projected, clipped_count, image, face_index, flags);
	const qboolean tag_this_face = (face_index == sw_dbg_tag_face);
	if (sw_spans != NULL && sw_spans->value != 0.0f && !sw_dbg_nospan &&
		light_width <= SW_LIGHT_CACHE_MAX && light_height <= SW_LIGHT_CACHE_MAX)
		draw_face_spans(projected, clipped_count, image, bsp, lighting_face, minimum_s,
			minimum_t, light_width, light_height, alpha, face_index,
			(flags & SURF_ALPHA_TEXTURE) != 0);
	else
	{
		sw_tag_face_paint_count = 0;
		for (int i = 1; i + 1 < clipped_count; ++i)
		{
			if (sw_dbg_tag_x >= 0 && sw_dbg_tag_y >= 0)
			{
				const float px = sw_dbg_tag_x + 0.5f, py = sw_dbg_tag_y + 0.5f;
				const screen_vertex_t a = projected[0], b = projected[i], c = projected[i + 1];
				const float e0 = edge(a, b, px, py), e1 = edge(b, c, px, py), e2 = edge(c, a, px, py);
				if (!((e0 < 0.0f || e1 < 0.0f || e2 < 0.0f) && (e0 > 0.0f || e1 > 0.0f || e2 > 0.0f)))
					sw_ri.Con_Printf(PRINT_ALL, "tagxy worldtri bf=%u face=%d tri=%d v0=(%g,%g,w=%g) v1=(%g,%g,w=%g) v2=(%g,%g,w=%g)\n",
						sw_stats.bench_frames, face_index, i, a.x, a.y, a.inverse_z, b.x, b.y, b.inverse_z, c.x, c.y, c.inverse_z);
			}
			raster_triangle(projected[0], projected[i], projected[i + 1], image,
				bsp, lighting_face, minimum_s, minimum_t, light_width, light_height, alpha);
		}
		sw_tag_face_paint_count = -1; // naive path: paints via SW_PutPixel (see tagxy lines)
	}
	if (tag_this_face)
		sw_ri.Con_Printf(PRINT_ALL, "tagface bf=%u face=%d painted=%d\n",
			sw_stats.bench_frames, face_index, sw_tag_face_paint_count);
	if (animation_frames > 1) ++sw_stats.animated_faces;
	if (flags & SURF_FLOWING) ++sw_stats.flowing_faces;
	if (flags & SURF_WARP) ++sw_stats.turbulent_faces;
	if (flags & SURF_UNDULATE) ++sw_stats.undulate_faces;
	if (quake_amount != 0.0f) ++sw_stats.quake_faces;
	++sw_stats.world_faces;
}

static int point_leaf(const sw_bsp_t *bsp, const vec3_t point)
{
	int node_index = 0;
	int steps = 0;
	while (node_index >= 0)
	{
		if (node_index >= bsp->num_nodes || ++steps > bsp->num_nodes)
			return 0;
		const dnode_t *node = &bsp->nodes[node_index];
		const int plane_index = LittleLong(node->planenum);
		if (plane_index < 0 || plane_index >= bsp->num_planes)
			return 0;
		const dplane_t *plane = &bsp->planes[plane_index];
		const float distance = point[0] * LittleFloat(plane->normal[0]) + point[1] * LittleFloat(plane->normal[1]) +
			point[2] * LittleFloat(plane->normal[2]) - LittleFloat(plane->dist);
		node_index = LittleLong(node->children[distance >= 0.0f ? 0 : 1]);
	}
	return -1 - node_index;
}

static byte sw_pvs_row[(MAX_MAP_LEAFS + 7) / 8];
static byte sw_visible_faces[MAX_MAP_FACES];

static qboolean decompress_pvs_row(const sw_bsp_t *bsp, const int view_cluster,
	byte *output, int *clusters_out)
{
	if (view_cluster < 0 || bsp->visibility_length < 4)
		return false;
	const int clusters = read_little_int(bsp->visibility);
	if (clusters <= 0 || clusters > (bsp->visibility_length - 4) / 8 ||
		clusters > MAX_MAP_LEAFS || view_cluster >= clusters)
		return false;
	int offset = read_little_int(bsp->visibility + 4 + view_cluster * 8);
	if (offset < 0 || offset >= bsp->visibility_length)
		return false;
	int written = 0;
	const int row = (clusters + 7) / 8;
	while (written < row && offset < bsp->visibility_length)
	{
		const byte value = bsp->visibility[offset++];
		if (value != 0)
			output[written++] = value;
		else
		{
			if (offset >= bsp->visibility_length)
				return false;
			const int run = bsp->visibility[offset++];
			if (run <= 0 || run > row - written)
				return false;
			memset(output + written, 0, (size_t)run);
			written += run;
		}
	}
	if (written != row)
		return false;
	*clusters_out = clusters;
	return true;
}

static void rotate_sky_direction(const vec3_t input, vec3_t output)
{
	vec3_t axis = { sw_sky_axis[0], sw_sky_axis[1], sw_sky_axis[2] };
	const float length_squared = dot3(axis, axis);
	if (sw_sky_rotate == 0.0f || !isfinite(length_squared) || length_squared < 0.000001f)
	{
		VectorCopy(input, output);
		return;
	}
	const float inverse_length = 1.0f / sqrtf(length_squared);
	VectorScale(axis, inverse_length, axis);
	const float angle = sw_refdef.time * sw_sky_rotate * (float)M_PI / 180.0f;
	const float cosine = cosf(angle);
	const float sine = -sinf(angle);
	vec3_t cross;
	CrossProduct(axis, input, cross);
	const float along_axis = dot3(axis, input) * (1.0f - cosine);
	for (int component = 0; component < 3; ++component)
		output[component] = input[component] * cosine + cross[component] * sine + axis[component] * along_axis;
}

static int sky_coordinates(const vec3_t direction, float *s, float *t)
{
	const float x = direction[0];
	const float y = direction[1];
	const float z = direction[2];
	const float ax = fabsf(x);
	const float ay = fabsf(y);
	const float az = fabsf(z);
	float cube_s;
	float cube_t;
	float major;
	int face;
	if (ax >= ay && ax >= az)
	{
		major = ax;
		if (x >= 0.0f) { face = 0; cube_s = -y / major; cube_t = z / major; }
		else { face = 2; cube_s = y / major; cube_t = z / major; }
	}
	else if (ay >= az)
	{
		major = ay;
		if (y >= 0.0f) { face = 1; cube_s = x / major; cube_t = z / major; }
		else { face = 3; cube_s = -x / major; cube_t = z / major; }
	}
	else
	{
		major = az;
		if (z >= 0.0f) { face = 4; cube_s = -y / major; cube_t = -x / major; }
		else { face = 5; cube_s = -y / major; cube_t = x / major; }
	}
	*s = Clamp((cube_s + 1.0f) * 0.5f, 0.0f, 1.0f);
	*t = Clamp(1.0f - (cube_t + 1.0f) * 0.5f, 0.0f, 1.0f);
	return face;
}

typedef struct sky_cache_key_s {
	int framebuffer_width;
	int framebuffer_height;
	int x;
	int y;
	int width;
	int height;
	float fov_x;
	float fov_y;
	vec3_t viewangles;
	float rotation_angle;
	unsigned generation;
	qboolean valid;
} sky_cache_key_t;

static uint16_t *sky_cache;
static size_t sky_cache_pixels;
static sky_cache_key_t sky_cache_key;
static unsigned sky_cache_face_mask;

void SW_SkyCacheShutdown(void)
{
	free(sky_cache);
	sky_cache = NULL;
	sky_cache_pixels = 0;
	memset(&sky_cache_key, 0, sizeof(sky_cache_key));
	sky_cache_face_mask = 0;
}

static float sw_canon_angle(const float a)
{
	float c = fmodf(a, 360.0f);
	if (c < 0.0f)
		c += 360.0f;
	return c;
}

static qboolean sky_cache_matches(const refdef_t *fd, const float rotation_angle)
{
	return sky_cache_key.valid && sky_cache_key.generation == sw_sky_generation &&
		sky_cache_key.framebuffer_width == sw_width && sky_cache_key.framebuffer_height == sw_height &&
		sky_cache_key.x == fd->x && sky_cache_key.y == fd->y &&
		sky_cache_key.width == fd->width && sky_cache_key.height == fd->height &&
		sky_cache_key.fov_x == fd->fov_x && sky_cache_key.fov_y == fd->fov_y &&
		sky_cache_key.viewangles[0] == sw_canon_angle(fd->viewangles[0]) &&
		sky_cache_key.viewangles[1] == sw_canon_angle(fd->viewangles[1]) &&
		sky_cache_key.viewangles[2] == sw_canon_angle(fd->viewangles[2]) &&
		sky_cache_key.rotation_angle == rotation_angle;
}

void SW_RasterSky(const refdef_t *fd)
{
	sw_init_inv_width();
	if ((fd->rdflags & RDF_NOWORLDMODEL) || sw_sky_images[0] == NULL)
		return;
	for (int face = 0; face < 6; ++face)
		if (sw_sky_images[face] == NULL)
			return;
	const int minimum_x = ClampI(fd->x, 0, sw_width);
	const int minimum_y = ClampI(fd->y, 0, sw_height);
	const int maximum_x = (int)min((long long)sw_width, (long long)fd->x + fd->width);
	const int maximum_y = (int)min((long long)sw_height, (long long)fd->y + fd->height);
	if (sw_width <= 0 || sw_height <= 0 || (size_t)sw_width > SIZE_MAX / (size_t)sw_height ||
		(size_t)sw_width * (size_t)sw_height > SIZE_MAX / sizeof(*sky_cache))
		return;
	const size_t required_pixels = (size_t)sw_width * (size_t)sw_height;
	if (required_pixels > sky_cache_pixels)
	{
		uint16_t *replacement = realloc(sky_cache, required_pixels * sizeof(*replacement));
		if (replacement != NULL)
		{
			sky_cache = replacement;
			sky_cache_pixels = required_pixels;
			sky_cache_key.valid = false;
		}
	}
	const float rotation_angle = sw_sky_rotate == 0.0f ? 0.0f : sw_refdef.time * sw_sky_rotate;
	const qboolean rotating = sw_sky_rotate != 0.0f;
	const qboolean sky_checker2 = sw_scan2_active();
	const size_t drawn_pixels = (size_t)max(0, maximum_x - minimum_x) * (size_t)max(0, maximum_y - minimum_y);
	if (!rotating && sky_cache != NULL && sky_cache_matches(fd, rotation_angle))
	{
		++sw_stats.sky_cache_hits;
		if (sw_dbg_skydiag)
			sw_ri.Con_Printf(PRINT_ALL, "sky: hit rframe=%u mask=%02x angles=%.4f %.4f %.4f holes=%u\n",
				sw_stats.bench_frames, sky_cache_face_mask, fd->viewangles[0], fd->viewangles[1],
				fd->viewangles[2], sw_stats.bench_holes_topband);
		for (int y = minimum_y; y < maximum_y; ++y)
		{
			const uint32_t *zb_row = sw_depth + (size_t)y * sw_width;
			const uint16_t *src = sky_cache + y * sw_width + minimum_x;
			uint16_t *dst = sw_color + y * sw_width + minimum_x;
			for (int x = 0; x < maximum_x - minimum_x; ++x)
				if (zb_row[minimum_x + x] == 0)
				{
					dst[x] = src[x];
					++sw_stats.bench_sky_holes;
					if (sw_dbg_tag_x == minimum_x + x && sw_dbg_tag_y == y)
						sw_ri.Con_Printf(PRINT_ALL, "tagxy sky-cache bf=%u color=%04x\n", sw_stats.bench_frames, src[x]);
				}
		}
		sw_stats.sky_pixels += (unsigned)drawn_pixels;
		sw_stats.sky_face_mask |= sky_cache_face_mask;
		return;
	}
	++sw_stats.sky_cache_misses;
	if (sw_dbg_skydiag)
		sw_ri.Con_Printf(PRINT_ALL, "sky: MISS/rebuild rframe=%u angles=%.4f %.4f %.4f holes=%u\n",
			sw_stats.bench_frames, fd->viewangles[0], fd->viewangles[1], fd->viewangles[2],
			sw_stats.bench_holes_topband);
	// Rotating the per-pixel direction by the swirl rotation is linear, so the
	// whole transform folds into the basis vectors once per frame. Direction is
	// then stepped incrementally across each scanline (3 adds per pixel) instead
	// of a full matrix rebuild.
	vec3_t basis_forward, basis_right, basis_up;
	if (rotating)
	{
		rotate_sky_direction(view_forward, basis_forward);
		rotate_sky_direction(view_right, basis_right);
		rotate_sky_direction(view_up, basis_up);
	}
	else
	{
		VectorCopy(view_forward, basis_forward);
		VectorCopy(view_right, basis_right);
		VectorCopy(view_up, basis_up);
	}
	const float horizontal_step = 1.0f / projection_x;
	const float vertical_step = 1.0f / projection_y;
	const float center_x = fd->x + fd->width * 0.5f;
	const float center_y = fd->y + fd->height * 0.5f;
	uint16_t *const cache_dst = (!rotating && sky_cache != NULL) ? sky_cache : NULL;
	unsigned rendered_face_mask = 0;
	for (int y = minimum_y; y < maximum_y; ++y)
	{
		// S-buffer row-coverage proof: every pixel painted => no sky px here.
		const uint32_t *cw = sw_covermask[y];
		qboolean full = true;
		for (int w = minimum_x >> 5; w <= (maximum_x - 1) >> 5; ++w)
			if (cw[w] != 0xFFFFFFFFu)
			{
				full = false;
				break;
			}
		if (full)
			continue;
		const float vertical = (center_y - (y + 0.5f)) * vertical_step;
		vec3_t direction;
		vec3_t step;
		const float horizontal0 = (minimum_x + 0.5f - center_x) * horizontal_step;
		for (int component = 0; component < 3; ++component)
		{
			direction[component] = basis_forward[component] + basis_right[component] * horizontal0 +
				basis_up[component] * vertical;
			step[component] = basis_right[component] * horizontal_step;
		}
		uint16_t *fb_row = sw_color + y * sw_width;
		const uint32_t *zb_row = sw_depth + (size_t)y * sw_width;
		uint16_t *cache_row = cache_dst != NULL ? cache_dst + y * sw_width : NULL;
		int x = minimum_x;
		while (x < maximum_x)
		{
			const int block = min(SW_SPAN_STEP - (x & (SW_SPAN_STEP - 1)), maximum_x - x);
			// Exact coordinates at both block endpoints; if the whole block stays
			// on one sky face, s/t step linearly inside it. Mixed-face blocks fall
			// back to exact per-pixel classification (cube edges are rare).
			float s0, t0, s1, t1;
			const int face0 = sky_coordinates(direction, &s0, &t0);
			vec3_t direction_end;
			direction_end[0] = direction[0] + step[0] * (block - 1);
			direction_end[1] = direction[1] + step[1] * (block - 1);
			direction_end[2] = direction[2] + step[2] * (block - 1);
			const int face1 = sky_coordinates(direction_end, &s1, &t1);
			if (face0 == face1)
			{
				const sw_image_t *image = sw_sky_images[face0];
				const float wscale = (float)(image->width - 1);
				const float hscale = (float)(image->height - 1);
				int is = (int)(s0 * wscale * 65536.0f);
				int it = (int)(t0 * hscale * 65536.0f);
				const int ds = (int)((s1 - s0) * wscale * 65536.0f * SW_INV_WIDTH(block - 1));
				const int dt = (int)((t1 - t0) * hscale * 65536.0f * SW_INV_WIDTH(block - 1));
				const int x_end = x + block;
				while (x < x_end)
				{
					if (zb_row[x] == 0 && (!sky_checker2 || ((x >> 1) & 1) == sw_scan_phase))
					{
						const int texture_x = ClampI(is >> 16, 0, image->width - 1);
						const int texture_y = ClampI(it >> 16, 0, image->height - 1);
						const uint16_t color = image->pixels[texture_y * image->width + texture_x];
						if (sw_dbg_tag_x == x && sw_dbg_tag_y == y)
							sw_ri.Con_Printf(PRINT_ALL, "tagxy sky bf=%u face=%d color=%04x\n",
								sw_stats.bench_frames, face0, color);
						fb_row[x] = color;
						++sw_stats.bench_sky_holes;
						if (cache_row != NULL)
							cache_row[x] = color;
					}
					is += ds; it += dt; ++x;
				}
				rendered_face_mask |= 1u << face0;
			}
			else
			{
				for (int i = 0; i < block; ++i, ++x)
				{
					if (zb_row[x] == 0 && (!sky_checker2 || ((x >> 1) & 1) == sw_scan_phase))
					{
						float s, t;
						const int face = sky_coordinates(direction, &s, &t);
						const sw_image_t *image = sw_sky_images[face];
						const int texture_x = ClampI((int)(s * (image->width - 1)), 0, image->width - 1);
						const int texture_y = ClampI((int)(t * (image->height - 1)), 0, image->height - 1);
						const uint16_t color = image->pixels[texture_y * image->width + texture_x];
						fb_row[x] = color;
						++sw_stats.bench_sky_holes;
						if (cache_row != NULL)
							cache_row[x] = color;
						rendered_face_mask |= 1u << face;
					}
					direction[0] += step[0];
					direction[1] += step[1];
					direction[2] += step[2];
				}
			}
			direction[0] = direction_end[0] + step[0];
			direction[1] = direction_end[1] + step[1];
			direction[2] = direction_end[2] + step[2];
		}
	}
	sw_stats.sky_pixels += (unsigned)drawn_pixels;
	sw_stats.sky_face_mask |= rendered_face_mask;
	if (cache_dst != NULL)
	{
		sky_cache_key.framebuffer_width = sw_width;
		sky_cache_key.framebuffer_height = sw_height;
		sky_cache_key.x = fd->x;
		sky_cache_key.y = fd->y;
		sky_cache_key.width = fd->width;
		sky_cache_key.height = fd->height;
		sky_cache_key.fov_x = fd->fov_x;
		sky_cache_key.fov_y = fd->fov_y;
		sky_cache_key.viewangles[0] = sw_canon_angle(fd->viewangles[0]);
		sky_cache_key.viewangles[1] = sw_canon_angle(fd->viewangles[1]);
		sky_cache_key.viewangles[2] = sw_canon_angle(fd->viewangles[2]);
		sky_cache_key.rotation_angle = rotation_angle;
		sky_cache_key.generation = sw_sky_generation;
		sky_cache_key.valid = true;
		sky_cache_face_mask = rendered_face_mask;
	}
}

void SW_RasterWorldTrans(void)
{
	sw_deferral_bypass = true;
	for (int i = 0; i < sw_deferred_face_count; ++i)
	{
		const sw_deferred_face_t *entry = &sw_deferred_faces[i];
		draw_face(entry->bsp, entry->face_index, entry->origin, entry->entity_frame);
	}
	sw_deferred_face_count = 0;
	sw_deferral_bypass = false;
}

// E59: front-to-back BSP traversal. Node face lists partition the BSP, so
// walking near-subtree -> node faces -> far-subtree draws every PVS-marked
// face exactly once while making nearer content win the z-test FIRST: a
// later far-face candidate is rejected at the zb compare before any texel or
// light work, converting shaded-then-overwritten pixels into cheap rejects.
// Depth writes are unchanged, so entity/sky gating and coplanar-tie semantics
// stay z-domain (first painter wins an exact key tie; gates arbitrate).
static void draw_world_node_ftb(const sw_bsp_t *bsp, const int node_index, const vec3_t vieworg,
	const byte *visible_faces, const int entity_frame)
{
	if (node_index < 0)
		return; // leaf: no faces of its own (node lists partition all faces)
	const dnode_t *node = &bsp->nodes[node_index];
	const dplane_t *plane = &bsp->planes[LittleLong(node->planenum)];
	const float distance = vieworg[0] * LittleFloat(plane->normal[0]) +
		vieworg[1] * LittleFloat(plane->normal[1]) + vieworg[2] * LittleFloat(plane->normal[2]) -
		LittleFloat(plane->dist);
	const int side = distance < 0.0f ? 1 : 0;
	draw_world_node_ftb(bsp, LittleLong(node->children[side]), vieworg, visible_faces, entity_frame);
	const int first = little_ushort(node->firstface);
	const int count = little_ushort(node->numfaces);
	for (int i = 0; i < count; ++i)
	{
		const int index = first + i;
		if (visible_faces[index])
		{
			const vec3_t zero = { 0.0f, 0.0f, 0.0f };
			draw_face(bsp, index, zero, entity_frame);
		}
	}
	draw_world_node_ftb(bsp, LittleLong(node->children[side ^ 1]), vieworg, visible_faces, entity_frame);
}

void SW_RasterWorld(const refdef_t *fd)
{
	if (sw_world_model == NULL || sw_world_model->bsp == NULL || (fd->rdflags & RDF_NOWORLDMODEL))
		return;
	memset(sw_covermask, 0, (size_t)sw_height * sizeof(sw_covermask[0]));
	// Per-face stores are keyed by face index into this BSP; invalidate all of
	// them when the world changes so a new map cannot reuse stale light.
	if (sw_face_light_world != sw_world_model->bsp)
	{
		sw_face_light_world = sw_world_model->bsp;
		++sw_face_light_generation;
		if (sw_face_light_generation == 0)
		{
			for (int i = 0; i < sw_face_light_store_count; ++i)
				sw_face_light_stores[i].generation = 0;
			sw_face_light_generation = 1;
		}
	}
	const sw_bsp_t *bsp = sw_world_model->bsp;
	memset(sw_visible_faces, 0, (size_t)bsp->num_faces);
	const int leaf_index = point_leaf(bsp, fd->vieworg);
	const int view_cluster = leaf_index >= 0 && leaf_index < bsp->num_leaves ? LittleShort(bsp->leaves[leaf_index].cluster) : -1;
	int pvs_clusters = 0;
	const qboolean have_pvs = decompress_pvs_row(bsp, view_cluster, sw_pvs_row, &pvs_clusters);
	for (int i = 0; i < bsp->num_leaves; ++i)
	{
		const dleaf_t *leaf = &bsp->leaves[i];
		const int cluster = LittleShort(leaf->cluster);
		const int area = LittleShort(leaf->area);
		const qboolean cluster_is_visible = !have_pvs || cluster < 0 || cluster >= pvs_clusters ||
			(sw_pvs_row[cluster >> 3] & (1u << (cluster & 7))) != 0;
		if (!cluster_is_visible ||
			(fd->areabits != NULL && area >= 0 && !(fd->areabits[area >> 3] & (1 << (area & 7)))))
			continue;
		const int first = little_ushort(leaf->firstleafface);
		const int count = little_ushort(leaf->numleaffaces);
		if (first < 0 || count < 0 || first + count > bsp->num_leaffaces)
			continue;
		for (int face = 0; face < count; ++face)
		{
			const int index = little_ushort(bsp->leaffaces[first + face]);
			if (index >= 0 && index < bsp->num_faces)
				sw_visible_faces[index] = 1;
		}
	}
	if (bsp->submodels != NULL && bsp->num_submodels > 0)
	{
		sw_world_ftb_mask_active = true;
		const int head_node = LittleLong(bsp->submodels[0].headnode);
		if (head_node >= 0 && head_node < bsp->num_nodes)
			draw_world_node_ftb(bsp, head_node, fd->vieworg, sw_visible_faces, (int)(fd->time * 2.0f));
		else
		{
			const vec3_t zero = { 0.0f, 0.0f, 0.0f };
			for (int i = 0; i < bsp->num_faces; ++i)
				if (sw_visible_faces[i])
					draw_face(bsp, i, zero, (int)(fd->time * 2.0f));
		}
		sw_world_ftb_mask_active = false;
	}
	sw_scan_phase ^= 1;
}

static qboolean project_point(const vec3_t point, int *x, int *y, float *depth)
{
	const sw_vertex_t vertex = world_to_view(point, 0.0f, 0.0f);
	if (vertex.z < SW_NEAR_CLIP)
		return false;
	const screen_vertex_t screen = project(vertex);
	if (!isfinite(screen.x) || !isfinite(screen.y) || screen.x < INT_MIN || screen.x > INT_MAX ||
		screen.y < INT_MIN || screen.y > INT_MAX)
		return false;
	*x = (int)screen.x;
	*y = (int)screen.y;
	*depth = vertex.z;
	return true;
}

// Production-faithful sprite rendering (mirrors src/ref_gl1/src/gl1_Sprite.c).
// All sprite kinds are emitted as world-space quads/polygons through the
// perspective-correct triangle path (draw_flex_triangle).
// ---------------------------------------------------------------------------

static uint16_t sw_white_pixel_storage;
static sw_image_t sw_white_image = {
	.name = "vertex-color",
	.width = 1,
	.height = 1,
	.num_frames = 1,
	.pixels = &sw_white_pixel_storage,
	.has_alpha = false,
	.synthetic_fallback = true,
	.registration_sequence = 0,
};

// RF_FIXED sprites are oriented by entity angles; others face the camera.
static void sprite_basis(const entity_t *entity, vec3_t up, vec3_t right)
{
	if (entity->flags & RF_FIXED)
	{
		vec3_t dir;
		DirAndUpFromAngles(entity->angles, dir, up);
		CrossProduct(up, dir, right);
		VectorNormalize(right);
	}
	else
	{
		for (int axis = 0; axis < 3; ++axis)
		{
			up[axis] = view_up[axis];
			right[axis] = view_right[axis];
		}
	}
}

// Mirrors R_HandleTransparency semantics: blend class + alpha handling.
static qboolean sprite_blend(const entity_t *entity, float color[4])
{
	if (entity->flags & (RF_TRANS_ADD | RF_TRANS_ADD_ALPHA))
	{
		if (entity->flags & RF_ALPHA_TEXTURE)
		{
			// GL_SRC_ALPHA, GL_ONE: per-texel alpha attenutation, full color.
			color[0] = entity->color.r / 255.0f;
			color[1] = entity->color.g / 255.0f;
			color[2] = entity->color.b / 255.0f;
			color[3] = 1.0f;
		}
		else if (entity->flags & RF_TRANS_ADD_ALPHA)
		{
			// GL_ONE, GL_ONE with rgb premultiplied by (a/255)^2 (see gl1_Misc.c).
			const float scaler = (entity->color.a / 255.0f) / 255.0f;
			color[0] = entity->color.r * scaler;
			color[1] = entity->color.g * scaler;
			color[2] = entity->color.b * scaler;
			color[3] = 1.0f;
		}
		else
		{
			// GL_ONE, GL_ONE with raw color.
			color[0] = entity->color.r / 255.0f;
			color[1] = entity->color.g / 255.0f;
			color[2] = entity->color.b / 255.0f;
			color[3] = 1.0f;
		}
		return true;
	}
	color[0] = entity->color.r / 255.0f;
	color[1] = entity->color.g / 255.0f;
	color[2] = entity->color.b / 255.0f;
	if (entity->flags & RF_TRANS_GHOST)
		// GL_DST_COLOR, GL_ZERO subtractive-style blend; approximate with half
		// alpha source-over to stay single-pass on the DOS sw rasterizer.
		color[3] = entity->color.a / 255.0f * 0.5f;
	else
		color[3] = entity->color.a / 255.0f;
	return false;
}

static const sw_image_t *sprite_variant_image(const entity_t *entity,
					      const sw_model_t *model)
{
	if (model != NULL && model->kind == SW_MODEL_SPRITE && model->num_frames > 0 &&
		model->sprite_frames != NULL)
	{
		const int frame = ((entity->frame % model->num_frames) + model->num_frames) %
			model->num_frames;
		if (model->sprite_frames[frame].image != NULL)
			return model->sprite_frames[frame].image;
	}
	sw_white_pixel_storage = SW_RGB565(entity->color.r, entity->color.g, entity->color.b);
	return &sw_white_image;
}

// counter_index: 0=dynamic, 1=variable, 2=line, other=standard (no extra counter).
static void draw_sprite_poly(const entity_t *entity, const vec3_t points[],
			     const float uv[][2], const int count,
			     const sw_image_t *image, const unsigned counter_index)
{
	float color[4];
	const qboolean additive = sprite_blend(entity, color);
	sw_bench_tri_category = 2;
	sw_sprite_no_depth_test = (entity->flags & RF_NODEPTHTEST) != 0;
	const qboolean depth_write = !additive && entity->color.a == 255 &&
		!sw_sprite_no_depth_test;
	for (int i = 1; i + 1 < count; ++i)
	{
		vec3_t tri[3];
		float tri_uv[3][2];
		float tri_color[3][4];
		const int indices[3] = { 0, i, i + 1 };
		for (int corner = 0; corner < 3; ++corner)
		{
			for (int axis = 0; axis < 3; ++axis)
				tri[corner][axis] = points[indices[corner]][axis];
			tri_uv[corner][0] = uv[indices[corner]][0];
			tri_uv[corner][1] = uv[indices[corner]][1];
			for (int channel = 0; channel < 4; ++channel)
				tri_color[corner][channel] = color[channel];
		}
		draw_flex_triangle(tri, tri_uv, tri_color, image, depth_write, additive);
	}
	sw_sprite_no_depth_test = false;
	sw_bench_tri_category = 0;
	++sw_stats.sprite_entities;
	if (counter_index == 0)
		++sw_stats.dynamic_sprites;
	else if (counter_index == 1)
		++sw_stats.variable_sprites;
	else if (counter_index == 2)
		++sw_stats.line_sprites;
}

static void draw_sprite_entity(const entity_t *entity, const sw_model_t *model)
{
	if (model->num_frames == 0 || model->sprite_frames == NULL)
		return;
	const sw_sprite_frame_t *frame = &model->sprite_frames[
		((entity->frame % model->num_frames) + model->num_frames) % model->num_frames];
	if (frame->image == NULL)
		return;
	const float scale = entity->scale > 0.0f ? entity->scale : 1.0f;
	vec3_t up, right;
	sprite_basis(entity, up, right);
	const float xl = (float)-frame->origin_x * scale;
	const float xr = (float)(frame->width - frame->origin_x) * scale;
	const float yt = (float)-frame->origin_y * scale;
	const float yb = (float)(frame->height - frame->origin_y) * scale;
	vec3_t points[4];
	const float xoff[4] = { xl, xl, xr, xr };
	const float yoff[4] = { yt, yb, yb, yt };
	const float uv[4][2] = { { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f } };
	for (int i = 0; i < 4; ++i)
		for (int axis = 0; axis < 3; ++axis)
			points[i][axis] = entity->origin[axis] + yoff[i] * up[axis] + xoff[i] * right[axis];
	draw_sprite_poly(entity, (const vec3_t *)points, uv, 4, frame->image, 3);
}

static void draw_dynamic_sprite(const entity_t *entity, const sw_model_t *model)
{
	vec3_t up, right;
	sprite_basis(entity, up, right);
	vec3_t points[4];
	float uv[4][2];
	for (int i = 0; i < 4; ++i)
	{
		if (!isfinite(entity->verts[i].x) || !isfinite(entity->verts[i].y) ||
			!isfinite(entity->verts[i].s) || !isfinite(entity->verts[i].t))
			return;
		for (int axis = 0; axis < 3; ++axis)
			points[i][axis] = entity->origin[axis] +
				entity->scale * entity->verts[i].y * up[axis] +
				entity->scale * entity->verts[i].x * right[axis];
		uv[i][0] = entity->verts[i].s;
		uv[i][1] = entity->verts[i].t;
	}
	draw_sprite_poly(entity, (const vec3_t *)points, (const float (*)[2])uv, 4,
			sprite_variant_image(entity, model), 0);
}

#define SW_MAX_VARIABLE_VERTS 32
static void draw_variable_sprite(const entity_t *entity, const sw_model_t *model)
{
	if (entity->verts_p == NULL || entity->numVerts < 3 ||
		entity->numVerts > SW_MAX_VARIABLE_VERTS)
		return;
	vec3_t up, right;
	sprite_basis(entity, up, right);
	vec3_t points[SW_MAX_VARIABLE_VERTS];
	float uv[SW_MAX_VARIABLE_VERTS][2];
	for (int i = 0; i < entity->numVerts; ++i)
	{
		if (!isfinite(entity->verts_p[i].x) || !isfinite(entity->verts_p[i].y) ||
			!isfinite(entity->verts_p[i].s) || !isfinite(entity->verts_p[i].t))
			return;
		for (int axis = 0; axis < 3; ++axis)
			points[i][axis] = entity->origin[axis] +
				entity->scale * entity->verts_p[i].y * up[axis] +
				entity->scale * entity->verts_p[i].x * right[axis];
		uv[i][0] = entity->verts_p[i].s;
		uv[i][1] = entity->verts_p[i].t;
	}
	draw_sprite_poly(entity, (const vec3_t *)points, (const float (*)[2])uv,
			entity->numVerts, sprite_variant_image(entity, model), 1);
}

static void draw_line_sprite(const entity_t *entity, const sw_model_t *model)
{
	vec3_t diff;
	for (int axis = 0; axis < 3; ++axis)
		diff[axis] = entity->endpos[axis] - entity->startpos[axis];
	vec3_t dir;
	CrossProduct(diff, view_forward, dir);
	if (!normalize_checked(dir))
		return;
	const float tile = entity->tile > 0.0f ? entity->tile : 1.0f;
	vec3_t points[4];
	for (int axis = 0; axis < 3; ++axis)
	{
		const float start_offset = dir[axis] * entity->scale * 0.5f;
		const float end_offset = dir[axis] * entity->scale2 * 0.5f;
		points[0][axis] = entity->startpos[axis] - start_offset;
		points[1][axis] = entity->startpos[axis] + start_offset;
		points[2][axis] = entity->endpos[axis] + end_offset;
		points[3][axis] = entity->endpos[axis] - end_offset;
	}
	const float uv[4][2] = {
		{ 0.0f, entity->tileoffset },
		{ 1.0f, entity->tileoffset },
		{ 1.0f, entity->tileoffset + tile },
		{ 0.0f, entity->tileoffset + tile }
	};
	draw_sprite_poly(entity, (const vec3_t *)points, uv, 4,
			sprite_variant_image(entity, model), 2);
}

typedef struct flex_raster_vertex_s {
	float x;
	float y;
	float z;
	float s;
	float t;
	float red;
	float green;
	float blue;
	float alpha;
} flex_raster_vertex_t;

typedef struct flex_opaque_vertex_s {
	float x;
	float y;
	float inverse_z;
	float s;
	float t;
	float red;
	float green;
	float blue;
} flex_opaque_vertex_t;

typedef struct flex_screen_vertex_s {
	float x;
	float y;
	float inverse_z;
	float s_over_z;
	float t_over_z;
	float red_over_z;
	float green_over_z;
	float blue_over_z;
	union
	{
		float alpha_over_z;
		// E4 (ideas #4): gouraud scalar for the CLUT path, written ONLY by the
		// opaque tri setup (which never reads alpha_over_z); no size growth.
		float intensity_over_z;
	};
} flex_screen_vertex_t;

static qboolean normalize_checked(vec3_t vector)
{
	const float length_squared = dot3(vector, vector);
	if (!isfinite(length_squared) || length_squared < 0.000001f)
		return false;
	const float inverse_length = 1.0f / sqrtf(length_squared);
	for (int axis = 0; axis < 3; ++axis)
		vector[axis] *= inverse_length;
	return true;
}

static qboolean face_point_inside(const sw_bsp_t *bsp, const dface_t *face, const vec3_t point, const vec3_t normal)
{
	const int first = LittleLong(face->firstedge);
	const int count = LittleShort(face->numedges);
	float sign = 0.0f;
	for (int i = 0; i < count; ++i)
	{
		const int surface_edge = LittleLong(bsp->surfedges[first + i]);
		const int next_surface_edge = LittleLong(bsp->surfedges[first + (i + 1) % count]);
		const int edge_index = abs(surface_edge);
		const int next_edge_index = abs(next_surface_edge);
		const int vertex_index = little_ushort(bsp->edges[edge_index].v[surface_edge >= 0 ? 0 : 1]);
		const int next_vertex_index = little_ushort(bsp->edges[next_edge_index].v[next_surface_edge >= 0 ? 0 : 1]);
		vec3_t edge_vector;
		vec3_t point_vector;
		for (int axis = 0; axis < 3; ++axis)
		{
			edge_vector[axis] = LittleFloat(bsp->vertices[next_vertex_index].point[axis]) - LittleFloat(bsp->vertices[vertex_index].point[axis]);
			point_vector[axis] = point[axis] - LittleFloat(bsp->vertices[vertex_index].point[axis]);
		}
		vec3_t cross;
		CrossProduct(edge_vector, point_vector, cross);
		const float value = dot3(cross, normal);
		if (fabsf(value) <= 0.01f)
			continue;
		if (sign == 0.0f)
			sign = value;
		else if ((value > 0.0f) != (sign > 0.0f))
			return false;
	}
	return true;
}

static qboolean sample_bsp_light(const vec3_t origin, float *red, float *green, float *blue);

// Exact-match test for one world face: the vertical ray from origin downward
// (up to 2048 units, 0.01 upward tolerance) striking the face plane, then a
// point-in-polygon check. Selection keeps the minimum drop; exact ties keep
// the smallest face index (identical to the legacy ascending linear scan).
static qboolean sw_light_face_test(const sw_bsp_t *bsp, const dface_t *face, const vec3_t origin,
	float *nearest, const dface_t **nearest_face, int *nearest_index, vec3_t nearest_point, const int face_index)
{
	const int flags = LittleLong(bsp->texinfo[LittleShort(face->texinfo)].flags);
	if (flags & SURF_FULLBRIGHT)
		return false;
	const dplane_t *plane = &bsp->planes[little_ushort(face->planenum)];
	vec3_t normal = {
		LittleFloat(plane->normal[0]), LittleFloat(plane->normal[1]), LittleFloat(plane->normal[2])
	};
	float distance = LittleFloat(plane->dist);
	if (LittleShort(face->side))
	{
		VectorScale(normal, -1.0f, normal);
		distance = -distance;
	}
	if (fabsf(normal[2]) < 0.0001f)
		return false;
	const float drop = (dot3(normal, origin) - distance) / normal[2];
	if (!isfinite(drop) || drop < -0.01f || drop > 2048.0f)
		return false;
	if (drop > *nearest || (drop == *nearest && face_index >= *nearest_index))
		return false;
	vec3_t point = { origin[0], origin[1], origin[2] - drop };
	if (!face_point_inside(bsp, face, point, normal))
		return false;
	*nearest = drop;
	*nearest_face = face;
	*nearest_index = face_index;
	VectorCopy(point, nearest_point);
	return true;
}

// BSP descent replacement for the legacy full-face linear scan. The vertical
// ray column [origin.z + 0.01, origin.z - 2048] only intersects subtrees whose
// node plane the column crosses; other subtrees cannot contain an accepted
// point, so they are pruned exactly. Leaf faces are de-duplicated by the
// (drop, face_index) selection rule, which matches the linear scan's result.
static void sw_light_descend(const sw_bsp_t *bsp, const vec3_t origin, const int nodenum,
	float *nearest, const dface_t **nearest_face, int *nearest_index, vec3_t nearest_point)
{
	if (nodenum < 0)
	{
		const dleaf_t *leaf = &bsp->leaves[-1 - nodenum];
		const int first = little_ushort(leaf->firstleafface);
		const int count = little_ushort(leaf->numleaffaces);
		for (int i = 0; i < count; ++i)
		{
			const int face_index = little_ushort(bsp->leaffaces[first + i]);
			sw_light_face_test(bsp, &bsp->faces[face_index], origin, nearest, nearest_face, nearest_index, nearest_point, face_index);
		}
		return;
	}
	const dnode_t *node = &bsp->nodes[nodenum];
	const dplane_t *plane = &bsp->planes[LittleLong(node->planenum)];
	const float nz = LittleFloat(plane->normal[2]);
	const float dist = LittleFloat(plane->normal[0]) * origin[0] + LittleFloat(plane->normal[1]) * origin[1] +
		nz * origin[2] - LittleFloat(plane->dist);
	const int side = dist >= 0.0f ? 0 : 1;
	sw_light_descend(bsp, origin, LittleLong(node->children[side]), nearest, nearest_face, nearest_index, nearest_point);
	qboolean cross;
	if (fabsf(nz) >= 0.0001f)
	{
		const float t = dist / nz;
		cross = (t >= -0.5f && t <= 2048.5f);
	}
	else
		cross = (dist >= -0.5f && dist <= 0.5f);
	if (cross)
		sw_light_descend(bsp, origin, LittleLong(node->children[side ^ 1]), nearest, nearest_face, nearest_index, nearest_point);
}

static qboolean sample_bsp_light(const vec3_t origin, float *red, float *green, float *blue)
{
	if (sw_world_model == NULL || sw_world_model->bsp == NULL)
		return false;
	const sw_bsp_t *bsp = sw_world_model->bsp;
	float nearest = FLT_MAX;
	const dface_t *nearest_face = NULL;
	int nearest_index = bsp->num_faces;
	vec3_t nearest_point = { 0.0f, 0.0f, 0.0f };
	const qboolean descent_ok = bsp->nodes != NULL && bsp->num_nodes > 0 && bsp->leaves != NULL && bsp->leaffaces != NULL;
	if (descent_ok)
		sw_light_descend(bsp, origin, 0, &nearest, &nearest_face, &nearest_index, nearest_point);
	else
	{
		for (int face_index = 0; face_index < bsp->num_faces; ++face_index)
		{
			const dface_t *face = &bsp->faces[face_index];
			if (sw_light_face_test(bsp, face, origin, &nearest, &nearest_face, &nearest_index, nearest_point, face_index))
				continue;
		}
	}
	if (nearest_face == NULL)
		return false;
	const texinfo_t *texture = &bsp->texinfo[LittleShort(nearest_face->texinfo)];
	const float s = nearest_point[0] * LittleFloat(texture->vecs[0][0]) + nearest_point[1] * LittleFloat(texture->vecs[0][1]) +
		nearest_point[2] * LittleFloat(texture->vecs[0][2]) + LittleFloat(texture->vecs[0][3]);
	const float t = nearest_point[0] * LittleFloat(texture->vecs[1][0]) + nearest_point[1] * LittleFloat(texture->vecs[1][1]) +
		nearest_point[2] * LittleFloat(texture->vecs[1][2]) + LittleFloat(texture->vecs[1][3]);
	float minimum_s;
	float minimum_t;
	int light_width;
	int light_height;
	const sw_face_packet_t *packet = bsp->packets != NULL && nearest_index >= 0 && nearest_index < bsp->num_faces ?
		&bsp->packets[nearest_index] : NULL;
	if (packet != NULL && packet->verts != NULL)
	{
		minimum_s = packet->light_min_s;
		minimum_t = packet->light_min_t;
		light_width = packet->light_width;
		light_height = packet->light_height;
	}
	else
	{
		minimum_s = FLT_MAX;
		minimum_t = FLT_MAX;
		float maximum_s = -FLT_MAX;
		float maximum_t = -FLT_MAX;
		const int first = LittleLong(nearest_face->firstedge);
		const int count = LittleShort(nearest_face->numedges);
		for (int i = 0; i < count; ++i)
		{
			const int surface_edge = LittleLong(bsp->surfedges[first + i]);
			const int vertex_index = little_ushort(bsp->edges[abs(surface_edge)].v[surface_edge >= 0 ? 0 : 1]);
			const dvertex_t *vertex = &bsp->vertices[vertex_index];
			const float vs = LittleFloat(vertex->point[0]) * LittleFloat(texture->vecs[0][0]) +
				LittleFloat(vertex->point[1]) * LittleFloat(texture->vecs[0][1]) +
				LittleFloat(vertex->point[2]) * LittleFloat(texture->vecs[0][2]) + LittleFloat(texture->vecs[0][3]);
			const float vt = LittleFloat(vertex->point[0]) * LittleFloat(texture->vecs[1][0]) +
				LittleFloat(vertex->point[1]) * LittleFloat(texture->vecs[1][1]) +
				LittleFloat(vertex->point[2]) * LittleFloat(texture->vecs[1][2]) + LittleFloat(texture->vecs[1][3]);
			minimum_s = min(minimum_s, vs);
			minimum_t = min(minimum_t, vt);
			maximum_s = max(maximum_s, vs);
			maximum_t = max(maximum_t, vt);
		}
		minimum_s = floorf(minimum_s / 16.0f) * 16.0f;
		minimum_t = floorf(minimum_t / 16.0f) * 16.0f;
		light_width = (int)((ceilf(maximum_s / 16.0f) * 16.0f - minimum_s) / 16.0f + 1.0f);
		light_height = (int)((ceilf(maximum_t / 16.0f) * 16.0f - minimum_t) / 16.0f + 1.0f);
	}
	lightmap_color(bsp, nearest_face, s, t, minimum_s, minimum_t, light_width, light_height, red, green, blue);
	return true;
}

static void flex_light(const entity_t *entity, float light[3])
{
	qboolean sampled = false;
	if (entity->flags & RF_TRANS_ADD_ALPHA)
	{
		light[0] = light[1] = light[2] = entity->color.a / 255.0f;
	}
	else if (entity->flags & RF_FULLBRIGHT)
	{
		light[0] = light[1] = light[2] = 1.0f;
	}
	else if (entity->absLight.r != 0 || entity->absLight.g != 0 || entity->absLight.b != 0)
	{
		light[0] = entity->absLight.r / 255.0f;
		light[1] = entity->absLight.g / 255.0f;
		light[2] = entity->absLight.b / 255.0f;
	}
	else
	{
		sampled = sample_bsp_light(entity->origin, &light[0], &light[1], &light[2]);
		if (!sampled)
			light[0] = light[1] = light[2] = 0.25f;
		for (int i = 0; sw_refdef.dlights != NULL && i < sw_refdef.num_dlights && i < MAX_DLIGHTS; ++i)
		{
			const dlight_t *dynamic = &sw_refdef.dlights[i];
			vec3_t delta;
			VectorSubtract(entity->origin, dynamic->origin, delta);
			const float distance_sq = max(0.0f, dot3(delta, delta));
			const float radius_sq = dynamic->intensity * dynamic->intensity;
			if (distance_sq >= radius_sq)
				continue;
			const float distance = sqrtf(distance_sq);
			const float contribution = max(dynamic->intensity - distance, 0.0f) / 256.0f;
			light[0] += dynamic->color.r / 255.0f * contribution;
			light[1] += dynamic->color.g / 255.0f * contribution;
			light[2] += dynamic->color.b / 255.0f * contribution;
		}
	}
	for (int channel = 0; channel < 3; ++channel)
		light[channel] *= entity->color.c_array[channel] / 255.0f;
	if ((entity->flags & RF_MINLIGHT) && light[0] <= 0.1f && light[1] <= 0.1f && light[2] <= 0.1f)
		light[0] = light[1] = light[2] = 0.1f;
	if (entity->flags & RF_GLOW)
		light[0] = light[1] = light[2] = sinf(sw_refdef.time * 7.0f) * 0.3f + 0.7f;
}

static void interpolate_flex_placement(const Placement_t *current, const Placement_t *old,
	const float move[3], const float front[3], const float back[3], Placement_t *output)
{
	for (int axis = 0; axis < 3; ++axis)
	{
		output->origin[axis] = current->origin[axis] * front[axis] + old->origin[axis] * back[axis] + move[axis];
		output->direction[axis] = current->direction[axis] * front[axis] + old->direction[axis] * back[axis] + move[axis];
		output->up[axis] = current->up[axis] * front[axis] + old->up[axis] * back[axis] + move[axis];
	}
}

static qboolean flex_joint_rotation(const Placement_t *placement, const vec3_t angles, matrix3_t rotation)
{
	vec3_t direction;
	vec3_t up;
	VectorSubtract(placement->direction, placement->origin, direction);
	VectorSubtract(placement->up, placement->origin, up);
	if (!normalize_checked(direction) || !normalize_checked(up))
		return false;
	matrix3_t to_world;
	matrix3_t partial_local;
	matrix3_t angle_rotation;
	matrix3_t temporary;
	vec3_t local_angles = { angles[0], angles[1], angles[2] };
	local_angles[ROLL] += (float)Matricies3FromDirAndUp(direction, up, to_world, partial_local);
	Matrix3FromAngles(local_angles, angle_rotation);
	Matrix3MultByMatrix3(angle_rotation, to_world, temporary);
	Matrix3MultByMatrix3(partial_local, temporary, rotation);
	return true;
}

static qboolean make_flex_rotations(const entity_t *entity, const int count,
	const Placement_t joints[SW_MAX_FLEX_JOINTS], matrix3_t rotations[SW_MAX_FLEX_JOINTS],
	int joint_indices[SW_MAX_FLEX_JOINTS])
{
	if (count < 1 || count > SW_MAX_FLEX_JOINTS || entity->rootJoint < 0 ||
		entity->rootJoint >= MAX_ARRAYED_SKELETAL_JOINTS || sw_ri.skeletalJoints == NULL ||
		sw_ri.jointNodes == NULL)
		return false;
	joint_indices[0] = entity->rootJoint;
	for (int joint = 1; joint < count; ++joint)
	{
		const int node = sw_ri.skeletalJoints[joint_indices[joint - 1]].children;
		if (node < 0 || node >= MAX_ARRAYED_JOINT_NODES)
			return false;
		joint_indices[joint] = sw_ri.jointNodes[node].data;
		if (joint_indices[joint] < 0 || joint_indices[joint] >= MAX_ARRAYED_SKELETAL_JOINTS)
			return false;
	}
	for (int joint = 0; joint < count; ++joint)
	{
		const vec3_t *angles = &sw_ri.skeletalJoints[joint_indices[joint]].angles;
		if (!isfinite((*angles)[0]) || !isfinite((*angles)[1]) || !isfinite((*angles)[2]) ||
			!flex_joint_rotation(&joints[joint], *angles, rotations[joint]))
			return false;
	}
	return true;
}

static void rotate_flex_clusters(const sw_flex_t *flex,
	const Placement_t joints[SW_MAX_FLEX_JOINTS],
	const matrix3_t rotations[SW_MAX_FLEX_JOINTS], vec3_t *vertices)
{
	for (int joint = flex->num_clusters - 1; joint >= 0; --joint)
		for (int index = 0; index < flex->cluster_counts[joint]; ++index)
			RotatePointAboutLocalOrigin(rotations[joint], joints[joint].origin,
				vertices[flex->cluster_vertices[joint][index]]);
}

static void entity_transform_basis(const vec3_t origin, const float cr, const float sr, const float cp,
	const float sp, const float cy, const float sy, const vec3_t input, vec3_t output);

static void entity_transform(const entity_t *entity, const vec3_t input, vec3_t output)
{
	const float cr = cosf(-entity->angles[2]);
	const float sr = sinf(-entity->angles[2]);
	const float cp = cosf(-entity->angles[0]);
	const float sp = sinf(-entity->angles[0]);
	const float cy = cosf(entity->angles[1]);
	const float sy = sinf(entity->angles[1]);
	entity_transform_basis(entity->origin, cr, sr, cp, sp, cy, sy, input, output);
}

// View-space position + inverse-z for one flex vertex index, computed once per
// entity per frame (trig is hoisted to the entity level and the transform/order
// is identical to entity_transform + world_to_view + project_flex).
typedef struct flex_proj_cache_s {
	float x;
	float y;
	float z;
	float inverse_z;
	unsigned entity_seq;
} flex_proj_cache_t;
static flex_proj_cache_t sw_flex_proj_cache[SW_MAX_FLEX_VERTS];
static unsigned sw_flex_proj_cache_entity_seq;

static void flex_proj_cache_reset(void)
{
	++sw_flex_proj_cache_entity_seq;
	if (sw_flex_proj_cache_entity_seq == 0)
	{
		for (int i = 0; i < SW_MAX_FLEX_VERTS; ++i)
			sw_flex_proj_cache[i].entity_seq = 0;
		sw_flex_proj_cache_entity_seq = 1;
	}
}

static void entity_transform_basis(const vec3_t origin, const float cr, const float sr, const float cp,
	const float sp, const float cy, const float sy, const vec3_t input, vec3_t output)
{
	const vec3_t roll = { input[0], input[1] * cr - input[2] * sr, input[1] * sr + input[2] * cr };
	const vec3_t pitch = { roll[0] * cp + roll[2] * sp, roll[1], -roll[0] * sp + roll[2] * cp };
	output[0] = pitch[0] * cy - pitch[1] * sy + origin[0];
	output[1] = pitch[0] * sy + pitch[1] * cy + origin[1];
	output[2] = pitch[2] + origin[2];
}

// E7 (census decision from run #78): fuse View x Entity into one 3x3 per
// entity-frame: flex proj fills then cost 9 mul + 9 add per vertex instead of
// 21 mul + 18 add (entity_transform_basis roll/pitch/yaw + world_to_view).
// Reassociates FLOAT position chains only; 2LSB gates arbitrate acceptance.
static void flex_fused_view_matrix(const float cr, const float sr, const float cp, const float sp,
	const float cy, const float sy, const vec3_t origin, float m[3][3], float t[3])
{
	// R = Ryaw * Rpitch * Rroll, matching entity_transform_basis exactly.
	const float r00 = cy * cp;
	const float r01 = cy * sp * sr - sy * cr;
	const float r02 = cy * sp * cr + sy * sr;
	const float r10 = sy * cp;
	const float r11 = sy * sp * sr + cy * cr;
	const float r12 = sy * sp * cr - cy * sr;
	const float r20 = -sp;
	const float r21 = cp * sr;
	const float r22 = cp * cr;
	// M = Vrows . R, t = Vrows . (origin - vieworg)
	const vec3_t* vbasis[3] = { &view_right, &view_up, &view_forward };
	const float rcols[3][3] = {{r00, r01, r02}, {r10, r11, r12}, {r20, r21, r22}};
	const vec3_t o = { origin[0] - sw_refdef.vieworg[0], origin[1] - sw_refdef.vieworg[1],
		origin[2] - sw_refdef.vieworg[2] };
	for (int i = 0; i < 3; ++i)
	{
		const float *v = *vbasis[i];
		m[i][0] = v[0] * rcols[0][0] + v[1] * rcols[1][0] + v[2] * rcols[2][0];
		m[i][1] = v[0] * rcols[0][1] + v[1] * rcols[1][1] + v[2] * rcols[2][1];
		m[i][2] = v[0] * rcols[0][2] + v[1] * rcols[1][2] + v[2] * rcols[2][2];
		t[i] = dot3(v, o);
	}
}

static int clip_flex_near(const flex_raster_vertex_t input[3], flex_raster_vertex_t output[5])
{
	int output_count = 0;
	for (int i = 0; i < 3; ++i)
	{
		const flex_raster_vertex_t current = input[i];
		const flex_raster_vertex_t previous = input[(i + 2) % 3];
		const qboolean current_inside = current.z >= SW_NEAR_CLIP;
		const qboolean previous_inside = previous.z >= SW_NEAR_CLIP;
		if (current_inside != previous_inside)
		{
			const float fraction = (SW_NEAR_CLIP - previous.z) / (current.z - previous.z);
			flex_raster_vertex_t intersection;
			float *destination = &intersection.x;
			const float *from = &previous.x;
			const float *to = &current.x;
			for (int field = 0; field < 9; ++field)
				destination[field] = from[field] + (to[field] - from[field]) * fraction;
			intersection.z = SW_NEAR_CLIP;
			output[output_count++] = intersection;
		}
		if (current_inside)
			output[output_count++] = current;
	}
	return output_count;
}

static flex_screen_vertex_t project_flex(const flex_raster_vertex_t vertex)
{
	const float inverse_z = 1.0f / vertex.z;
	flex_screen_vertex_t output;
	output.x = sw_refdef.x + sw_refdef.width * 0.5f + vertex.x * projection_x * inverse_z;
	output.y = sw_refdef.y + sw_refdef.height * 0.5f - vertex.y * projection_y * inverse_z;
	output.inverse_z = inverse_z;
	output.s_over_z = vertex.s * inverse_z;
	output.t_over_z = vertex.t * inverse_z;
	output.red_over_z = vertex.red * inverse_z;
	output.green_over_z = vertex.green * inverse_z;
	output.blue_over_z = vertex.blue * inverse_z;
	output.alpha_over_z = vertex.alpha * inverse_z;
	return output;
}

static float flex_edge(const flex_screen_vertex_t a, const flex_screen_vertex_t b, const float x, const float y)
{
	return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}

// Span-engine triangle raster for flex models and sprites: 32-pixel subdivided
// affine stepping of the five perspective attributes (s/t/r/g/b/a over z) plus
// affine inverse-z. Gouraud color and alpha are interpolated with the same
// fixed-point granularity as the world lightmap path (+/-1 LSB tolerance).
// ---------------------------------------------------------------------------
// E4 (ideas #4): intensity-shade CLUT for flex opaque spans.
// One table per current entity-light key: clut[level][skin8] = packed 565 of
// (texel_rgb x base_light x level_fraction) computed with EXACTLY the integer
// ops the per-pixel path uses, so at the quantization centers the result is
// bit-identical to the lerped vcolor path; error = intensity quantization only.
// env H2DOS_SW_CLUT=<levels> (16/32/64/128 for the fidelity sweep); unset = off.
#define SW_CLUT_MAX_LEVELS 128
#define SW_CLUT_SLOTS 4
static uint16_t sw_shade_clut[SW_CLUT_SLOTS][SW_CLUT_MAX_LEVELS][256];
static unsigned sw_clut_keys[SW_CLUT_SLOTS];
static int sw_clut_levels = -1;
static int sw_clut_next_slot;
static qboolean sw_clut_frame;  // cached once per frame (hot branches must not re-parse)

static qboolean sw_clut_enabled(void)
{
	if (sw_clut_levels < 0)
	{
		const char *spec = getenv("H2DOS_SW_CLUT");
		int v = spec != NULL ? atoi(spec) : 0;
		if (v != 16 && v != 32 && v != 64 && v != 128)
			v = 0;
		sw_clut_levels = v;
	}
	return sw_clut_levels >= 16;
}

static unsigned sw_clut_key_of(const float base_light[3])
{
	// quants at 1/2 intensity: dlight flicker stays inside a cell across
	// frames, so tables persist; cell coarseness shows as band-smooth light
	// snap, traded deliberately in the fidelity-vs-rebuild-storm decision.
	const int qr = ClampI((int)(base_light[0] * 2.0f), 0, 255);
	const int qg = ClampI((int)(base_light[1] * 2.0f), 0, 255);
	const int qb = ClampI((int)(base_light[2] * 2.0f), 0, 255);
	return ((unsigned)qr << 16) | ((unsigned)qg << 8) | (unsigned)qb;
}

static int sw_clut_build(const float base_light[3], const uint16_t pal565[256], const int levels, const int slot)
{
	// Exactness contract (mirrors the per-pixel block's integer math):
	//   current per-pixel: cr = lerped(vcolor = base*I) in [0..512] units,
	//   mr = ((texel_r * cr + 128) >> 8). Here cr = round(base_c*256) lives in
	//   the TABLE (per channel) and level==shade selects (cr x I): the LUT per
	//   level & index stores SW_RGB565(min(255,(texel*bl_q*shade+32768)>>16)).
	// At the quantization center, (base*I)*256 == bl_q*shade when I == shade/256.
	const int bl_q[3] = {
		min(512, max(0, (int)(base_light[0] * 256.0f + 0.5f))),
		min(512, max(0, (int)(base_light[1] * 256.0f + 0.5f))),
		min(512, max(0, (int)(base_light[2] * 256.0f + 0.5f))),
	};
	for (int level = 0; level < levels; ++level)
	{
		const int shade = (level * 512) / levels + (256 / levels);
		for (int idx = 0; idx < 256; ++idx)
		{
			const uint16_t texel = pal565[idx];
			const unsigned tr = (texel >> 8) & 0xF8u;
			const unsigned tg = (texel >> 3) & 0xFCu;
			const unsigned tb = (texel << 3) & 0xF8u;
			const unsigned mr = ((tr * (unsigned)bl_q[0] * (unsigned)shade + 32768u) >> 16);
			const unsigned mg = ((tg * (unsigned)bl_q[1] * (unsigned)shade + 32768u) >> 16);
			const unsigned mb = ((tb * (unsigned)bl_q[2] * (unsigned)shade + 32768u) >> 16);
			sw_shade_clut[slot][level][idx] = SW_RGB565(min(255u, mr), min(255u, mg), min(255u, mb));
		}
	}
	return slot;
}

static int sw_clut_cur;
static void sw_clut_select(const float base_light[3], const sw_image_t *skin)
{
	// Each m8 has its own palette, so the key must cover (light x skin row).
	const unsigned key = sw_clut_key_of(base_light) ^ (unsigned)(uintptr_t)skin;
	for (int i = 0; i < SW_CLUT_SLOTS; ++i)
		if (sw_clut_keys[i] == key)
		{
			sw_clut_cur = i;
			return;
		}
	const int slot = sw_clut_next_slot;
	sw_clut_next_slot = (sw_clut_next_slot + 1) & (SW_CLUT_SLOTS - 1);
	sw_clut_cur = sw_clut_build(base_light, skin->pal565, sw_clut_levels, slot);
	sw_clut_keys[slot] = key;
}

static void span_raster_flex_triangle(const flex_screen_vertex_t a, const flex_screen_vertex_t b,
	const flex_screen_vertex_t c, const sw_image_t *image, const qboolean depth_write, const qboolean additive)
{
	if (image == NULL || image->pixels == NULL)
		return;
	if ((a.x < 0.0f && b.x < 0.0f && c.x < 0.0f) || (a.x >= (float)sw_width && b.x >= (float)sw_width && c.x >= (float)sw_width) ||
		(a.y < 0.0f && b.y < 0.0f && c.y < 0.0f) || (a.y >= (float)sw_height && b.y >= (float)sw_height && c.y >= (float)sw_height))
	{
		++sw_stats.bench_flex_tris_bbox;
		return;
	}
	if (a.inverse_z <= 0.0f && b.inverse_z <= 0.0f && c.inverse_z <= 0.0f)
	{
		++sw_stats.bench_flex_tris_behind;
		return;
	}
	const float x10 = b.x - a.x, y10 = b.y - a.y;
	const float x20 = c.x - a.x, y20 = c.y - a.y;
	const float denom = x10 * y20 - x20 * y10;
	if (!isfinite(denom) || fabsf(denom) < 0.0001f)
		return;
	const float inv = 1.0f / denom;
	float guz_x, guz_y, gvz_x, gvz_y, gw_x, gw_y;
	float gr_x, gr_y, gg_x, gg_y, gb_x, gb_y, ga_x, ga_y;
#define SWF_GRAD(attr, ox, oy) do { \
		const float p10 = b.attr - a.attr; \
		const float p20 = c.attr - a.attr; \
		ox = (p10 * y20 - p20 * y10) * inv; \
		oy = (p20 * x10 - p10 * x20) * inv; \
	} while (0)
	SWF_GRAD(s_over_z, guz_x, guz_y);
	SWF_GRAD(t_over_z, gvz_x, gvz_y);
	SWF_GRAD(inverse_z, gw_x, gw_y);
	SWF_GRAD(red_over_z, gr_x, gr_y);
	SWF_GRAD(green_over_z, gg_x, gg_y);
	SWF_GRAD(blue_over_z, gb_x, gb_y);
	SWF_GRAD(alpha_over_z, ga_x, ga_y);
#undef SWF_GRAD

	const flex_screen_vertex_t *verts[3] = { &a, &b, &c };
	int top = 0, bottom = 0;
	for (int i = 1; i < 3; ++i)
	{
		if (verts[i]->y < verts[top]->y) top = i;
		if (verts[i]->y > verts[bottom]->y) bottom = i;
	}
	const qboolean no_depth_test = sw_sprite_no_depth_test;
	const int scan_first = ClampI((int)ceilf(verts[top]->y - 0.5f), 0, sw_height - 1);
	const int scan_last = ClampI((int)ceilf(verts[bottom]->y - 0.5f) - 1, 0, sw_height - 1);
	int left_prev = top, left_cur = top, right_prev = top, right_cur = top;
	float left_x = 0.0f, right_x = 0.0f, left_slope = 0.0f, right_slope = 0.0f;
	for (int y = scan_first; y <= scan_last; ++y)
	{
		++sw_stats.bench_flex_spans;
		const float y_center = y + 0.5f - a.y;
		const int old_left_prev = left_prev, old_right_prev = right_prev;
		while (left_cur != bottom && verts[left_cur]->y <= (y + 0.5f))
		{
			left_prev = left_cur;
			left_cur = (left_cur + 1) % 3;
		}
		while (right_cur != bottom && verts[right_cur]->y <= (y + 0.5f))
		{
			right_prev = right_cur;
			right_cur = (right_cur + 3 - 1) % 3;
		}
		const flex_screen_vertex_t *lp0 = verts[left_prev], *lp1 = verts[left_cur];
		const flex_screen_vertex_t *rp0 = verts[right_prev], *rp1 = verts[right_cur];
		if (left_prev != old_left_prev || y == scan_first)
		{
			left_slope = (lp1->y > lp0->y) ? (lp1->x - lp0->x) / (lp1->y - lp0->y) : 0.0f;
			left_x = lp0->x + (lp1->y > lp0->y ? ((y + 0.5f) - lp0->y) * left_slope : 0.0f);
		}
		else
			left_x += left_slope;
		if (right_prev != old_right_prev || y == scan_first)
		{
			right_slope = (rp1->y > rp0->y) ? (rp1->x - rp0->x) / (rp1->y - rp0->y) : 0.0f;
			right_x = rp0->x + (rp1->y > rp0->y ? ((y + 0.5f) - rp0->y) * right_slope : 0.0f);
		}
		else
			right_x += right_slope;
		int xa = ClampI((int)ceilf(min(left_x, right_x) - 0.5f), 0, sw_width - 1);
		int xb = ClampI((int)ceilf(max(left_x, right_x) - 0.5f) - 1, 0, sw_width - 1);
		if (xa > xb)
			continue;
		const float x_center = xa + 0.5f - a.x;
		float uz = a.s_over_z + guz_x * x_center + guz_y * y_center;
		float vz = a.t_over_z + gvz_x * x_center + gvz_y * y_center;
		float w = a.inverse_z + gw_x * x_center + gw_y * y_center;
		float rz = a.red_over_z + gr_x * x_center + gr_y * y_center;
		float gz = a.green_over_z + gg_x * x_center + gg_y * y_center;
		float bz = a.blue_over_z + gb_x * x_center + gb_y * y_center;
		float az = a.alpha_over_z + ga_x * x_center + ga_y * y_center;
		uint16_t *const fb_row = sw_color + (size_t)y * sw_width;
		uint32_t *const zb_row = sw_depth + (size_t)y * sw_width;
		int x = xa;
		int remaining = xb - xa + 1;
		while (remaining > 0)
		{
			// Tiny tris: most spans fit one block; merging blocks that a 16px
			// x-boundary would split cuts one conversion-heavy prologue. Step
			// quantization differences stay inside the fixed-point budget.
			const int block = min(remaining, SW_SPAN_STEP);
			++sw_stats.bench_flex_blocks;
			if (w <= 0.0f)
				break;
			const float w_end = w + gw_x * block;
			const float iz0 = 1.0f / w;
			const float iz1 = 1.0f / max(w_end, 1e-20f);
			const float u0 = uz * iz0, u1 = (uz + guz_x * block) * iz1;
			const float v0b = vz * iz0, v1b = (vz + gvz_x * block) * iz1;
			const float r0 = rz * iz0, r1 = (rz + gr_x * block) * iz1;
			const float g0 = gz * iz0, g1 = (gz + gg_x * block) * iz1;
			const float b0c = bz * iz0, b1c = (bz + gb_x * block) * iz1;
			const float a0c = az * iz0, a1c = (az + ga_x * block) * iz1;
			int iu = (int)(u0 * 65536.0f);
			int it = (int)(v0b * 65536.0f);
			int ir = (int)(r0 * 131072.0f);
			int ig = (int)(g0 * 131072.0f);
			int ib = (int)(b0c * 131072.0f);
			int ia = (int)(a0c * 131072.0f);
			const int du = (int)((u1 - u0) * 65536.0f * SW_INV_WIDTH(block));
			const int dt = (int)((v1b - v0b) * 65536.0f * SW_INV_WIDTH(block));
			const int dr = (int)((r1 - r0) * 131072.0f * SW_INV_WIDTH(block));
			const int dg = (int)((g1 - g0) * 131072.0f * SW_INV_WIDTH(block));
			const int db = (int)((b1c - b0c) * 131072.0f * SW_INV_WIDTH(block));
			const int da = (int)((a1c - a0c) * 131072.0f * SW_INV_WIDTH(block));
			uint32_t ikey = (uint32_t)(w * SW_DEPTH_SCALE);
			uint32_t ikey_end = (uint32_t)(max(w_end, 0.0f) * SW_DEPTH_SCALE);
			const int ikey_step = ((int)ikey_end - (int)ikey) / block;
			for (int i = 0; i < block; ++i, ++x)
			{
				++sw_stats.bench_pixels_flex;
				const size_t idx = (size_t)x;
				if (sw_dbg_tag_x == x && sw_dbg_tag_y == y)
					sw_ri.Con_Printf(PRINT_ALL, "tagxy flexspan bf=%u cat=%d key=%u zb=%u iu=%d it=%d\n",
						sw_stats.bench_frames, sw_bench_tri_category, ikey, zb_row[idx], iu >> 16, it >> 16);
				if (!no_depth_test && ikey < zb_row[idx])
					goto advance;
				{
					const int tx = ClampI(iu >> 16, 0, image->width - 1);
					const int ty = ClampI(it >> 16, 0, image->height - 1);
					const uint16_t texel = image->pixels[ty * image->width + tx];
					const int cr = min(512, max(0, ir >> 9)); // fixed [0..512]: 256 == 1.0
					const int cg = min(512, max(0, ig >> 9));
					const int cb = min(512, max(0, ib >> 9));
					const int ca = min(512, max(0, ia >> 9));
					const unsigned mr = ((((texel >> 8) & 0xF8u) * (unsigned)cr + 128u) >> 8);
					const unsigned mg = ((((texel >> 3) & 0xFCu) * (unsigned)cg + 128u) >> 8);
					const unsigned mb = ((((texel << 3) & 0xF8u) * (unsigned)cb + 128u) >> 8);
					// 565 colorkey: texel 0x0000 is transparent; otherwise alpha is the
					// old (255 * ca + 128) >> 8 scale.
					const int salpha = texel != 0 ? (int)min(255u, (255u * (unsigned)ca + 128u) >> 8) : 0;
					if (salpha == 0)
						goto advance;
					if (additive)
					{
						const uint16_t dst = fb_row[idx];
						const uint16_t fpx = SW_RGB565(
							min(255u, (min(255u, mr) * (unsigned)salpha + (((unsigned)dst >> 8) & 0xF8u) * 255u) / 255u),
							min(255u, (min(255u, mg) * (unsigned)salpha + (((unsigned)dst >> 3) & 0xFCu) * 255u) / 255u),
							min(255u, (min(255u, mb) * (unsigned)salpha + (((unsigned)dst << 3) & 0xF8u) * 255u) / 255u));
						sw_stats.bench_mag_flex += (fpx == 0xF81Fu);
						fb_row[idx] = fpx;
					}
					else if (salpha == 255)
					{
						const uint16_t fpx = SW_RGB565(min(255u, mr), min(255u, mg), min(255u, mb));
						sw_stats.bench_mag_flex += (fpx == 0xF81Fu);
						fb_row[idx] = fpx;
						if (depth_write && !no_depth_test)
						{
							zb_row[idx] = ikey;
							++sw_stats.depth_writes;
						}
					}
					else
					{
						const uint16_t dst = fb_row[idx];
						const unsigned dw = 255u - (unsigned)salpha;
						const uint16_t fpx = SW_RGB565(
							min(255u, (min(255u, mr) * (unsigned)salpha + (((unsigned)dst >> 8) & 0xF8u) * dw) / 255u),
							min(255u, (min(255u, mg) * (unsigned)salpha + (((unsigned)dst >> 3) & 0xFCu) * dw) / 255u),
							min(255u, (min(255u, mb) * (unsigned)salpha + (((unsigned)dst << 3) & 0xF8u) * dw) / 255u));
						sw_stats.bench_mag_flex += (fpx == 0xF81Fu);
						fb_row[idx] = fpx;
					}
				}
advance:
				(void)0;
				iu += du; it += dt; ir += dr; ig += dg; ib += db; ia += da; ikey += (uint32_t)ikey_step;
			}
			uz += guz_x * block;
			vz += gvz_x * block;
			rz += gr_x * block;
			gz += gg_x * block;
			bz += gb_x * block;
			az += ga_x * block;
			w = w_end;
			remaining -= block;
		}
	}
	++sw_stats.triangles;
	++sw_stats.flex_triangles;
}

// E55c: opaque fast path for skeleton-flex triangles, BIT-EXACT with the
// general span path wherever bits are observable: s/t/inverse_z and the
// gouraud r/g/b keep the identical per-block perspective float chain (same
// z-divided corner values, same endpoint math, same fixed-point conversion).
// Dropped: the alpha attribute entirely (entity is provably opaque solid —
// salpha evaluates to 255 deterministically), the colorkey blend branches
// (texel==0 test kept, semantics equal to salpha==0), and the per-triangle
// colors[][] rebuild (base_light*intensity premultiplied once per vertex —
// same two-operand product, bit-identical). Caller guarantees: entity
// opaque_depth && !additive, no per-node USE_COLOR, every corner in front of
// the near plane, skin non-NULL.
// E-A/ideas: exact universal flex shade LUT. Built from the exact scalar
// chain mr=(c*cr+128)>>8 clamped, so bit-identical per (texel,cr); the key
// is the lerped gouraud value itself (no light key to churn, no quant to
// drift) - sweep-verified offline over texel x (cr,cg,cb).
static byte sw_flex5lut[513][32];
static byte sw_flex6lut[513][64];
static qboolean sw_flexlut_ready;

extern uint32_t *sw_flex_skin_patch_sites[4];

__asm__(
	".data\n"
	".align 4\n"
	".globl _sw_flex_skin_patch_sites\n"
	"_sw_flex_skin_patch_sites:\n"
	".long .Lsw_flex_patch_width, .Lsw_flex_patch_pixels\n"
	".long .Lsw_flex_affine_patch_width, .Lsw_flex_affine_patch_pixels\n"
	".text\n");

static void sw_flex_patch_skin(const sw_image_t *image)
{
	static const uint16_t *patched_pixels;
	static int patched_width;
	if (patched_pixels == image->pixels && patched_width == image->width)
		return;
	*(volatile uint32_t *)sw_flex_skin_patch_sites[0] = (uint32_t)image->width;
	*(volatile uint32_t *)sw_flex_skin_patch_sites[1] = (uint32_t)(uintptr_t)image->pixels;
	*(volatile uint32_t *)sw_flex_skin_patch_sites[2] = (uint32_t)image->width;
	*(volatile uint32_t *)sw_flex_skin_patch_sites[3] = (uint32_t)(uintptr_t)image->pixels;
	patched_pixels = image->pixels;
	patched_width = image->width;
	__asm__ volatile("jmp 1f\n1:" ::: "memory");
}

static void sw_flex_lut_init(void)
{
	sw_flexlut_ready = true;
	for (int cr = 0; cr <= 512; ++cr)
	{
		for (int c = 0; c < 32; ++c)
		{
			unsigned v = (((unsigned)(c << 3)) * (unsigned)cr + 128u) >> 8;
			if (v > 255u) v = 255u;
			sw_flex5lut[cr][c] = (byte)((v & 0xF8u) >> 3);
		}
		for (int c = 0; c < 64; ++c)
		{
			unsigned v = (((unsigned)(c << 2)) * (unsigned)cr + 128u) >> 8;
			if (v > 255u) v = 255u;
			sw_flex6lut[cr][c] = (byte)((v & 0xFCu) >> 2);
		}
	}
}

#define SW_FLEX_MASK_CACHE_SIZE 256

typedef struct flex_mask_shape_s {
	unsigned key_plus_one;
	uint32_t coverage;
	byte source[25];
	byte width, height;
} flex_mask_shape_t;

typedef struct flex_mask_vertex_s {
	int x, y;
	uint32_t depth;
	uint16_t color;
	qboolean paints;
} flex_mask_vertex_t;

static flex_mask_shape_t sw_flex_mask_cache[SW_FLEX_MASK_CACHE_SIZE];
static float sw_flex_vcolor[SW_MAX_FLEX_VERTS][3];

static int flex_mask_orient(const flex_mask_vertex_t *a, const flex_mask_vertex_t *b,
	const int x, const int y)
{
	return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

static const flex_mask_shape_t *flex_mask_shape(const flex_mask_vertex_t vertices[3],
	const int origin_x, const int origin_y, const int width, const int height)
{
	int local_x[3], local_y[3];
	unsigned key = 0;
	for (int corner = 0; corner < 3; ++corner)
	{
		local_x[corner] = vertices[corner].x - origin_x;
		local_y[corner] = vertices[corner].y - origin_y;
		const unsigned point = (unsigned)(local_y[corner] * 6 + local_x[corner]);
		key = key * 36u + point;
	}
	flex_mask_shape_t *shape = &sw_flex_mask_cache[key & (SW_FLEX_MASK_CACHE_SIZE - 1)];
	if (shape->key_plus_one == key + 1u)
		return shape;
	shape->key_plus_one = key + 1u;
	shape->coverage = 0;
	shape->width = (byte)width;
	shape->height = (byte)height;
	flex_mask_vertex_t local[3] = { vertices[0], vertices[1], vertices[2] };
	for (int corner = 0; corner < 3; ++corner)
	{
		local[corner].x = local_x[corner];
		local[corner].y = local_y[corner];
	}
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			const int e0 = flex_mask_orient(&local[1], &local[2], x, y);
			const int e1 = flex_mask_orient(&local[2], &local[0], x, y);
			const int e2 = flex_mask_orient(&local[0], &local[1], x, y);
			if (e0 >= 0 && e1 >= 0 && e2 >= 0)
			{
				const int bit = y * 5 + x;
				shape->coverage |= 1u << bit;
				shape->source[bit] = (byte)(e0 >= e1 && e0 >= e2 ? 0 : (e1 >= e2 ? 1 : 2));
			}
		}
	return shape;
}

static qboolean flex_mask_opaque(const flex_proj_cache_t *projected[3],
	const sw_flex_rtri_t *triangle, const int vertex_indices[3],
	const sw_image_t *image)
{
	if (!sw_flexlut_ready)
		sw_flex_lut_init();
	flex_mask_vertex_t vertices[3];
	int minimum_x = INT_MAX, minimum_y = INT_MAX;
	int maximum_x = INT_MIN, maximum_y = INT_MIN;
	for (int corner = 0; corner < 3; ++corner)
	{
		const float x = projected[corner]->x;
		const float y = projected[corner]->y;
		vertices[corner].x = (int)(x + (x >= 0.0f ? 0.5f : -0.5f));
		vertices[corner].y = (int)(y + (y >= 0.0f ? 0.5f : -0.5f));
		minimum_x = min(minimum_x, vertices[corner].x);
		minimum_y = min(minimum_y, vertices[corner].y);
		maximum_x = max(maximum_x, vertices[corner].x);
		maximum_y = max(maximum_y, vertices[corner].y);
	}
	const int width = maximum_x - minimum_x;
	const int height = maximum_y - minimum_y;
	if (width < 1 || height < 1 || width > 5 || height > 5 ||
		flex_mask_orient(&vertices[0], &vertices[1], vertices[2].x, vertices[2].y) <= 0)
		return false;
	for (int corner = 0; corner < 3; ++corner)
	{
		const int vertex = vertex_indices[corner];
		const int tx = ClampI((int)(triangle->st[corner][0] * image->width), 0, image->width - 1);
		const int ty = ClampI((int)(triangle->st[corner][1] * image->height), 0, image->height - 1);
		const uint16_t texel = image->pixels[ty * image->width + tx];
		vertices[corner].paints = image->no_zero_texel || texel != 0;
		const int cr = min(512, max(0, (int)(sw_flex_vcolor[vertex][0] * 256.0f)));
		const int cg = min(512, max(0, (int)(sw_flex_vcolor[vertex][1] * 256.0f)));
		const int cb = min(512, max(0, (int)(sw_flex_vcolor[vertex][2] * 256.0f)));
		vertices[corner].color = (uint16_t)((sw_flex5lut[cr][(texel >> 11) & 31] << 11) |
			(sw_flex6lut[cg][(texel >> 5) & 63] << 5) | sw_flex5lut[cb][texel & 31]);
		vertices[corner].depth = (uint32_t)(projected[corner]->inverse_z * SW_DEPTH_SCALE);
	}
	const flex_mask_shape_t *shape = flex_mask_shape(vertices, minimum_x, minimum_y, width, height);
	for (int y = 0; y < shape->height; ++y)
		for (int x = 0; x < shape->width; ++x)
		{
			const int bit = y * 5 + x;
			if ((shape->coverage & (1u << bit)) == 0)
				continue;
			const flex_mask_vertex_t *source = &vertices[shape->source[bit]];
			const int screen_x = minimum_x + x;
			const int screen_y = minimum_y + y;
			if (!source->paints || screen_x < 0 || screen_x >= sw_width ||
				screen_y < 0 || screen_y >= sw_height)
				continue;
			const size_t index = (size_t)screen_y * sw_width + screen_x;
			++sw_stats.bench_pixels_flex;
			if (source->depth >= sw_depth[index])
			{
				sw_color[index] = source->color;
				sw_depth[index] = source->depth;
				++sw_stats.depth_writes;
			}
		}
	++sw_stats.triangles;
	++sw_stats.flex_triangles;
	++sw_stats.bench_flex_tris_mask;
	return true;
}

static qboolean sw_flex_affine_enabled(void)
{
	static int enabled = -1;
	if (enabled < 0)
	{
		const char *spec = getenv("H2DOS_SW_FLEX_AFFINE");
		enabled = spec == NULL || atoi(spec) != 0;
	}
	return enabled != 0;
}

static void span_raster_flex_opaque_affine(const flex_opaque_vertex_t a,
	const flex_opaque_vertex_t b, const flex_opaque_vertex_t c, const sw_image_t *image)
{
	if (!sw_flexlut_ready)
		sw_flex_lut_init();
	const float x10 = b.x - a.x, y10 = b.y - a.y;
	const float x20 = c.x - a.x, y20 = c.y - a.y;
	const float denom = x10 * y20 - x20 * y10;
	if (!isfinite(denom) || fabsf(denom) < 0.0001f)
		return;
	const float inv = 1.0f / denom;
	float gs_x, gs_y, gt_x, gt_y, gw_x, gw_y;
	float gr_x, gr_y, gg_x, gg_y, gb_x, gb_y;
#define SWA_GRAD(attr, ox, oy) do { \
		const float p10 = b.attr - a.attr; \
		const float p20 = c.attr - a.attr; \
		ox = (p10 * y20 - p20 * y10) * inv; \
		oy = (p20 * x10 - p10 * x20) * inv; \
	} while (0)
	SWA_GRAD(s, gs_x, gs_y);
	SWA_GRAD(t, gt_x, gt_y);
	SWA_GRAD(inverse_z, gw_x, gw_y);
	SWA_GRAD(red, gr_x, gr_y);
	SWA_GRAD(green, gg_x, gg_y);
	SWA_GRAD(blue, gb_x, gb_y);
#undef SWA_GRAD

	const flex_opaque_vertex_t *verts[3] = { &a, &b, &c };
	int top = 0, bottom = 0;
	for (int i = 1; i < 3; ++i)
	{
		if (verts[i]->y < verts[top]->y) top = i;
		if (verts[i]->y > verts[bottom]->y) bottom = i;
	}
	const int scan_first = ClampI((int)ceilf(verts[top]->y - 0.5f), 0, sw_height - 1);
	const int scan_last = ClampI((int)ceilf(verts[bottom]->y - 0.5f) - 1, 0, sw_height - 1);
	int left_prev = top, left_cur = top, right_prev = top, right_cur = top;
	float left_x = 0.0f, right_x = 0.0f, left_slope = 0.0f, right_slope = 0.0f;
	const int du = (int)(gs_x * 65536.0f);
	const int dt = (int)(gt_x * 65536.0f);
	const int dr = (int)(gr_x * 131072.0f);
	const int dg = (int)(gg_x * 131072.0f);
	const int db = (int)(gb_x * 131072.0f);
	const int ikey_step = (int)(gw_x * SW_DEPTH_SCALE);
	const qboolean has_key = !image->no_zero_texel;
	for (int y = scan_first; y <= scan_last; ++y)
	{
		++sw_stats.bench_flex_spans;
		const float y_center = y + 0.5f - a.y;
		const int old_left_prev = left_prev, old_right_prev = right_prev;
		while (left_cur != bottom && verts[left_cur]->y <= (y + 0.5f))
		{
			left_prev = left_cur;
			left_cur = (left_cur + 1) % 3;
		}
		while (right_cur != bottom && verts[right_cur]->y <= (y + 0.5f))
		{
			right_prev = right_cur;
			right_cur = (right_cur + 2) % 3;
		}
		const flex_opaque_vertex_t *lp0 = verts[left_prev], *lp1 = verts[left_cur];
		const flex_opaque_vertex_t *rp0 = verts[right_prev], *rp1 = verts[right_cur];
		if (left_prev != old_left_prev || y == scan_first)
		{
			left_slope = lp1->y > lp0->y ? (lp1->x - lp0->x) / (lp1->y - lp0->y) : 0.0f;
			left_x = lp0->x + (lp1->y > lp0->y ? ((y + 0.5f) - lp0->y) * left_slope : 0.0f);
		}
		else
			left_x += left_slope;
		if (right_prev != old_right_prev || y == scan_first)
		{
			right_slope = rp1->y > rp0->y ? (rp1->x - rp0->x) / (rp1->y - rp0->y) : 0.0f;
			right_x = rp0->x + (rp1->y > rp0->y ? ((y + 0.5f) - rp0->y) * right_slope : 0.0f);
		}
		else
			right_x += right_slope;
		const int xa = ClampI((int)ceilf(min(left_x, right_x) - 0.5f), 0, sw_width - 1);
		const int xb = ClampI((int)ceilf(max(left_x, right_x) - 0.5f) - 1, 0, sw_width - 1);
		if (xa > xb)
			continue;
		++sw_stats.bench_flex_blocks;
		const float x_center = xa + 0.5f - a.x;
		int iu = (int)((a.s + gs_x * x_center + gs_y * y_center) * 65536.0f);
		int it = (int)((a.t + gt_x * x_center + gt_y * y_center) * 65536.0f);
		int ir = (int)((a.red + gr_x * x_center + gr_y * y_center) * 131072.0f);
		int ig = (int)((a.green + gg_x * x_center + gg_y * y_center) * 131072.0f);
		int ib = (int)((a.blue + gb_x * x_center + gb_y * y_center) * 131072.0f);
		uint32_t ikey = (uint32_t)((a.inverse_z + gw_x * x_center + gw_y * y_center) * SW_DEPTH_SCALE);
		uint16_t *const fb_row = sw_color + (size_t)y * sw_width;
		uint32_t *const zb_row = sw_depth + (size_t)y * sw_width;
		const int count = xb - xa + 1;
		const int iu_last = iu + (count - 1) * du;
		const int it_last = it + (count - 1) * dt;
		const qboolean st_in = min(iu, iu_last) >= 0 &&
			max(iu, iu_last) <= ((image->width - 1) << 16) &&
			min(it, it_last) >= 0 && max(it, it_last) <= ((image->height - 1) << 16);
		for (int x = xa; x <= xb; ++x)
		{
			++sw_stats.bench_pixels_flex;
			const size_t idx = (size_t)x;
			if (ikey >= zb_row[idx])
			{
				uint16_t texel;
				if (st_in)
				{
					uint32_t sample_u = (uint32_t)iu;
					uint32_t sample_v = (uint32_t)it;
					__asm__ volatile(
						".intel_syntax noprefix\n"
						"sar ecx, 16\n"
						".byte 0x69, 0xc9\n"
						".Lsw_flex_affine_patch_width: .long 0x12345678\n"
						"sar eax, 16\n"
						"add eax, ecx\n"
						".byte 0x0f, 0xb7, 0x04, 0x45\n"
						".Lsw_flex_affine_patch_pixels: .long 0x12345678\n"
						".att_syntax prefix\n"
						: "+a"(sample_u), "+c"(sample_v) : : "memory");
					texel = (uint16_t)sample_u;
				}
				else
				{
					const int tx = ClampI(iu >> 16, 0, image->width - 1);
					const int ty = ClampI(it >> 16, 0, image->height - 1);
					texel = image->pixels[ty * image->width + tx];
				}
				if (!has_key || texel != 0)
				{
					const int cr = min(512, max(0, ir >> 9));
					const int cg = min(512, max(0, ig >> 9));
					const int cb = min(512, max(0, ib >> 9));
					const uint16_t fpx = (uint16_t)((sw_flex5lut[cr][(texel >> 11) & 31] << 11) |
						(sw_flex6lut[cg][(texel >> 5) & 63] << 5) | sw_flex5lut[cb][texel & 31]);
					sw_stats.bench_mag_flex += fpx == 0xF81Fu;
					fb_row[idx] = fpx;
					zb_row[idx] = ikey;
					++sw_stats.depth_writes;
				}
			}
			iu += du; it += dt; ir += dr; ig += dg; ib += db; ikey += (uint32_t)ikey_step;
		}
	}
	++sw_stats.triangles;
	++sw_stats.flex_triangles;
	++sw_stats.bench_flex_tris_affine;
}

static void span_raster_flex_opaque(const flex_screen_vertex_t a, const flex_screen_vertex_t b,
	const flex_screen_vertex_t c, const sw_image_t *image)
{
	if (!sw_flexlut_ready)
		sw_flex_lut_init();
	const qboolean clut = sw_clut_frame && image->src8 != NULL;
	const float x10 = b.x - a.x, y10 = b.y - a.y;
	const float x20 = c.x - a.x, y20 = c.y - a.y;
	const float denom = x10 * y20 - x20 * y10;
	if (!isfinite(denom) || fabsf(denom) < 0.0001f)
		return;
	const float inv = 1.0f / denom;
	float guz_x, guz_y, gvz_x, gvz_y, gw_x, gw_y;
	float gr_x, gr_y, gg_x, gg_y, gb_x, gb_y;
	float gi_x, gi_y;
#define SWO_GRAD(attr, ox, oy) do { \
		const float p10 = b.attr - a.attr; \
		const float p20 = c.attr - a.attr; \
		ox = (p10 * y20 - p20 * y10) * inv; \
		oy = (p20 * x10 - p10 * x20) * inv; \
	} while (0)
	SWO_GRAD(s_over_z, guz_x, guz_y);
	SWO_GRAD(t_over_z, gvz_x, gvz_y);
	SWO_GRAD(inverse_z, gw_x, gw_y);
	if (clut)
		SWO_GRAD(intensity_over_z, gi_x, gi_y);
	else
	{
		SWO_GRAD(red_over_z, gr_x, gr_y);
		SWO_GRAD(green_over_z, gg_x, gg_y);
		SWO_GRAD(blue_over_z, gb_x, gb_y);
	}
#undef SWO_GRAD

	const flex_screen_vertex_t *verts[3] = { &a, &b, &c };
	int top = 0, bottom = 0;
	for (int i = 1; i < 3; ++i)
	{
		if (verts[i]->y < verts[top]->y) top = i;
		if (verts[i]->y > verts[bottom]->y) bottom = i;
	}
	const int scan_first = ClampI((int)ceilf(verts[top]->y - 0.5f), 0, sw_height - 1);
	const int scan_last = ClampI((int)ceilf(verts[bottom]->y - 0.5f) - 1, 0, sw_height - 1);
	int left_prev = top, left_cur = top, right_prev = top, right_cur = top;
	float left_x = 0.0f, right_x = 0.0f, left_slope = 0.0f, right_slope = 0.0f;
	for (int y = scan_first; y <= scan_last; ++y)
	{
		++sw_stats.bench_flex_spans;
		const float y_center = y + 0.5f - a.y;
		const int old_left_prev = left_prev, old_right_prev = right_prev;
		while (left_cur != bottom && verts[left_cur]->y <= (y + 0.5f))
		{
			left_prev = left_cur;
			left_cur = (left_cur + 1) % 3;
		}
		while (right_cur != bottom && verts[right_cur]->y <= (y + 0.5f))
		{
			right_prev = right_cur;
			right_cur = (right_cur + 3 - 1) % 3;
		}
		const flex_screen_vertex_t *lp0 = verts[left_prev], *lp1 = verts[left_cur];
		const flex_screen_vertex_t *rp0 = verts[right_prev], *rp1 = verts[right_cur];
		if (left_prev != old_left_prev || y == scan_first)
		{
			left_slope = (lp1->y > lp0->y) ? (lp1->x - lp0->x) / (lp1->y - lp0->y) : 0.0f;
			left_x = lp0->x + (lp1->y > lp0->y ? ((y + 0.5f) - lp0->y) * left_slope : 0.0f);
		}
		else
			left_x += left_slope;
		if (right_prev != old_right_prev || y == scan_first)
		{
			right_slope = (rp1->y > rp0->y) ? (rp1->x - rp0->x) / (rp1->y - rp0->y) : 0.0f;
			right_x = rp0->x + (rp1->y > rp0->y ? ((y + 0.5f) - rp0->y) * right_slope : 0.0f);
		}
		else
			right_x += right_slope;
		const int xa = ClampI((int)ceilf(min(left_x, right_x) - 0.5f), 0, sw_width - 1);
		const int xb = ClampI((int)ceilf(max(left_x, right_x) - 0.5f) - 1, 0, sw_width - 1);
		if (xa > xb)
			continue;
		const float x_center = xa + 0.5f - a.x;
		float uz = a.s_over_z + guz_x * x_center + guz_y * y_center;
		float vz = a.t_over_z + gvz_x * x_center + gvz_y * y_center;
		float w = a.inverse_z + gw_x * x_center + gw_y * y_center;
		float rz = clut ? (a.intensity_over_z + gi_x * x_center + gi_y * y_center)
				: (a.red_over_z + gr_x * x_center + gr_y * y_center);
		float gz = a.green_over_z + gg_x * x_center + gg_y * y_center;
		float bz = a.blue_over_z + gb_x * x_center + gb_y * y_center;
		uint16_t *const fb_row = sw_color + (size_t)y * sw_width;
		uint32_t *const zb_row = sw_depth + (size_t)y * sw_width;
		int x = xa;
		int remaining = xb - xa + 1;
		while (remaining > 0)
		{
			const int block = min(remaining, SW_SPAN_STEP);
			++sw_stats.bench_flex_blocks;
			if (w <= 0.0f)
				break;
			const float w_end = w + gw_x * block;
			const float iz0 = 1.0f / w;
			const float iz1 = 1.0f / max(w_end, 1e-20f);
			const float u0 = uz * iz0, u1 = (uz + guz_x * block) * iz1;
			const float v0b = vz * iz0, v1b = (vz + gvz_x * block) * iz1;
			const float r0 = rz * iz0, r1 = (rz + (clut ? gi_x : gr_x) * block) * iz1;
			const float g0 = gz * iz0, g1 = (gz + gg_x * block) * iz1;
			const float b0c = bz * iz0, b1c = (bz + gb_x * block) * iz1;
			int iu = (int)(u0 * 65536.0f);
			int it = (int)(v0b * 65536.0f);
			int ir = (int)(r0 * 131072.0f);
			int ig = (int)(g0 * 131072.0f);
			int ib = (int)(b0c * 131072.0f);
			const int du = (int)((u1 - u0) * 65536.0f * SW_INV_WIDTH(block));
			const int dt = (int)((v1b - v0b) * 65536.0f * SW_INV_WIDTH(block));
			const int dr = (int)((r1 - r0) * 131072.0f * SW_INV_WIDTH(block));
			const int dg = (int)((g1 - g0) * 131072.0f * SW_INV_WIDTH(block));
			const int db = (int)((b1c - b0c) * 131072.0f * SW_INV_WIDTH(block));
			uint32_t ikey = (uint32_t)(w * SW_DEPTH_SCALE);
			uint32_t ikey_end = (uint32_t)(max(w_end, 0.0f) * SW_DEPTH_SCALE);
			const int ikey_step = ((int)ikey_end - (int)ikey) / block;
			// E-D/ideas: u,v are affine over the block => px values stay inside
			// [min(iu,iu_end)..max(...)]; monotonicity makes the per-px clamps
			// redundant when BOTH block endpoints are in-texture range.
			const int iu_last = iu + (block - 1) * du;
			const int it_last = it + (block - 1) * dt;
			const qboolean su_in = min(iu, iu_last) >= 0 &&
				max(iu, iu_last) <= ((image->width - 1) << 16);
			const qboolean st_in = min(it, it_last) >= 0 &&
				max(it, it_last) <= ((image->height - 1) << 16);
			const qboolean has_key = !image->no_zero_texel;
			if (clut)
				for (int i = 0; i < block; ++i, ++x)
				{
					++sw_stats.bench_pixels_flex;
					const size_t idx = (size_t)x;
					if (ikey >= zb_row[idx])
					{
						const int tx = ClampI(iu >> 16, 0, image->width - 1);
						const int ty = ClampI(it >> 16, 0, image->height - 1);
						const byte s8 = image->src8[ty * image->width + tx];
						if (s8 != 255)
						{
							// cr = ir>>9 spans [0..512] (256==1.0); level = cr in units of
							// (512/levels): index = (cr * levels) / 512 = (ir * levels) >> 18.
							const int level = min(sw_clut_levels - 1, max(0, (ir * sw_clut_levels) >> 18));
							const uint16_t fpx = sw_shade_clut[sw_clut_cur][level][s8];
							if (sw_dbg_tag_x == x && sw_dbg_tag_y == y)
								sw_ri.Con_Printf(PRINT_ALL,
									"tagxy clut bf=%u ir=%d lvl=%d s8=%d fpx=%04x pal=%04x blkey=%u\n",
									sw_stats.bench_frames, ir, level, s8, fpx, image->pal565[s8], sw_clut_keys ? sw_clut_keys[sw_clut_cur] : 0u);
							sw_stats.bench_mag_flex += (fpx == 0xF81Fu);
							fb_row[idx] = fpx;
							zb_row[idx] = ikey;
							++sw_stats.depth_writes;
						}
					}
					iu += du; it += dt; ir += dr; ig += dg; ib += db; ikey += (uint32_t)ikey_step;
				}
			else
				for (int i = 0; i < block; ++i, ++x)
				{
					++sw_stats.bench_pixels_flex;
					const size_t idx = (size_t)x;
					if (ikey >= zb_row[idx])
					{
						uint16_t texel;
						if (su_in && st_in)
						{
							uint32_t sample_u = (uint32_t)iu;
							uint32_t sample_v = (uint32_t)it;
							__asm__ volatile(
								".intel_syntax noprefix\n"
								"sar ecx, 16\n"
								".byte 0x69, 0xc9\n"
								".Lsw_flex_patch_width: .long 0x12345678\n"
								"sar eax, 16\n"
								"add eax, ecx\n"
								".byte 0x0f, 0xb7, 0x04, 0x45\n"
								".Lsw_flex_patch_pixels: .long 0x12345678\n"
								".att_syntax prefix\n"
								: "+a"(sample_u), "+c"(sample_v) : : "memory");
							texel = (uint16_t)sample_u;
						}
						else
						{
							const int tx = ClampI(iu >> 16, 0, image->width - 1);
							const int ty = ClampI(it >> 16, 0, image->height - 1);
							texel = image->pixels[ty * image->width + tx];
						}
						// texel 0x0000 is the colorkey: identical semantics to the
						// salpha==0 branch (no paint, no depth write).
						if (!has_key || texel != 0)
						{
							const int cr = min(512, max(0, ir >> 9)); // fixed [0..512]: 256 == 1.0
							const int cg = min(512, max(0, ig >> 9));
							const int cb = min(512, max(0, ib >> 9));
							const uint16_t fpx = (uint16_t)((sw_flex5lut[cr][(texel >> 11) & 31] << 11) |
								(sw_flex6lut[cg][(texel >> 5) & 63] << 5) | sw_flex5lut[cb][texel & 31]);
							sw_stats.bench_mag_flex += (fpx == 0xF81Fu);
							fb_row[idx] = fpx;
							zb_row[idx] = ikey;
							++sw_stats.depth_writes;
						}
					}
					iu += du; it += dt; ir += dr; ig += dg; ib += db; ikey += (uint32_t)ikey_step;
				}
			uz += guz_x * block;
			vz += gvz_x * block;
			rz += gr_x * block;
			gz += gg_x * block;
			bz += gb_x * block;
			w = w_end;
			remaining -= block;
		}
	}
	++sw_stats.triangles;
	++sw_stats.flex_triangles;
	++sw_stats.bench_flex_tris_affine;
}

static void raster_flex_triangle(const flex_screen_vertex_t a, const flex_screen_vertex_t b,
	const flex_screen_vertex_t c, const sw_image_t *image, const qboolean depth_write, const qboolean additive)
{
	if (image == NULL || !isfinite(a.x) || !isfinite(a.y) || !isfinite(b.x) || !isfinite(b.y) ||
		!isfinite(c.x) || !isfinite(c.y))
		return;
	const float area = flex_edge(a, b, c.x, c.y);
	if (!isfinite(area) || fabsf(area) < 0.0001f)
		return;
	const float inverse_area = 1.0f / area;
	const int min_x = ClampI((int)floorf(min(a.x, min(b.x, c.x))), 0, sw_width - 1);
	const int max_x = ClampI((int)ceilf(max(a.x, max(b.x, c.x))), 0, sw_width - 1);
	const int min_y = ClampI((int)floorf(min(a.y, min(b.y, c.y))), 0, sw_height - 1);
	const int max_y = ClampI((int)ceilf(max(a.y, max(b.y, c.y))), 0, sw_height - 1);
	unsigned *const bench_walked = sw_bench_tri_category == 2 ? &sw_stats.bench_walked_sprite :
		sw_bench_tri_category == 1 ? &sw_stats.bench_walked_flex : &sw_stats.bench_walked_world;
	unsigned *const bench_pixels = sw_bench_tri_category == 2 ? &sw_stats.bench_pixels_sprite :
		sw_bench_tri_category == 1 ? &sw_stats.bench_pixels_flex : &sw_stats.bench_pixels_other;
	*bench_walked += (unsigned)((max_x - min_x + 1) * (max_y - min_y + 1));
	for (int y = min_y; y <= max_y; ++y)
		for (int x = min_x; x <= max_x; ++x)
		{
			const float px = x + 0.5f;
			const float py = y + 0.5f;
			const float wa = flex_edge(b, c, px, py) * inverse_area;
			const float wb = flex_edge(c, a, px, py) * inverse_area;
			const float wc = flex_edge(a, b, px, py) * inverse_area;
			if (wa < 0.0f || wb < 0.0f || wc < 0.0f)
				continue;
			++*bench_pixels;
			const float inverse_z = wa * a.inverse_z + wb * b.inverse_z + wc * c.inverse_z;
			if (inverse_z <= 0.0f)
				continue;
			const float z = 1.0f / inverse_z;
			const float s = (wa * a.s_over_z + wb * b.s_over_z + wc * c.s_over_z) * z;
			const float t = (wa * a.t_over_z + wb * b.t_over_z + wc * c.t_over_z) * z;
			const float red = (wa * a.red_over_z + wb * b.red_over_z + wc * c.red_over_z) * z;
			const float green = (wa * a.green_over_z + wb * b.green_over_z + wc * c.green_over_z) * z;
			const float blue = (wa * a.blue_over_z + wb * b.blue_over_z + wc * c.blue_over_z) * z;
			const float alpha = (wa * a.alpha_over_z + wb * b.alpha_over_z + wc * c.alpha_over_z) * z;
			const int tx = ClampI((int)floorf(s), 0, image->width - 1);
			const int ty = ClampI((int)floorf(t), 0, image->height - 1);
			byte sprite_alpha;
			const uint16_t color = modulate(image->pixels[ty * image->width + tx], red, green, blue, alpha, &sprite_alpha);
			if (sprite_alpha != 0)
				SW_PutPixel(x, y, sw_sprite_no_depth_test ? FLT_MAX : inverse_z, color, sprite_alpha,
					depth_write && sprite_alpha == 255 && !sw_sprite_no_depth_test, additive);
		}
	++sw_stats.triangles;
	++sw_stats.flex_triangles;
}

// Project flex vertex index `vertex` (world position in lerped[vertex]) once
// and cache the screen coordinates plus inverse-z for every incident triangle
// of this entity. Returns false when the vertex is behind the near plane, in
// which case the caller must fall back to the general clipped path.
// E9/ideas#9: pointer-returning lookup — callers only read the slot, so
// skip the 45-byte *out = *cache copy per corner (~160KB/frame at 3,970
// lookups). Returns NULL for z < SW_NEAR_CLIP; callers never read the slot
// in that case (the clip path rebuilds corners via entity_transform_basis).
static const flex_proj_cache_t *flex_proj_lookup(const entity_t *entity, const vec3_t lerped[], const float m[3][3],
	const float t[3], const int vertex)
{
	flex_proj_cache_t *cache = &sw_flex_proj_cache[vertex];
	if (cache->entity_seq != sw_flex_proj_cache_entity_seq)
	{
		// E7: one fused View x Entity matrix per entity-frame; 9 mul + 9 add per
		// vertex fill (was entity_transform_basis + world_to_view: 21 mul + 18 add).
		sw_vertex_t view;
		view.x = m[0][0] * lerped[vertex][0] + m[0][1] * lerped[vertex][1] + m[0][2] * lerped[vertex][2] + t[0];
		view.y = m[1][0] * lerped[vertex][0] + m[1][1] * lerped[vertex][1] + m[1][2] * lerped[vertex][2] + t[1];
		view.z = m[2][0] * lerped[vertex][0] + m[2][1] * lerped[vertex][1] + m[2][2] * lerped[vertex][2] + t[2];
		(void)entity;
		const float w = 1.0f / view.z;
		cache->x = sw_refdef.x + sw_refdef.width * 0.5f + view.x * projection_x * w;
		cache->y = sw_refdef.y + sw_refdef.height * 0.5f - view.y * projection_y * w;
		cache->z = view.z;
		cache->inverse_z = w;
		cache->entity_seq = sw_flex_proj_cache_entity_seq;
	}
	return cache->z >= SW_NEAR_CLIP ? cache : NULL;
}

static void dispatch_flex_screen_triangle(const flex_screen_vertex_t a, const flex_screen_vertex_t b,
	const flex_screen_vertex_t c, const sw_image_t *image, const qboolean depth_write, const qboolean additive);

static void draw_flex_triangle(const vec3_t points[3], const float uv[3][2], const float colors[3][4],
	const sw_image_t *image, const qboolean depth_write, const qboolean additive)
{
	flex_raster_vertex_t input[3];
	for (int i = 0; i < 3; ++i)
	{
		const sw_vertex_t view = world_to_view(points[i], uv[i][0] * image->width, uv[i][1] * image->height);
		input[i].x = view.x;
		input[i].y = view.y;
		input[i].z = view.z;
		input[i].s = view.s;
		input[i].t = view.t;
		input[i].red = colors[i][0];
		input[i].green = colors[i][1];
		input[i].blue = colors[i][2];
		input[i].alpha = colors[i][3];
	}
	flex_raster_vertex_t clipped[5];
	const int count = clip_flex_near(input, clipped);
	if (count < 3)
		return;
	const flex_screen_vertex_t first = project_flex(clipped[0]);
	for (int i = 1; i + 1 < count; ++i)
		dispatch_flex_screen_triangle(first, project_flex(clipped[i]), project_flex(clipped[i + 1]),
			image, depth_write, additive);
}

static void dispatch_flex_screen_triangle(const flex_screen_vertex_t a, const flex_screen_vertex_t b,
	const flex_screen_vertex_t c, const sw_image_t *image, const qboolean depth_write, const qboolean additive)
{
	static cvar_t *sw_spans;
	sw_dbg_tag_src = sw_bench_tri_category == 2 ? -3 : -2;
	if (sw_dbg_tag_x >= 0 && sw_dbg_tag_y >= 0)
	{
		const float px = sw_dbg_tag_x + 0.5f, py = sw_dbg_tag_y + 0.5f;
		const float e0 = flex_edge(a, b, px, py);
		const float e1 = flex_edge(b, c, px, py);
		const float e2 = flex_edge(c, a, px, py);
		if (!((e0 < 0.0f || e1 < 0.0f || e2 < 0.0f) && (e0 > 0.0f || e1 > 0.0f || e2 > 0.0f)))
			sw_ri.Con_Printf(PRINT_ALL, "tagxy %s bf=%u img=%s iz=(%g %g %g) dw=%d add=%d\n",
				sw_bench_tri_category == 2 ? "sprite" : "flex", sw_stats.bench_frames,
				image != NULL ? image->name : "?", a.inverse_z, b.inverse_z, c.inverse_z,
				(int)depth_write, (int)additive);
	}
	if (sw_spans == NULL)
		sw_spans = sw_ri.Cvar_Get("r_swspans", "1", 0);
	if (sw_spans != NULL && sw_spans->value != 0.0f && !sw_dbg_nospan)
		span_raster_flex_triangle(a, b, c, image, depth_write, additive);
	else
		raster_flex_triangle(a, b, c, image, depth_write, additive);
}

static const char *book_dbg_pat;
static qboolean book_dbg_ready;
static qboolean book_dbg_on(void)
{
	if (!book_dbg_ready)
	{
		book_dbg_ready = true;
		const char *s = getenv("H2DOS_DBG_MODELSKIN");
		if (s != NULL && *s != 0)
			book_dbg_pat = s;
	}
	return book_dbg_pat != NULL;
}

static qboolean flex_model_frame_valid(const sw_model_t *model, const int frame)
{
	if (model->frame_remap != NULL)
		return frame >= 0 && frame < model->frame_remap_count;
	return frame >= 0 && frame < model->flex->header.num_frames;
}

static int flex_model_frame_index(const sw_model_t *model, const int frame)
{
	if (!flex_model_frame_valid(model, frame))
		return model->frame_remap != NULL ? model->frame_remap[0] : 0;
	return model->frame_remap != NULL ? model->frame_remap[frame] : frame;
}

void SW_RasterFlexEntity(const entity_t *entity, const sw_model_t *model, const qboolean shadow_pass)
{
	static vec3_t lerped[SW_MAX_FLEX_VERTS];
	static float intensities[SW_MAX_FLEX_VERTS];
	// E2 (ideas #2, pm-mca baseline #75): byte vertex components go through a
	// byte->float LUT instead of a store->FILD chain per access (34/89 clk of
	// the gradient loop were fp-operand stalls on that serialization). Exact
	// 0..255 integers in fp32 => bit-identical to FILD conversion.
	static float sw_byte2float[256];
	static qboolean sw_byte2float_ready = false;
	if (!sw_byte2float_ready)
	{
		for (int i = 0; i < 256; ++i)
			sw_byte2float[i] = (float)i;
		sw_byte2float_ready = true;
	}
	const uclock_t prologue_start = uclock();
	if (entity == NULL || model == NULL || model->flex == NULL || (entity->flags & RF_NODRAW) ||
		!isfinite(entity->scale) || entity->scale <= 0.0f || !isfinite(entity->backlerp) ||
		!isfinite(entity->angles[0]) || !isfinite(entity->angles[1]) || !isfinite(entity->angles[2]))
		return;
	const sw_flex_t *flex = model->flex;
	const int frame_index = flex_model_frame_index(model, entity->frame);
	const int old_frame_index = flex_model_frame_index(model, entity->oldframe);
	const sw_flex_frame_t *frame = &flex->frames[frame_index];
	const sw_flex_frame_t *old_frame = &flex->frames[old_frame_index];
	const float back_lerp = min(1.0f, max(0.0f, entity->backlerp));
	const float front_lerp = 1.0f - back_lerp;
	const float scale = entity->scale;
	float move[3];
	float front[3];
	float back[3];
	for (int axis = 0; axis < 3; ++axis)
	{
		move[axis] = (front_lerp * frame->translate[axis] + back_lerp * old_frame->translate[axis]) * scale;
		front[axis] = front_lerp * frame->scale[axis] * scale;
		back[axis] = back_lerp * old_frame->scale[axis] * scale;
	}
	const int shade_row = ((int)(entity->angles[1] * (SW_SHADEDOT_QUANT / (2.0f * (float)M_PI))) & (SW_SHADEDOT_QUANT - 1));
	for (int vertex = 0; vertex < flex->render_num_xyz; ++vertex)
	{
		for (int axis = 0; axis < 3; ++axis)
			lerped[vertex][axis] = sw_byte2float[frame->vertices[vertex].v[axis]] * front[axis] +
				sw_byte2float[old_frame->vertices[vertex].v[axis]] * back[axis] + move[axis];
		const float current = r_avertexnormal_dots[shade_row][frame->vertices[vertex].lightnormalindex];
		const float old = r_avertexnormal_dots[shade_row][old_frame->vertices[vertex].lightnormalindex];
		intensities[vertex] = current * front_lerp + old * back_lerp;
	}
	Placement_t current_joints[SW_MAX_FLEX_JOINTS];
	matrix3_t current_rotations[SW_MAX_FLEX_JOINTS];
	int current_joint_indices[SW_MAX_FLEX_JOINTS] = { 0 };
	qboolean have_rotations = false;
	if (flex->num_clusters > 0 && flex->skeleton_placements != NULL)
	{
		for (int joint = 0; joint < flex->num_clusters; ++joint)
			interpolate_flex_placement(&flex->skeleton_placements[frame_index * flex->num_clusters + joint],
				&flex->skeleton_placements[old_frame_index * flex->num_clusters + joint], move, front, back, &current_joints[joint]);
		have_rotations = make_flex_rotations(entity, flex->num_clusters, current_joints,
			current_rotations, current_joint_indices);
		if (have_rotations)
			rotate_flex_clusters(flex, current_joints, current_rotations, lerped);
	}
	sw_stats.bench_flex_vertex_lerps += (unsigned)flex->render_num_xyz;
	sw_stats.bench_us_flex_prologue += (unsigned long long)(uclock() - prologue_start);
	const float cr = cosf(-entity->angles[2]);
	const float sr = sinf(-entity->angles[2]);
	const float cp = cosf(-entity->angles[0]);
	const float sp = sinf(-entity->angles[0]);
	const float cy = cosf(entity->angles[1]);
	const float sy = sinf(entity->angles[1]);
	float ent_view_m[3][3];
	float ent_view_t[3];
	flex_fused_view_matrix(cr, sr, cp, sp, cy, sy, entity->origin, ent_view_m, ent_view_t);
	flex_proj_cache_reset();
	const uclock_t swaprefs_start = uclock();
	qboolean use_swap = flex->num_clusters > 0 && entity->swapFrame != NO_SWAP_FRAME &&
		flex_model_frame_valid(model, entity->swapFrame);
	const int swap_frame_index = flex_model_frame_index(model, entity->swapFrame);
	const int old_swap_frame_index = flex_model_frame_index(model, entity->oldSwapFrame);
	Placement_t swap_joints[SW_MAX_FLEX_JOINTS];
	matrix3_t swap_rotations[SW_MAX_FLEX_JOINTS];
	int swap_joint_indices[SW_MAX_FLEX_JOINTS] = { 0 };
	if (use_swap)
	{
		const sw_flex_frame_t *swap_frame = &flex->frames[swap_frame_index];
		const sw_flex_frame_t *old_swap_frame = &flex->frames[old_swap_frame_index];
		float swap_move[3];
		float swap_front[3];
		float swap_back[3];
		for (int axis = 0; axis < 3; ++axis)
		{
			swap_move[axis] = (front_lerp * swap_frame->translate[axis] + back_lerp * old_swap_frame->translate[axis]) * scale;
			swap_front[axis] = front_lerp * swap_frame->scale[axis] * scale;
			swap_back[axis] = back_lerp * old_swap_frame->scale[axis] * scale;
		}
		for (int vertex = 0; vertex < flex->cluster_counts[0]; ++vertex)
		{
			const int index = flex->cluster_vertices[0][vertex];
			for (int axis = 0; axis < 3; ++axis)
				lerped[index][axis] = sw_byte2float[swap_frame->vertices[index].v[axis]] * swap_front[axis] +
					sw_byte2float[old_swap_frame->vertices[index].v[axis]] * swap_back[axis] + swap_move[axis];
		}
		for (int joint = 0; joint < flex->num_clusters; ++joint)
			interpolate_flex_placement(&flex->skeleton_placements[swap_frame_index * flex->num_clusters + joint],
				&flex->skeleton_placements[old_swap_frame_index * flex->num_clusters + joint], swap_move, swap_front, swap_back, &swap_joints[joint]);
		if (make_flex_rotations(entity, flex->num_clusters, swap_joints, swap_rotations, swap_joint_indices))
		{
			rotate_flex_clusters(flex, swap_joints, swap_rotations, lerped);
			for (int vertex = 0; vertex < flex->cluster_counts[0]; ++vertex)
			{
				const int index = flex->cluster_vertices[0][vertex];
				VectorSubtract(lerped[index], swap_joints[0].origin, lerped[index]);
				VectorAdd(lerped[index], current_joints[0].origin, lerped[index]);
			}
		}
		else
			use_swap = false;
	}
	if (flex->reference_type != REF_NULL && flex->references != NULL && entity->referenceInfo != NULL &&
		!(entity->flags & RF_IGNORE_REFS))
	{
		LERPedReferences_t *reference_info = entity->referenceInfo;
		reference_info->refType = flex->reference_type;
		if (reference_info->jointIDs == NULL)
			reference_info->jointIDs = jointIDs[flex->reference_type];
		const float delta = sw_refdef.time - reference_info->lastUpdate;
		reference_info->lastUpdate = sw_refdef.time;
		for (int reference = 0; reference < flex->num_references; ++reference)
		{
			Placement_t *current = &reference_info->references[reference].placement;
			Placement_t *old = &reference_info->oldReferences[reference].placement;
			if (delta <= REF_MINCULLTIME) *old = *current;
			const int joint_id = reference_info->jointIDs != NULL ? reference_info->jointIDs[reference] : -1;
			const qboolean swap_reference = use_swap && joint_id >= 0;
			if (swap_reference)
			{
				const sw_flex_frame_t *swap_frame = &flex->frames[swap_frame_index];
				const sw_flex_frame_t *old_swap_frame = &flex->frames[old_swap_frame_index];
				float swap_move[3], swap_front[3], swap_back[3];
				for (int axis = 0; axis < 3; ++axis)
				{
					swap_move[axis] = (front_lerp * swap_frame->translate[axis] + back_lerp * old_swap_frame->translate[axis]) * scale;
					swap_front[axis] = front_lerp * swap_frame->scale[axis] * scale;
					swap_back[axis] = back_lerp * old_swap_frame->scale[axis] * scale;
				}
				interpolate_flex_placement(&flex->references[swap_frame_index * flex->num_references + reference],
					&flex->references[old_swap_frame_index * flex->num_references + reference], swap_move, swap_front, swap_back, current);
			}
			else
				interpolate_flex_placement(&flex->references[frame_index * flex->num_references + reference],
					&flex->references[old_frame_index * flex->num_references + reference], move, front, back, current);
			if (have_rotations && joint_id >= 0 && joint_id < flex->num_clusters)
			{
				const Placement_t *joints = swap_reference ? swap_joints : current_joints;
				const matrix3_t *rotations = swap_reference ? swap_rotations : current_rotations;
				for (int joint = joint_id; joint >= 0; --joint)
				{
					RotatePointAboutLocalOrigin(rotations[joint], joints[joint].origin, current->origin);
					RotatePointAboutLocalOrigin(rotations[joint], joints[joint].origin, current->direction);
					RotatePointAboutLocalOrigin(rotations[joint], joints[joint].origin, current->up);
				}
			}
			if (swap_reference)
			{
				VectorSubtract(current->origin, swap_joints[0].origin, current->origin);
				VectorAdd(current->origin, current_joints[0].origin, current->origin);
			}
			if (delta > REF_MINCULLTIME) *old = *current;
		}
		++sw_stats.reference_updates;
	}
	const uclock_t tris_start = uclock();
	sw_stats.bench_us_flex_swaprefs += (unsigned long long)(tris_start - swaprefs_start);
	float base_light[3];
	flex_light(entity, base_light);
	sw_image_t *base_skin = entity->skin;
	if (base_skin == NULL && entity->skinnum >= 0 && entity->skinnum < flex->header.num_skins)
		base_skin = flex->skins[entity->skinnum];
	if (base_skin == NULL)
		base_skin = flex->skins[0];
	if (base_skin == NULL)
		return;
	const qboolean additive = (entity->flags & (RF_TRANS_ADD | RF_TRANS_ADD_ALPHA)) != 0;
	const float entity_alpha = (entity->flags & RF_TRANS_GHOST) ? base_light[0] * 0.5f : entity->color.a / 255.0f;
	const qboolean opaque_depth = !(entity->flags & RF_TRANS_ANY) && entity->color.a == 255;
	// E55c: premultiply per-vertex gouraud color (base_light * intensity) once
	// per entity; the value per incident triangle corner is the same
	// two-operand product the colors[][] loop would compute, bit-identical.
	const qboolean flex_opaque_ok = opaque_depth && !additive && !shadow_pass;
	if (flex_opaque_ok && flex->render_num_xyz <= SW_MAX_FLEX_VERTS)
	{
		const qboolean fullbright = (entity->flags & RF_FULLBRIGHT) != 0;
		for (int vertex = 0; vertex < flex->render_num_xyz; ++vertex)
		{
			const float intensity = fullbright ? 1.0f : intensities[vertex];
			sw_flex_vcolor[vertex][0] = base_light[0] * intensity;
			sw_flex_vcolor[vertex][1] = base_light[1] * intensity;
			sw_flex_vcolor[vertex][2] = base_light[2] * intensity;
		}
		if (sw_clut_enabled() && base_skin != NULL && base_skin->src8 != NULL)
			sw_clut_select(base_light, base_skin);
	}
	for (int mesh_index = 0; mesh_index < flex->header.num_mesh_nodes; ++mesh_index)
	{
		const fmnodeinfo_t *node = entity->fmnodeinfo != NULL ? &entity->fmnodeinfo[mesh_index] : NULL;
		// TEMP DIAG (missing-stand investigation): env-gated, prints draw/skip
		// per mesh node for models whose skin name matches H2DOS_DBG_MODELSKIN.
		if (book_dbg_on() && strstr(base_skin != NULL && base_skin->name != NULL ? base_skin->name : "", book_dbg_pat) != NULL)
			sw_ri.Con_Printf(PRINT_ALL,
				"modeldbg node=%d/%d nodeinfo=%s flags=%d skin=%d base=%s usec=%d\n",
				mesh_index, flex->header.num_mesh_nodes, node != NULL ? "set" : "NULL",
				node != NULL ? node->flags : -1, node != NULL ? node->skin : -1,
				(base_skin != NULL && base_skin->name != NULL) ? base_skin->name : "null",
				node != NULL ? (int)(node->flags & FMNI_USE_COLOR) : 0);
		if (node != NULL && (node->flags & FMNI_NO_DRAW))
			continue;
		sw_image_t *skin = base_skin;
		if (node != NULL && (node->flags & FMNI_USE_SKIN) && node->skin >= 0 && node->skin < flex->header.num_skins)
		{
			if (entity->skins != NULL && entity->skins[node->skin] != NULL)
				skin = entity->skins[node->skin];
			else if (flex->skins[node->skin] != NULL)
				skin = flex->skins[node->skin];
		}
		if (skin != NULL && skin->pixels != NULL)
			sw_flex_patch_skin(skin);
		const qboolean use_color = node != NULL && (node->flags & FMNI_USE_COLOR);
		const float node_color[4] = {
			use_color ? node->color.r / 255.0f : 1.0f,
			use_color ? node->color.g / 255.0f : 1.0f,
			use_color ? node->color.b / 255.0f : 1.0f,
			use_color ? node->color.a / 255.0f : entity_alpha
		};
		const sw_flex_mesh_t *mesh = &flex->meshes[mesh_index];
		// E-B2/ideas: mesh-node screen-bbox pre-cull. A projective map with all
		// corner w positive maps the vertex hull inside its corners' image hull,
		// so a fully-offscreen node bbox proves its tris are already doomed by
		// the per-tri bbox test => skipping is bit-exact. If any bbox corner is
		// behind the near plane, projection is unsafe: no cull.
		if (mesh->vert_count > 0)
		{
			float mn[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
			float mx[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
			for (int vi = 0; vi < mesh->vert_count; ++vi)
			{
				const float *pv = lerped[mesh->vert_indices[vi]];
				for (int axis = 0; axis < 3; ++axis)
				{
					if (pv[axis] < mn[axis]) mn[axis] = pv[axis];
					if (pv[axis] > mx[axis]) mx[axis] = pv[axis];
				}
			}
			float sminx = 0.0f, smaxx = 0.0f, sminy = 0.0f, smaxy = 0.0f;
			qboolean unsafe = false;
			for (int corner = 0; corner < 8 && !unsafe; ++corner)
			{
				const float px = (corner & 1) ? mx[0] : mn[0];
				const float py = (corner & 2) ? mx[1] : mn[1];
				const float pz = (corner & 4) ? mx[2] : mn[2];
				const float vx = ent_view_m[0][0] * px + ent_view_m[0][1] * py + ent_view_m[0][2] * pz + ent_view_t[0];
				const float vy = ent_view_m[1][0] * px + ent_view_m[1][1] * py + ent_view_m[1][2] * pz + ent_view_t[1];
				const float vz2 = ent_view_m[2][0] * px + ent_view_m[2][1] * py + ent_view_m[2][2] * pz + ent_view_t[2];
				if (!(vz2 >= SW_NEAR_CLIP))
				{
					unsafe = true;
					break;
				}
				const float cw = 1.0f / vz2;
				const float sx = sw_refdef.x + sw_refdef.width * 0.5f + vx * projection_x * cw;
				const float sy = sw_refdef.y + sw_refdef.height * 0.5f - vy * projection_y * cw;
				if (corner == 0)
				{
					sminx = smaxx = sx;
					sminy = smaxy = sy;
				}
				else
				{
					sminx = min(sminx, sx); smaxx = max(smaxx, sx);
					sminy = min(sminy, sy); smaxy = max(smaxy, sy);
				}
			}
			++sw_stats.bench_flex_nodes;
			if (!unsafe &&
				(smaxx < 0.0f || sminx >= (float)sw_width || smaxy < 0.0f || sminy >= (float)sw_height))
			{
				++sw_stats.bench_flex_nodes_culled;
				continue;
			}
		}
		const int tri_end = mesh->start_tri + mesh->num_tris;
		for (int ti = mesh->start_tri; ti < tri_end; ++ti)
		{
			const sw_flex_rtri_t *rt = &flex->rtri[ti];
			vec3_t points[3];
			float uv[3][2];
			float colors[3][4];
			const flex_proj_cache_t *projected[3];
			int vertex_indices[3];
			qboolean cache_ok = true;
			for (int corner = 0; corner < 3; ++corner)
			{
				const int vertex = rt->vi[corner];
				vertex_indices[corner] = vertex;
				projected[corner] = (vertex >= 0 && vertex < SW_MAX_FLEX_VERTS) ?
					flex_proj_lookup(entity, lerped, ent_view_m, ent_view_t, vertex) : NULL;
				if (projected[corner] == NULL)
					cache_ok = false;
			}
				// Early trivial-reject, identical to the checks inside
				// span_raster_flex_triangle/draw_flex_triangle: these triangles produce
				// no pixels, so skip colors/corner construction entirely.
				if (cache_ok &&
					((projected[0]->x < 0.0f && projected[1]->x < 0.0f && projected[2]->x < 0.0f) ||
					 (projected[0]->x >= (float)sw_width && projected[1]->x >= (float)sw_width && projected[2]->x >= (float)sw_width) ||
					 (projected[0]->y < 0.0f && projected[1]->y < 0.0f && projected[2]->y < 0.0f) ||
					 (projected[0]->y >= (float)sw_height && projected[1]->y >= (float)sw_height && projected[2]->y >= (float)sw_height) ||
					 (projected[0]->inverse_z <= 0.0f && projected[1]->inverse_z <= 0.0f && projected[2]->inverse_z <= 0.0f)))
				{
					++sw_stats.bench_flex_tris_bbox;
					continue;
				}
				// E50-restore (ideas queue #1): default-ON winding cull after the
				// 144-frame A/B measured 0 px delta with the gate swing (combat +
				// player alias + monsters). Env off-switch: H2DOS_SW_WIND=0.
				static cvar_t *sw_wind;
				if (sw_wind == NULL)
				{
					const char *env = getenv("H2DOS_SW_WIND");
					sw_wind = sw_ri.Cvar_Get("r_swwind", env != NULL ? env : "1", 0);
				}
				if (sw_wind != NULL && sw_wind->value != 0.0f && cache_ok)
				{
					const float area2 = (projected[1]->x - projected[0]->x) * (projected[2]->y - projected[0]->y) -
						(projected[2]->x - projected[0]->x) * (projected[1]->y - projected[0]->y);
					if (area2 <= 0.0f)
					{
						++sw_stats.bench_flex_tris_wind;
						continue;
					}
				}
				const qboolean opaque_tri = flex_opaque_ok && !use_color && cache_ok &&
					skin != NULL && skin->pixels != NULL;
			for (int corner = 0; corner < 3; ++corner)
			{
				uv[corner][0] = rt->st[corner][0];
				uv[corner][1] = rt->st[corner][1];
					if (!opaque_tri)
					{
						const float intensity = use_color || (entity->flags & RF_FULLBRIGHT) ? 1.0f : intensities[vertex_indices[corner]];
						colors[corner][0] = use_color ? node_color[0] : base_light[0] * intensity;
						colors[corner][1] = use_color ? node_color[1] : base_light[1] * intensity;
						colors[corner][2] = use_color ? node_color[2] : base_light[2] * intensity;
						colors[corner][3] = node_color[3];
					}
				}
				sw_bench_tri_category = 1;
				++sw_stats.bench_flex_tris;
				const qboolean used_mask = opaque_tri &&
					flex_mask_opaque(projected, rt, vertex_indices, skin);
				if (used_mask)
					++sw_stats.bench_flex_tris_fast;
				else if (cache_ok && skin != NULL && skin->pixels != NULL)
				{
					if (opaque_tri && !sw_clut_frame && sw_flex_affine_enabled())
					{
						flex_opaque_vertex_t corners[3];
						for (int corner = 0; corner < 3; ++corner)
						{
							corners[corner].x = projected[corner]->x;
							corners[corner].y = projected[corner]->y;
							corners[corner].inverse_z = projected[corner]->inverse_z;
							corners[corner].s = uv[corner][0] * (float)skin->width;
							corners[corner].t = uv[corner][1] * (float)skin->height;
							corners[corner].red = sw_flex_vcolor[vertex_indices[corner]][0];
							corners[corner].green = sw_flex_vcolor[vertex_indices[corner]][1];
							corners[corner].blue = sw_flex_vcolor[vertex_indices[corner]][2];
						}
						span_raster_flex_opaque_affine(corners[0], corners[1], corners[2], skin);
					}
					else
					{
						flex_screen_vertex_t corners[3];
						for (int corner = 0; corner < 3; ++corner)
						{
							const float w = projected[corner]->inverse_z;
							corners[corner].x = projected[corner]->x;
							corners[corner].y = projected[corner]->y;
							corners[corner].inverse_z = w;
							corners[corner].s_over_z = uv[corner][0] * (float)skin->width * w;
							corners[corner].t_over_z = uv[corner][1] * (float)skin->height * w;
							if (opaque_tri)
							{
								corners[corner].red_over_z = sw_flex_vcolor[vertex_indices[corner]][0] * w;
								corners[corner].green_over_z = sw_flex_vcolor[vertex_indices[corner]][1] * w;
								corners[corner].blue_over_z = sw_flex_vcolor[vertex_indices[corner]][2] * w;
								corners[corner].alpha_over_z = 1.0f;
								if (sw_clut_frame)
									corners[corner].intensity_over_z = ((entity->flags & RF_FULLBRIGHT) != 0 ?
										1.0f : intensities[vertex_indices[corner]]) * w;
							}
							else
							{
								corners[corner].red_over_z = colors[corner][0] * w;
								corners[corner].green_over_z = colors[corner][1] * w;
								corners[corner].blue_over_z = colors[corner][2] * w;
								corners[corner].alpha_over_z = colors[corner][3] * w;
							}
						}
						if (opaque_tri)
							span_raster_flex_opaque(corners[0], corners[1], corners[2], skin);
						else
							dispatch_flex_screen_triangle(corners[0], corners[1], corners[2], skin,
								opaque_depth, additive);
					}
					++sw_stats.bench_flex_tris_fast;
				}
				else
				{
					++sw_stats.bench_flex_tris_clip;
					for (int corner = 0; corner < 3; ++corner)
					{
						const int vertex = vertex_indices[corner];
						entity_transform_basis(entity->origin, cr, sr, cp, sp, cy, sy, lerped[vertex], points[corner]);
					}
					draw_flex_triangle(points, uv, colors, skin, opaque_depth, additive);
				}
				sw_bench_tri_category = 0;
			}
		}
	++sw_stats.flex_entities;
	sw_stats.bench_us_flex_tris += (unsigned long long)(uclock() - tris_start);
	if (shadow_pass) ++sw_stats.flex_shadow_entities;
}

void SW_RasterEntities(const refdef_t *fd)
{
	for (int pass = 0; pass < 2; ++pass)
	{
		const int count = pass == 0 ? fd->num_entities : fd->num_alpha_entities;
		entity_t **entities = pass == 0 ? fd->entities : fd->alpha_entities;
		for (int i = 0; i < count; ++i)
		{
			const entity_t *entity = entities != NULL ? entities[i] : NULL;
			if (entity == NULL)
				continue;
			if (entity->flags & RF_NODRAW)
				continue;
			const sw_model_t *model = entity->model != NULL ? *entity->model : NULL;
			if (model == NULL || model->kind == SW_MODEL_SPRITE)
			{
				// Dispatch on spriteType like R_DrawSpriteModel in production GL.
				switch (entity->spriteType)
				{
				case SPRITE_DYNAMIC:
				{
					const uclock_t tmark = uclock();
					const unsigned pixels_before = sw_stats.bench_pixels_flex;
					const unsigned spans_before = sw_stats.bench_flex_spans;
					const unsigned blocks_before = sw_stats.bench_flex_blocks;
					draw_dynamic_sprite(entity, model);
					sw_stats.bench_us_ent_dynamic += (unsigned long long)(uclock() - tmark);
					sw_stats.bench_pixels_dynamic += sw_stats.bench_pixels_flex - pixels_before;
					sw_stats.bench_spans_dynamic += sw_stats.bench_flex_spans - spans_before;
					sw_stats.bench_blocks_dynamic += sw_stats.bench_flex_blocks - blocks_before;
					++sw_stats.bench_model_dynamic;
					break;
				}
				case SPRITE_VARIABLE:
				{
					const uclock_t tmark = uclock();
					draw_variable_sprite(entity, model);
					sw_stats.bench_us_ent_variable += (unsigned long long)(uclock() - tmark);
					++sw_stats.bench_model_variable;
					break;
				}
				case SPRITE_LINE:
				{
					const uclock_t tmark = uclock();
					draw_line_sprite(entity, model);
					sw_stats.bench_us_ent_line += (unsigned long long)(uclock() - tmark);
					++sw_stats.bench_model_line;
					break;
				}
				default:
					if (model != NULL)
					{
						const uclock_t tmark = uclock();
						draw_sprite_entity(entity, model);
						sw_stats.bench_us_ent_sprite += (unsigned long long)(uclock() - tmark);
						++sw_stats.bench_model_sprite;
					}
					break;
				}
				continue;
			}
			if (model->kind == SW_MODEL_FLEX)
			{
				const uclock_t tmark = uclock();
				SW_RasterFlexEntity(entity, model,
					(entity->flags & (RF_FULLBRIGHT | RF_TRANSLUCENT | RF_ALPHA_TEXTURE)) ==
					(RF_FULLBRIGHT | RF_TRANSLUCENT | RF_ALPHA_TEXTURE));
				sw_stats.bench_us_ent_flex += (unsigned long long)(uclock() - tmark);
				++sw_stats.bench_model_flex;
			}
			else if ((model->kind == SW_MODEL_INLINE || model->kind == SW_MODEL_BSP) && model->bsp != NULL)
			{
				const uclock_t tmark = uclock();
				// Whole-model frustum reject via the submodel's bounding sphere.
				// Rotation-safe and pixel-exact: every face lies inside the sphere, so
				// a sphere fully outside a view-frustum half-space contains no face.
				vec3_t center_local;
				VectorAdd(model->mins, model->maxs, center_local);
				VectorScale(center_local, 0.5f, center_local);
				vec3_t extents;
				VectorSubtract(model->maxs, model->mins, extents);
				VectorScale(extents, 0.5f, extents);
				const float radius = (float)sqrt(dot3(extents, extents));
				vec3_t center_world;
				VectorAdd(entity->origin, center_local, center_world);
				const sw_vertex_t vc = world_to_view(center_world, 0.0f, 0.0f);
				const float rx = sw_refdef.width * 0.5f / projection_x;
				const float ry = sw_refdef.height * 0.5f / projection_y;
				const float nx = (float)sqrt(1.0f + rx * rx);
				const float ny = (float)sqrt(1.0f + ry * ry);
				const float margin = radius + 0.01f;
				if (vc.z < SW_NEAR_CLIP - radius ||
					vc.x + vc.z * rx < -nx * margin || vc.z * rx - vc.x < -nx * margin ||
					vc.y + vc.z * ry < -ny * margin || vc.z * ry - vc.y < -ny * margin)
				{
					++sw_stats.bench_brush_culled;
				}
				else
				{
					sw_brush_span_active = true;
					// GL-equivalent backface cull: BSP planes are single-sided, and GL
					// renders bmodels with face culling enabled. Translate the plane into
					// world space by the entity origin (the soft rasterizer never rotates
					// bmodels) and skip faces whose back side faces the viewer.
					for (int face = 0; face < model->num_faces; ++face)
					{
						const dface_t *face_ptr = &model->bsp->faces[model->first_face + face];
						const int tix = LittleShort(face_ptr->texinfo);
						if (tix >= 0 && tix < model->bsp->num_texinfo)
						{
							const int tflags = LittleLong(model->bsp->texinfo[tix].flags);
							if (tflags & (SURF_TRANS33 | SURF_TRANS66 | SURF_ALPHA_TEXTURE | SURF_WARP | SURF_UNDULATE))
							{
								draw_face(model->bsp, model->first_face + face, entity->origin, entity->frame);
								continue;
							}
						}
						const dplane_t *plane = &model->bsp->planes[little_ushort(face_ptr->planenum)];
						float nx = LittleFloat(plane->normal[0]);
						float ny = LittleFloat(plane->normal[1]);
						float nz = LittleFloat(plane->normal[2]);
						float pdist = LittleFloat(plane->dist);
						if (LittleShort(face_ptr->side))
						{
							nx = -nx; ny = -ny; nz = -nz; pdist = -pdist;
						}
						const float pos = nx * (sw_refdef.vieworg[0] - entity->origin[0]) +
							ny * (sw_refdef.vieworg[1] - entity->origin[1]) +
							nz * (sw_refdef.vieworg[2] - entity->origin[2]) - pdist;
						if (pos < -0.01f)
							continue;
						draw_face(model->bsp, model->first_face + face, entity->origin, entity->frame);
					}
					sw_brush_span_active = false;
				}
				sw_stats.bench_us_ent_brush += (unsigned long long)(uclock() - tmark);
				sw_stats.bench_brush_faces += (unsigned)model->num_faces;
				++sw_stats.brush_entities;
			}
		}
	}
}

// S/T windows for the 62 particle atlas cells, mirroring the production GL1 table
// so synthetic and real particle.m32 data map identically.
static const float sw_particle_st[NUM_PARTICLE_TYPES][4] =
{
	{ 0.00390625f, 0.00390625f, 0.02734375f, 0.02734375f },
	{ 0.03515625f, 0.00390625f, 0.05859375f, 0.02734375f },
	{ 0.06640625f, 0.00390625f, 0.08984375f, 0.02734375f },
	{ 0.09765625f, 0.00390625f, 0.12109375f, 0.02734375f },
	{ 0.00390625f, 0.03515625f, 0.02734375f, 0.05859375f },
	{ 0.03515625f, 0.03515625f, 0.05859375f, 0.05859375f },
	{ 0.06640625f, 0.03515625f, 0.08984375f, 0.05859375f },
	{ 0.09765625f, 0.03515625f, 0.12109375f, 0.05859375f },
	{ 0.00390625f, 0.06640625f, 0.02734375f, 0.08984375f },
	{ 0.03515625f, 0.06640625f, 0.05859375f, 0.08984375f },
	{ 0.06640625f, 0.06640625f, 0.08984375f, 0.08984375f },
	{ 0.09765625f, 0.06640625f, 0.12109375f, 0.08984375f },
	{ 0.00390625f, 0.09765625f, 0.02734375f, 0.12109375f },
	{ 0.03515625f, 0.09765625f, 0.05859375f, 0.12109375f },
	{ 0.06640625f, 0.09765625f, 0.08984375f, 0.12109375f },
	{ 0.09765625f, 0.09765625f, 0.12109375f, 0.12109375f },
	{ 0.12890625f, 0.00390625f, 0.18359375f, 0.05859375f },
	{ 0.19140625f, 0.00390625f, 0.24609375f, 0.05859375f },
	{ 0.12890625f, 0.06640625f, 0.18359375f, 0.12109375f },
	{ 0.19140625f, 0.06640625f, 0.24609375f, 0.12109375f },
	{ 0.00390625f, 0.12890625f, 0.12109375f, 0.24609375f },
	{ 0.12890625f, 0.12890625f, 0.24609375f, 0.24609375f },
	{ 0.25390625f, 0.00390625f, 0.37109375f, 0.12109375f },
	{ 0.37890625f, 0.00390625f, 0.49609375f, 0.12109375f },
	{ 0.25390625f, 0.12890625f, 0.37109375f, 0.24609375f },
	{ 0.37890625f, 0.12890625f, 0.49609375f, 0.24609375f },
	{ 0.00390625f, 0.25390625f, 0.24609375f, 0.49609375f },
	{ 0.25390625f, 0.25390625f, 0.49609375f, 0.49609375f },
	{ 0.50390625f, 0.00390625f, 0.74609375f, 0.24609375f },
	{ 0.75390625f, 0.00390625f, 0.99609375f, 0.24609375f },
	{ 0.50390625f, 0.25390625f, 0.74609375f, 0.49609375f },
	{ 0.75390625f, 0.25390625f, 0.87109375f, 0.37109375f },
	{ 0.87890625f, 0.25390625f, 0.99609375f, 0.37109375f },
	{ 0.75390625f, 0.37890625f, 0.87109375f, 0.49609375f },
	{ 0.87890625f, 0.37890625f, 0.99609375f, 0.49609375f },
	{ 0.00390625f, 0.50390625f, 0.24609375f, 0.74609375f },
	{ 0.00390625f, 0.50390625f, 0.24609375f, 0.74609375f },
	{ 0.25390625f, 0.50390625f, 0.37109375f, 0.62109375f },
	{ 0.37890625f, 0.50390625f, 0.43359375f, 0.55859375f },
	{ 0.44140625f, 0.50390625f, 0.49609375f, 0.55859375f },
	{ 0.37890625f, 0.56640625f, 0.43359375f, 0.62109375f },
	{ 0.44140625f, 0.56640625f, 0.49609375f, 0.62109375f },
	{ 0.25390625f, 0.62890625f, 0.30859375f, 0.68359375f },
	{ 0.31640625f, 0.62890625f, 0.37109375f, 0.68359375f },
	{ 0.25390625f, 0.69140625f, 0.30859375f, 0.74609375f },
	{ 0.31640625f, 0.69140625f, 0.37109375f, 0.74609375f },
	{ 0.37890625f, 0.62890625f, 0.43359375f, 0.68359375f },
	{ 0.44140625f, 0.62890625f, 0.49609375f, 0.68359375f },
	{ 0.37890625f, 0.69140625f, 0.43359375f, 0.74609375f },
	{ 0.44140625f, 0.69140625f, 0.49609375f, 0.74609375f },
	{ 0.00390625f, 0.75390625f, 0.24609375f, 0.99609375f },
	{ 0.25390625f, 0.75390625f, 0.49609375f, 0.99609375f },
	{ 0.50390625f, 0.50390625f, 0.62109375f, 0.62109375f },
	{ 0.62890625f, 0.50390625f, 0.74609375f, 0.62109375f },
	{ 0.50390625f, 0.62890625f, 0.62109375f, 0.74609375f },
	{ 0.62890625f, 0.62890625f, 0.74609375f, 0.74609375f },
	{ 0.75390625f, 0.50390625f, 0.99609375f, 0.74609375f },
	{ 0.50390625f, 0.75390625f, 0.74609375f, 0.99609375f },
	{ 0.75390625f, 0.75390625f, 0.87109375f, 0.87109375f },
	{ 0.87890625f, 0.75390625f, 0.99609375f, 0.87109375f },
	{ 0.75390625f, 0.87890625f, 0.87109375f, 0.99609375f },
	{ 0.87890625f, 0.87890625f, 0.99609375f, 0.99609375f }
};

static sw_image_t *sw_particle_tex;
static sw_image_t *sw_aparticle_tex;

void SW_ParticleTexturesInit(void)
{
	sw_particle_tex = SW_FindImage("pics/misc/particle.m32", SW_IMAGE_PIC);
	if (sw_particle_tex == NULL)
		sw_particle_tex = SW_FindImage("pics/misc/particle.m8", SW_IMAGE_PIC);
	sw_aparticle_tex = SW_FindImage("pics/misc/aparticle.m8", SW_IMAGE_PIC);
	if (sw_aparticle_tex == NULL)
		sw_aparticle_tex = sw_particle_tex;
}

void SW_ParticleTexturesShutdown(void)
{
	sw_particle_tex = NULL;
	sw_aparticle_tex = NULL;
}

static uint16_t particle_atlas_sample(const sw_image_t *image, const byte type_index, const float u, const float v)
{
	if (image == NULL)
		return SW_RGB565(255, 255, 255);
	const float *st = sw_particle_st[type_index];
	const float s = st[0] + (st[2] - st[0]) * u;
	const float t = st[1] + (st[3] - st[1]) * v;
	const int ix = ClampI((int)(s * image->width), 0, image->width - 1);
	const int iy = ClampI((int)(t * image->height), 0, image->height - 1);
	return image->pixels[iy * image->width + ix];
}

// ---------------------------------------------------------------------------
// E14 particle stamps (census #88: 1.33M textured px + 417K point px per
// 64-frame combat run, 0 additive px). draw_particle paid SW_PutPixel per px
// plus, in the quad path, two float lerps + clamps + an atlas index per px.
// Stamps bake exactly what is static: point-disc row extents (int predicate)
// and the (type x radius x image) sampled-texel grid (built with the very
// same float expressions, so bit-identical). The modulate() float chain and
// the z-guard stay per px; the additive class folds to dr + sr*255/255 ==
// dr + sr, the blend classes reuse the swept (m*0x8081)>>23 == m/255
// identity (m <= 248*255 from the 565 masks). Debug flags keep the original
// per-pixel walk.
#define SW_PP_RADIUS_MAX 16
static byte sw_pp_extent[SW_PP_RADIUS_MAX + 1][SW_PP_RADIUS_MAX + 1]; // [r][py] -> xmax
static qboolean sw_pp_extent_built;

#define SW_PT_SLOTS 512
#define SW_PT_DIM (2 * SW_PP_RADIUS_MAX)
static uint16_t sw_pt_data[SW_PT_SLOTS][SW_PT_DIM * SW_PT_DIM];
static byte sw_pt_rowcount[SW_PT_SLOTS][SW_PT_DIM]; // nonzero-source texels per row (qa!=0 px)
static int sw_pt_map[256][SW_PP_RADIUS_MAX - 1]; // [type][radius-2] -> slot, -1 unfilled, -2 bypass
static const sw_image_t *sw_pt_slot_image[SW_PT_SLOTS];
static unsigned sw_pt_next;
static qboolean sw_pt_map_built;

static void sw_pp_build_extents(void)
{
	sw_pp_extent_built = true;
	for (int r = 1; r <= SW_PP_RADIUS_MAX; ++r)
		for (int py = 0; py <= SW_PP_RADIUS_MAX; ++py)
		{
			int xmax = 0;
			for (int px = 0; px <= r; ++px)
				if (px * px + py * py <= r * r)
					xmax = px;
			sw_pp_extent[r][py] = (byte)xmax;
		}
}

static const uint16_t *sw_pt_lookup(const sw_image_t *image, const byte type_index, const int radius)
{
	if (!sw_pt_map_built)
	{
		sw_pt_map_built = true;
		memset(sw_pt_map, 0xFF, sizeof(sw_pt_map));
	}
	int slot = sw_pt_map[type_index][radius - 2];
	if (slot == -2)
		return NULL;
	if (slot >= 0)
	{
		if (sw_pt_slot_image[slot] == image)
			return sw_pt_data[slot];
		sw_pt_map[type_index][radius - 2] = -2; // image churn on one key: never restamp
		return NULL;
	}
	if (sw_pt_next >= SW_PT_SLOTS)
	{
		sw_pt_map[type_index][radius - 2] = -2;
		return NULL;
	}
	slot = (int)sw_pt_next++;
	sw_pt_map[type_index][radius - 2] = slot;
	sw_pt_slot_image[slot] = image;
	uint16_t *out = sw_pt_data[slot];
	for (int py = -radius; py < radius; ++py)
	{
		unsigned nonzero = 0;
		for (int px = -radius; px < radius; ++px)
		{
			const uint16_t texel = particle_atlas_sample(image, type_index, (float)(px + radius) / (radius * 2),
				(float)(py + radius) / (radius * 2));
			*out++ = texel;
			nonzero += (texel != 0);
		}
		sw_pt_rowcount[slot][py + radius] = (byte)nonzero;
	}
	return sw_pt_data[slot];
}
static const byte *sw_pt_rowcount_of(const uint16_t *stamp, const int radius)
{
	return sw_pt_rowcount[(int)(((uintptr_t)stamp - (uintptr_t)sw_pt_data) / sizeof(sw_pt_data[0]))] + radius;
}

static void draw_particle(const particle_t *particle, const qboolean additive)
{
	int x, y;
	float view_z;
	if (!project_point(particle->origin, &x, &y, &view_z))
		return;
	const float depth = 1.0f / view_z;
	const float projected_radius = max(1.0f, particle->scale) * projection_y / view_z;
	if (!isfinite(projected_radius))
		return;
	const int radius = ClampI((int)min(projected_radius, 16.0f), 1, 16);
	if (sw_dbg_tag_x >= 0 && sw_dbg_tag_x >= x - radius && sw_dbg_tag_x <= x + radius &&
		sw_dbg_tag_y >= y - radius && sw_dbg_tag_y <= y + radius)
		sw_ri.Con_Printf(PRINT_ALL, "tagxy particle bf=%u at=%d,%d r=%d add=%d type=%d iz=%g\n",
			sw_stats.bench_frames, x, y, radius, (int)additive, (int)(particle->type & PFL_FLAG_MASK), depth);
	const byte type_index = (byte)(particle->type & PFL_FLAG_MASK);
	const sw_image_t *image = additive ? sw_aparticle_tex : sw_particle_tex;
	paletteRGBA_t color = particle->color;
	if ((particle->type & PFL_LM_COLOR) && sw_world_model != NULL)
	{
		float light[3];
		if (sample_bsp_light(particle->origin, &light[0], &light[1], &light[2]))
		{
			color.r = (byte)ClampI((int)(color.r * light[0] * 0.5f), 0, 255);
			color.g = (byte)ClampI((int)(color.g * light[1] * 0.5f), 0, 255);
			color.b = (byte)ClampI((int)(color.b * light[2] * 0.5f), 0, 255);
		}
	}
	const qboolean point = (particle->type & PFL_SOFT_MASK) != 0;
	if (additive && color.a != 255)
	{
		color.r = (byte)((color.r * color.a) / 255);
		color.g = (byte)((color.g * color.a) / 255);
		color.b = (byte)((color.b * color.a) / 255);
		color.a = 255;
	}
	++sw_stats.bench_part_calls;
	sw_stats.bench_part_lmcolor += ((particle->type & PFL_LM_COLOR) != 0);
	if (point || radius < 2 || image == NULL)
	{
		const uint16_t color565 = SW_RGB565(color.r, color.g, color.b);
		if (sw_dbg_tag_x < 0 && !sw_dbg_zcollide && !sw_dbg_zviz && !sw_dbg_zstrict)
		{
			if (!sw_pp_extent_built)
				sw_pp_build_extents();
			const uint32_t key = (depth > 1e30f) ? 0xFFFFFFFFu : (uint32_t)(depth * SW_DEPTH_SCALE);
			const unsigned sr = (color565 >> 8) & 0xF8u;
			const unsigned sg = (color565 >> 3) & 0xFCu;
			const unsigned sb = (color565 << 3) & 0xF8u;
			unsigned mag = 0;
			for (int py = -radius; py <= radius; ++py)
			{
				// The general loop counted every in-disc px INCLUDING off-screen
				// ones (PutPixel bounds-returned after the increments): count them
				// arithmetically here so the evidence numbers stay identical.
				const int xmax = sw_pp_extent[radius][py < 0 ? -py : py];
				sw_stats.bench_part_px_point += (unsigned)(2 * xmax + 1);
				sw_stats.bench_part_px_add += additive ? (unsigned)(2 * xmax + 1) : 0u;
				const int dy = y + py;
				if ((unsigned)dy >= (unsigned)sw_height)
					continue;
				if (!additive && color.a == 0)
					continue; // PutPixel would run the z-gate then write nothing
				const int px0 = max(-(int)xmax, -x);
				const int px1 = min((int)xmax, sw_width - 1 - x);
				uint16_t *const dst = sw_color + (size_t)dy * (size_t)sw_width + x;
				const uint32_t *const zbr = sw_depth + (size_t)dy * (size_t)sw_width + x;
				for (int px = px0; px <= px1; ++px)
				{
					if (zbr[px] > key)
						continue;
					uint16_t out;
					if (additive)
					{
						// alpha==255 after premultiply: min(255, dr + sr).
						const uint16_t d = dst[px];
						const unsigned r = min(255u, ((d >> 8) & 0xF8u) + sr);
						const unsigned g = min(255u, ((d >> 3) & 0xFCu) + sg);
						const unsigned b = min(255u, ((d << 3) & 0xF8u) + sb);
						out = SW_RGB565(r, g, b);
					}
					else if (color.a == 255)
					{
						out = color565;
					}
					else
					{
						const uint16_t d = dst[px];
						const unsigned aw = color.a, dw = 255u - color.a;
						const unsigned r = ((sr * aw + ((d >> 8) & 0xF8u) * dw) * 0x8081u) >> 23;
						const unsigned g = ((sg * aw + ((d >> 3) & 0xFCu) * dw) * 0x8081u) >> 23;
						const unsigned b = ((sb * aw + ((d << 3) & 0xF8u) * dw) * 0x8081u) >> 23;
						out = SW_RGB565(r, g, b);
					}
					dst[px] = out;
					mag += (out == 0xF81Fu);
				}
			}
			sw_stats.bench_mag_putpixel += mag;
			++sw_stats.particles;
			return;
		}
		for (int py = -radius; py <= radius; ++py)
			for (int px = -radius; px <= radius; ++px)
				if (px * px + py * py <= radius * radius)
				{
					++sw_stats.bench_part_px_point;
					sw_stats.bench_part_px_add += additive;
					SW_PutPixel(x + px, y + py, depth, color565, color.a, false, additive);
				}
		++sw_stats.particles;
		return;
	}
	// Textured quad, screen-aligned like the production GL1 particle pass.
	const uint16_t *stamp = NULL;
	if (sw_dbg_tag_x < 0 && !sw_dbg_zcollide && !sw_dbg_zviz && !sw_dbg_zstrict)
		stamp = sw_pt_lookup(image, type_index, radius);
	if (stamp != NULL)
	{
		const uint32_t key = (depth > 1e30f) ? 0xFFFFFFFFu : (uint32_t)(depth * SW_DEPTH_SCALE);
		const float fr = color.r / 255.0f;
		const float fg = color.g / 255.0f;
		const float fb = color.b / 255.0f;
		const float fa = color.a / 255.0f;
		// Per-particle alpha: modulate() sets it to this same constant for any
		// nonzero source texel (and zero otherwise). additive premultiplied a==255.
		const byte quad_alpha_const = (byte)ClampI((int)(255.0f * fa), 0, 255);
		unsigned mag = 0;
		const byte *const rowcounts = sw_pt_rowcount_of(stamp, radius);
		for (int py = -radius; py < radius; ++py)
		{
			// Counter parity with the general loop, INCLUDING off-screen px
			// (PutPixel counted, then bounds-returned): the additive class counts
			// all 2r px/row (it premultiplies a to 255, so quad_alpha==0 is
			// impossible there); the other class counts nonzero-source px unless
			// the whole particle is alpha 0.
			sw_stats.bench_part_px_tex += additive ? (unsigned)(2 * radius) :
				(quad_alpha_const != 0 ? (unsigned)rowcounts[py] : 0u);
			sw_stats.bench_part_px_add += additive ? (unsigned)(2 * radius) : 0u;
			const int dy = y + py;
			if ((unsigned)dy >= (unsigned)sw_height)
				continue;
			if (quad_alpha_const == 0)
				continue;
			const int px0 = max(-radius, -x);
			const int px1 = min(radius - 1, sw_width - 1 - x);
			uint16_t *const dst = sw_color + (size_t)dy * (size_t)sw_width + x;
			const uint32_t *const zbr = sw_depth + (size_t)dy * (size_t)sw_width + x;
			const uint16_t *const srow = stamp + (py + radius) * (radius * 2) + radius;
			for (int px = px0; px <= px1; ++px)
			{
				const uint16_t source = srow[px];
				if (source == 0)
					continue; // colorkey: alpha 0, both classes write nothing
				byte quad_alpha;
				const uint16_t out = modulate(source, fr, fg, fb, fa, &quad_alpha);
				if (zbr[px] > key)
					continue;
				uint16_t fin;
				if (additive)
				{
					const uint16_t d = dst[px];
					const unsigned osr = (out >> 8) & 0xF8u;
					const unsigned osg = (out >> 3) & 0xFCu;
					const unsigned osb = (out << 3) & 0xF8u;
					const unsigned r = min(255u, ((d >> 8) & 0xF8u) + osr);
					const unsigned g = min(255u, ((d >> 3) & 0xFCu) + osg);
					const unsigned b = min(255u, ((d << 3) & 0xF8u) + osb);
					fin = SW_RGB565(r, g, b);
				}
				else if (quad_alpha_const == 255)
				{
					fin = out;
				}
				else
				{
					const uint16_t d = dst[px];
					const unsigned aw = quad_alpha_const, dw = 255u - quad_alpha_const;
					const unsigned r = ((((out >> 8) & 0xF8u) * aw + ((d >> 8) & 0xF8u) * dw) * 0x8081u) >> 23;
					const unsigned g = ((((out >> 3) & 0xFCu) * aw + ((d >> 3) & 0xFCu) * dw) * 0x8081u) >> 23;
					const unsigned b = ((((out << 3) & 0xF8u) * aw + ((d << 3) & 0xF8u) * dw) * 0x8081u) >> 23;
					fin = SW_RGB565(r, g, b);
				}
				dst[px] = fin;
				mag += (fin == 0xF81Fu);
			}
		}
		sw_stats.bench_mag_putpixel += mag;
		sw_stats.texture_particles += (type_index >= 22 && type_index < 36) ? 2 : 1;
		++sw_stats.particles;
		return;
	}
	for (int py = -radius; py < radius; ++py)
		for (int px = -radius; px < radius; ++px)
		{
			const uint16_t source = particle_atlas_sample(image, type_index, (float)(px + radius) / (radius * 2),
				(float)(py + radius) / (radius * 2));
			byte quad_alpha;
			const uint16_t out = modulate(source, color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
				color.a / 255.0f, &quad_alpha);
			if (quad_alpha == 0 && !additive)
				continue;
			++sw_stats.bench_part_px_tex;
			sw_stats.bench_part_px_add += additive;
			SW_PutPixel(x + px, y + py, depth, out, quad_alpha, false, additive);
		}
	sw_stats.texture_particles += (type_index >= 22 && type_index < 36) ? 2 : 1;
	++sw_stats.particles;
}

void SW_RasterParticles(const refdef_t *fd)
{
	for (int i = 0; i < fd->num_particles; ++i)
		draw_particle(&fd->particles[i], false);
	for (int i = 0; i < fd->anum_particles; ++i)
		draw_particle(&fd->aparticles[i], true);
}

// ---------------------------------------------------------------------------
// E15/ideas#15 light-disc stamps. SW_RasterLights used to walk a (2r+1)^2
// square per dlight, paying sqrtf + the full SW_PutPixel body per candidate
// pixel. The tables below precompute, per integer radius: the exact in-disc
// extent of each row (from the identical sqrtf predicate) and the radial
// falloff 1 - sqrtf(d2)/r. Every float the per-px chain consumes is then the
// same value the old code computed, so output is bit-identical:
// alpha = (byte)(factor * color.a * 0.35f) preserves the original
// ((1-d/r)*a)*0.35 order, and the additive blend uses
// dr + floor(sr*a/255) == floor((sr*a + dr*255)/255) with the swept identity
// (m*0x8081)>>23 == m/255 for m <= 248*255. Debug flags fall back to the
// original per-pixel walk (TAGXY prints, ZCOLLIDE counters, ZVIZ, ZSTRICT).
#define SW_DL_RADIUS_MAX 64
#define SW_DL_STRIDE (2 * SW_DL_RADIUS_MAX + 1)
#define SW_DL_FACTOR_SIZE (2 * SW_DL_RADIUS_MAX * SW_DL_RADIUS_MAX + 1)
static float sw_dl_factor[SW_DL_RADIUS_MAX + 1][SW_DL_FACTOR_SIZE];
static byte sw_dl_extent[SW_DL_RADIUS_MAX + 1][SW_DL_STRIDE];
static byte sw_dl_built[SW_DL_RADIUS_MAX + 1];

static void sw_dl_build(const int radius)
{
	sw_dl_built[radius] = 1;
	for (int py = 0; py <= radius; ++py)
	{
		int xmax = 0;
		for (int px = 0; px <= radius; ++px)
		{
			const float distance = sqrtf((float)(px * px + py * py));
			if (distance > radius)
				break;
			xmax = px;
		}
		sw_dl_extent[radius][SW_DL_RADIUS_MAX + py] = (byte)xmax;
		sw_dl_extent[radius][SW_DL_RADIUS_MAX - py] = (byte)xmax;
		for (int px = 0; px <= xmax; ++px)
		{
			const int d2 = px * px + py * py;
			sw_dl_factor[radius][d2] = 1.0f - sqrtf((float)d2) / radius;
		}
	}
}

void SW_RasterLights(const refdef_t *fd)
{
	for (int i = 0; i < fd->num_dlights; ++i)
	{
		const dlight_t *light = &fd->dlights[i];
		int x, y;
		float view_z;
		if (!project_point(light->origin, &x, &y, &view_z))
			continue;
		const float depth = 1.0f / view_z;
		const float projected_radius = light->intensity * projection_y / max(view_z, 1.0f);
		if (!isfinite(projected_radius))
			continue;
		const int radius = ClampI((int)min(projected_radius, 64.0f), 2, 64);
		if (sw_dbg_tag_x >= 0 || sw_dbg_zcollide || sw_dbg_zviz || sw_dbg_zstrict)
		{
			for (int py = -radius; py <= radius; ++py)
				for (int px = -radius; px <= radius; ++px)
				{
					const float distance = sqrtf((float)(px * px + py * py));
					if (distance > radius)
						continue;
					const byte alpha = (byte)((1.0f - distance / radius) * light->color.a * 0.35f);
					SW_PutPixel(x + px, y + py, depth,
						SW_RGB565(light->color.r, light->color.g, light->color.b), alpha, false, true);
				}
			++sw_stats.dynamic_lights;
			continue;
		}
		if (!sw_dl_built[radius])
			sw_dl_build(radius);
		const uint16_t color = SW_RGB565(light->color.r, light->color.g, light->color.b);
		const uint32_t key = (depth > 1e30f) ? 0xFFFFFFFFu : (uint32_t)(depth * SW_DEPTH_SCALE);
		const unsigned sr = (color >> 8) & 0xF8u;
		const unsigned sg = (color >> 3) & 0xFCu;
		const unsigned sb = (color << 3) & 0xF8u;
		const float *const factor = sw_dl_factor[radius];
		unsigned mag = 0;
		for (int py = -radius; py <= radius; ++py)
		{
			const int dy = y + py;
			if ((unsigned)dy >= (unsigned)sw_height)
				continue;
			const int xmax = sw_dl_extent[radius][SW_DL_RADIUS_MAX + py];
			const int px0 = max(-(int)xmax, -x);
			const int px1 = min((int)xmax, sw_width - 1 - x);
			uint16_t *const dst = sw_color + (size_t)dy * (size_t)sw_width + x;
			const uint32_t *const zbr = sw_depth + (size_t)dy * (size_t)sw_width + x;
			const int py2 = py * py;
			for (int px = px0; px <= px1; ++px)
			{
				// SW_PutPixel production z-guard: writes iff key >= stored (rejects key < zb).
				if (zbr[px] > key)
					continue;
				const byte alpha = (byte)(factor[py2 + px * px] * light->color.a * 0.35f);
				if (alpha == 0)
					continue;
				const uint16_t dcol = dst[px];
				const unsigned r = min(255u, ((dcol >> 8) & 0xF8u) + (((sr * alpha) * 0x8081u) >> 23));
				const unsigned g = min(255u, ((dcol >> 3) & 0xFCu) + (((sg * alpha) * 0x8081u) >> 23));
				const unsigned b = min(255u, ((dcol << 3) & 0xF8u) + (((sb * alpha) * 0x8081u) >> 23));
				const uint16_t out = SW_RGB565(r, g, b);
				dst[px] = out;
				mag += (out == 0xF81Fu);
			}
		}
		sw_stats.bench_mag_putpixel += mag;
		++sw_stats.dynamic_lights;
	}
}

// Optional checkerboard stripe-interleave diagnostic.
static cvar_t *sw_cvar_scan2;
int sw_scan_phase;
qboolean sw_scan2_active(void)
{
	if (sw_cvar_scan2 == NULL)
		sw_cvar_scan2 = sw_ri.Cvar_Get("r_swscan2", getenv("H2DOS_SW_SCAN2") != NULL ? "1" : "0", 0);
	return sw_cvar_scan2 != NULL && sw_cvar_scan2->value != 0.0f;
}

uint32_t sw_covermask[480][SW_COVER_WORDS];

// Deterministic combat-fx load for the T3 holdout (H2DOS_SW_FXLOAD=1): the
// production rasterizers see exactly the shapes real combat produces (typed
// atlas/softmask/additive particles, dlight stamps, a full-screen damage
// blend), injected at the refdef boundary. Counts are frame-indexed only, so
// reruns are bit-identical; the swarm orbits vieworg for coverage motion.
static void sw_debug_fxload(refdef_t *fd)
{
	const unsigned f = sw_stats.bench_frames;
	const float base_ang = (float)(f % 48u) * (6.2831853f / 48.0f);
	for (int i = 0; i < 64 && fd->num_particles < MAX_PARTICLES; ++i)
	{
		const float a = base_ang + (float)i * (6.2831853f / 64.0f);
		const float r = 96.0f + (float)((f + (unsigned)(i * 17)) & 63u);
		particle_t *p = &fd->particles[fd->num_particles++];
		p->origin[0] = fd->vieworg[0] + cosf(a) * r;
		p->origin[1] = fd->vieworg[1] + sinf(a) * r;
		p->origin[2] = fd->vieworg[2] + 8.0f + (float)((i * 13) & 31) - 16.0f;
		p->color.r = (byte)(160 + (i & 95));
		p->color.g = (byte)(64 + ((f + (unsigned)i) & 63u));
		p->color.b = 32;
		p->color.a = 255;
		p->scale = 2.0f + (float)(i & 3);
		p->type = (unsigned)(i & 7) | ((i & 1) ? PFL_ADDITIVE : 0u) |
			((i % 5 == 0) ? PFL_SOFT_MASK : 0u);
	}
	for (int i = 0; i < 2 && fd->num_dlights < MAX_DLIGHTS; ++i)
	{
		const float a = base_ang + (float)i * 3.1415926f;
		dlight_t *dl = &fd->dlights[fd->num_dlights++];
		dl->origin[0] = fd->vieworg[0] + cosf(a) * 144.0f;
		dl->origin[1] = fd->vieworg[1] + sinf(a) * 144.0f;
		dl->origin[2] = fd->vieworg[2] + 24.0f;
		dl->color.r = 255;
		dl->color.g = 96;
		dl->color.b = 24;
		dl->color.a = 255;
		dl->intensity = 220.0f + (float)((f * 11u) & 63u);
	}
	if (getenv("H2DOS_DBG_LIGHT") != NULL && fd->num_dlights < MAX_DLIGHTS)
	{
		// Debug illumination for dark-scene investigations.
		dlight_t *dl = &fd->dlights[fd->num_dlights++];
		dl->origin[0] = fd->vieworg[0];
		dl->origin[1] = fd->vieworg[1];
		dl->origin[2] = fd->vieworg[2] + 32.0f;
		dl->color.r = 255; dl->color.g = 255; dl->color.b = 255; dl->color.a = 255;
		dl->intensity = 1500.0f;
	}
	const float pulse = 0.10f + 0.05f * (float)(f & 15u) / 15.0f;
	fd->blend[0] = 1.0f;
	fd->blend[1] = 0.25f;
	fd->blend[2] = 0.15f;
	fd->blend[3] = pulse;
}

int SW_RenderFrame(const refdef_t *fd)
{
	if (fd == NULL || sw_color == NULL || fd->width <= 0 || fd->height <= 0 ||
		fd->fov_x <= 1.0f || fd->fov_x >= 179.0f || fd->fov_y <= 1.0f || fd->fov_y >= 179.0f)
		return 0;
	refdef_t fx_fd;
	if (getenv("H2DOS_SW_FXLOAD") != NULL)
	{
		fx_fd = *fd;
		sw_debug_fxload(&fx_fd);
		fd = &fx_fd;
	}
	sw_clut_frame = sw_clut_enabled();
	// Diagnostic camera tour (H2DOS_SW_TOUR=radius[,zoff]): move the refdef
	// vieworg on a deterministic circle around the first-rendered position so
	// parallax, near-plane crossings and PVS transitions actually exercise the
	// rasterizers. 64-frame bench: the client sweep already orbits viewangles.
	refdef_t tour_fd;
	{
		static int tour_init = -1;
		static float tour_radius, tour_zoff;
		static vec3_t tour_base;
		if (tour_init < 0)
		{
			const char *spec = getenv("H2DOS_SW_TOUR");
			tour_init = 1;
			tour_radius = 0.0f;
			tour_zoff = 0.0f;
			if (spec != NULL && *spec != '\0')
			{
				tour_radius = (float)atof(spec);
				const char *comma = strchr(spec, ',');
				if (comma != NULL)
					tour_zoff = (float)atof(comma + 1);
			}
			VectorCopy(fd->vieworg, tour_base);
		}
		if (tour_radius > 0.0f)
		{
			const float ang = (float)(sw_stats.bench_frames % 48) * (6.283185307f / 48.0f);
			tour_fd = *fd;
			tour_fd.vieworg[0] += tour_radius * cosf(ang);
			tour_fd.vieworg[1] += tour_radius * sinf(ang);
			tour_fd.vieworg[2] += tour_zoff;
			(void)tour_base;
			fd = &tour_fd;
		}
	}
	sw_refdef = *fd;
	setup_view(fd);
	if (fd->rdflags & RDF_NOWORLDMODEL)
		SW_ClearColorFill();
	const uclock_t frame_start = uclock();
	uclock_t mark = frame_start;
	// Sky runs after the world pass and writes only where depth is still far.
	SW_RasterWorld(fd);
	{
		const uclock_t next = uclock();
		sw_stats.bench_us_world += (unsigned long long)(next - mark);
		mark = next;
	}
	SW_RasterSky(fd);
	{
		const uclock_t next = uclock();
		sw_stats.bench_us_sky += (unsigned long long)(next - mark);
		mark = next;
	}
	SW_RasterEntities(fd);
	{
		const uclock_t next = uclock();
		sw_stats.bench_us_entities += (unsigned long long)(next - mark);
		mark = next;
	}
	// Translucent faces queued during the world+brush passes, drawn after
	// sky/entities so their blend composites over what shows through them.
	SW_RasterWorldTrans();
	{
		const uclock_t next = uclock();
		sw_stats.bench_us_world += (unsigned long long)(next - mark);
		mark = next;
	}
	SW_RasterParticles(fd);
	{
		const uclock_t next = uclock();
		sw_stats.bench_us_particles += (unsigned long long)(next - mark);
		mark = next;
	}
	SW_RasterLights(fd);
	{
		const uclock_t next = uclock();
		sw_stats.bench_us_lights += (unsigned long long)(next - mark);
		mark = next;
	}
	// census F (ideas B2): frame classes + post-blend write traffic.
	++sw_stats.bench_blend_frames;
	if (fd->blend[3] > 0.0f)
	{
		SW_BlendRect(0, 0, sw_width, sw_height, SW_RGB565(
			ClampI((int)(fd->blend[0] * 255.0f), 0, 255),
			ClampI((int)(fd->blend[1] * 255.0f), 0, 255),
			ClampI((int)(fd->blend[2] * 255.0f), 0, 255)),
			(byte)ClampI((int)(fd->blend[3] * 255.0f), 0, 255));
		sw_blend_pending = true;
	}
	else
		++sw_stats.bench_blend_alpha0;
	{
		const uclock_t next = uclock();
		sw_stats.bench_us_blend += (unsigned long long)(next - mark);
		mark = next;
	}
	{
		// Coverage audit: count sky-visible (depth==0) pixels in the top 60
		// rows so sky-band flicker can be attributed to world coverage
		// holes rather than sky rendering itself.
		unsigned holes = 0;
		for (size_t i = 0; i < (size_t)60 * (size_t)sw_width; ++i)
			holes += sw_depth[i] == 0;
		sw_stats.bench_holes_topband = holes;
		(void)mark;
		sw_stats.bench_us_frame += (unsigned long long)(uclock() - frame_start);
		++sw_stats.bench_frames;
	}
	return 0;
}

void SW_DrawName(const vec3_t origin, const char *name, const paletteRGBA_t color)
{
	int x, y;
	float depth;
	if (!project_point(origin, &x, &y, &depth) || name == NULL)
		return;
	x -= (int)strlen(name) * 4;
	for (; *name != '\0'; ++name, x += 8)
		SW_DrawChar(x, y, 1, (byte)*name, color, true);
}
