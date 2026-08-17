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
	projection_x = fd->width * 0.5f / tanf(fd->fov_x * (float)M_PI / 360.0f);
	projection_y = fd->height * 0.5f / tanf(fd->fov_y * (float)M_PI / 360.0f);
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

static byte color_channel(const uint32_t color, const int shift)
{
	return (byte)((color >> shift) & 255u);
}

static uint32_t modulate(const uint32_t color, const float red, const float green, const float blue, const float alpha)
{
	return SW_RGBA(
		(byte)ClampI((int)(color_channel(color, 0) * red), 0, 255),
		(byte)ClampI((int)(color_channel(color, 8) * green), 0, 255),
		(byte)ClampI((int)(color_channel(color, 16) * blue), 0, 255),
		(byte)ClampI((int)(color_channel(color, 24) * alpha), 0, 255));
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

#define SW_SPAN_STEP 16
#define SW_LIGHT_CACHE_MAX 64
// Dense build scratch (lw x lh) and the band-packed working buffer (lw+2 x lh+2).
static uint32_t sw_face_light_dense[SW_LIGHT_CACHE_MAX * SW_LIGHT_CACHE_MAX];
static uint32_t sw_face_light_cache[(SW_LIGHT_CACHE_MAX + 2) * (SW_LIGHT_CACHE_MAX + 2)];

// Reciprocal table replaces the per-block integer divide of the span stepping
// code ((u1-u0)*65536/len) with one float multiply: an fdiv is ~39 cycles on a
// 486/Pentium and is worth hundreds of cycles per triangle on small spans.
#define SW_INV_WIDTH_MAX 64
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
} sw_face_light_store_t;
static sw_face_light_store_t *sw_face_light_stores;
static int sw_face_light_store_count;
static unsigned sw_face_light_generation;
static float sw_lightstyle_fingerprint[MAX_LIGHTSTYLES * 3];
static qboolean sw_lightstyle_fingerprint_valid;

static qboolean sw_lightstyles_changed(void)
{
	const lightstyle_t *styles = sw_refdef.lightstyles;
	if (!sw_lightstyle_fingerprint_valid)
		return true;
	for (int i = 0; i < MAX_LIGHTSTYLES; ++i)
	{
		if (styles != NULL)
		{
			if (sw_lightstyle_fingerprint[i * 3] != styles[i].rgb[0] ||
				sw_lightstyle_fingerprint[i * 3 + 1] != styles[i].rgb[1] ||
				sw_lightstyle_fingerprint[i * 3 + 2] != styles[i].rgb[2])
				return true;
		}
		else if (sw_lightstyle_fingerprint[i * 3] != 1.0f || sw_lightstyle_fingerprint[i * 3 + 1] != 1.0f ||
			sw_lightstyle_fingerprint[i * 3 + 2] != 1.0f)
			return true;
	}
	return false;
}

static void sw_lightstyles_snapshot(void)
{
	const lightstyle_t *styles = sw_refdef.lightstyles;
	for (int i = 0; i < MAX_LIGHTSTYLES; ++i)
	{
		sw_lightstyle_fingerprint[i * 3] = styles != NULL ? styles[i].rgb[0] : 1.0f;
		sw_lightstyle_fingerprint[i * 3 + 1] = styles != NULL ? styles[i].rgb[1] : 1.0f;
		sw_lightstyle_fingerprint[i * 3 + 2] = styles != NULL ? styles[i].rgb[2] : 1.0f;
	}
	sw_lightstyle_fingerprint_valid = true;
}

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

// span_modulate with the light channels pre-unpacked (avoids re-extracting
// them per pixel; the caller keeps them in registers per light cell).
static inline uint32_t span_modulate3(const uint32_t texel, const unsigned lr, const unsigned lg,
	const unsigned lb)
{
	uint32_t r = (((texel) & 0xffu) * lr + 128u) >> 8;
	uint32_t g = ((((texel) >> 8) & 0xffu) * lg + 128u) >> 8;
	uint32_t b = ((((texel) >> 16) & 0xffu) * lb + 128u) >> 8;
	if (r > 255u) r = 255u;
	if (g > 255u) g = 255u;
	if (b > 255u) b = 255u;
	return r | (g << 8) | (b << 16) | 0xff000000u;
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
			const size_t offset = (size_t)light_offset + (size_t)styles[style] * layer_size + (size_t)sample * 3;
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

static inline uint32_t span_modulate(const uint32_t texel, const uint32_t light_packed)
{
	uint32_t r = (((texel & 0xffu) * ((light_packed >> 20) & 1023u)) + 128u) >> 8;
	uint32_t g = ((((texel >> 8) & 0xffu) * ((light_packed >> 10) & 1023u)) + 128u) >> 8;
	uint32_t b = ((((texel >> 16) & 0xffu) * (light_packed & 1023u)) + 128u) >> 8;
	if (r > 255u) r = 255u;
	if (g > 255u) g = 255u;
	if (b > 255u) b = 255u;
	return r | (g << 8) | (b << 16) | 0xff000000u;
}

static void draw_face_spans(const screen_vertex_t *verts, const int n, const sw_image_t *image,
	const sw_bsp_t *bsp, const dface_t *face, const float texture_min_s, const float texture_min_t,
	const int light_width, const int light_height, const float alpha, const int face_index)
{
	if (n < 3 || image == NULL || image->pixels == NULL)
		return;
	// Cheap reject before gradient math: whole polygon outside the viewport.
	{
		float minx = verts[0].x, maxx = verts[0].x, miny = verts[0].y, maxy = verts[0].y;
		for (int i = 1; i < n; ++i)
		{
			minx = min(minx, verts[i].x); maxx = max(maxx, verts[i].x);
			miny = min(miny, verts[i].y); maxy = max(maxy, verts[i].y);
		}
		if (maxx < 0.0f || minx >= (float)sw_width || maxy < 0.0f || miny >= (float)sw_height)
			return;
	}

	// Plane gradients in screen space for the five interpolated attributes.
	// u/z, v/z and 1/z are exactly affine in (x, y) for a planar polygon.
	const screen_vertex_t *v0 = &verts[0];
	const screen_vertex_t *v1 = &verts[1];
	const screen_vertex_t *v2 = &verts[2];
	const float x10 = v1->x - v0->x, y10 = v1->y - v0->y;
	const float x20 = v2->x - v0->x, y20 = v2->y - v0->y;
	const float denom = x10 * y20 - x20 * y10;
	if (!isfinite(denom) || fabsf(denom) < 0.0001f)
		return;
	const float inv_denom = 1.0f / denom;
	float gradient_uz_x, gradient_uz_y, gradient_vz_x, gradient_vz_y;
	float gradient_w_x, gradient_w_y, gradient_lsz_x, gradient_lsz_y, gradient_ltz_x, gradient_ltz_y;
#define SW_GRADIENT(attr, out_x, out_y) do { \
		const float p10 = v1->attr - v0->attr; \
		const float p20 = v2->attr - v0->attr; \
		out_x = (p10 * y20 - p20 * y10) * inv_denom; \
		out_y = (p20 * x10 - p10 * x20) * inv_denom; \
	} while (0)
	SW_GRADIENT(s_over_z, gradient_uz_x, gradient_uz_y);
	SW_GRADIENT(t_over_z, gradient_vz_x, gradient_vz_y);
	SW_GRADIENT(inverse_z, gradient_w_x, gradient_w_y);
	SW_GRADIENT(light_s_over_z, gradient_lsz_x, gradient_lsz_y);
	SW_GRADIENT(light_t_over_z, gradient_ltz_x, gradient_ltz_y);
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
		if (store->generation != sw_face_light_generation || store->samples == NULL ||
			store->light_width != light_width || store->light_height != light_height)
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
		return;

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
			continue;

		const float x_center = xa + 0.5f - v0->x;
		float uz = v0->s_over_z + gradient_uz_x * x_center + gradient_uz_y * y_center;
		float vz = v0->t_over_z + gradient_vz_x * x_center + gradient_vz_y * y_center;
		float w = v0->inverse_z + gradient_w_x * x_center + gradient_w_y * y_center;
		float lsz = v0->light_s_over_z + gradient_lsz_x * x_center + gradient_lsz_y * y_center;
		float ltz = v0->light_t_over_z + gradient_ltz_x * x_center + gradient_ltz_y * y_center;

		uint32_t *const fb_row = sw_color + (size_t)y * sw_width;
		uint32_t *const zb_row = sw_depth + (size_t)y * sw_width;
		int x = xa;
		int remaining = xb - xa + 1;
		while (remaining > 0)
		{
			++sw_stats.bench_world_blocks;
			const int block = (x & (SW_SPAN_STEP - 1)) ? min(remaining, SW_SPAN_STEP - (x & (SW_SPAN_STEP - 1))) : min(remaining, SW_SPAN_STEP);
			if (w <= 0.0f)
				return;
			const float w_end = w + gradient_w_x * block;
			const float iz0 = 1.0f / w;
			const float iz1 = 1.0f / max(w_end, 1e-20f);
			const float u0 = uz * iz0;
			const float v0b = vz * iz0;
			const float ls0 = lsz * iz0;
			const float lt0 = ltz * iz0;
			const float u1 = (uz + gradient_uz_x * block) * iz1;
			const float v1b = (vz + gradient_vz_x * block) * iz1;
			const float ls1 = (lsz + gradient_lsz_x * block) * iz1;
			const float lt1 = (ltz + gradient_ltz_x * block) * iz1;
			int iu = (int)(u0 * 65536.0f);
			int it = (int)(v0b * 65536.0f);
			// +1 cell bias into the band-packed light map => no per-pixel clamps
			int ils = (int)(ls0 * 65536.0f) - (light_min_s << 16) + (1 << 20);
			int ilt = (int)(lt0 * 65536.0f) - (light_min_t << 16) + (1 << 20);
			const int iu_step = (int)((u1 - u0) * 65536.0f * SW_INV_WIDTH(block));
			const int it_step = (int)((v1b - v0b) * 65536.0f * SW_INV_WIDTH(block));
			const int ils_step = (int)((ls1 - ls0) * 65536.0f * SW_INV_WIDTH(block));
			const int ilt_step = (int)((lt1 - lt0) * 65536.0f * SW_INV_WIDTH(block));
			const int light_max_s_fixed = ((light_width + 2) << 20) - 1;
			const int light_max_t_fixed = ((light_height + 2) << 20) - 1;
			if (ils > light_max_s_fixed) ils = light_max_s_fixed;
			else if (ils < 0) ils = 0;
			if (ilt > light_max_t_fixed) ilt = light_max_t_fixed;
			else if (ilt < 0) ilt = 0;
			// All stored depths are non-negative floats (SW_PutPixel/span guards
			// reject <= 0), so their bit patterns order as uint32 keys exactly like
			// the floats did. Step an integer key block-wise instead of a per-pixel
			// FADD + FCOM pair; within-block linearization of the key can flip only
			// exact z-fighting ties (documented tolerance).
			uint32_t ikey;
			uint32_t ikey_end;
			memcpy(&ikey, &w, 4);
			memcpy(&ikey_end, &w_end, 4);
			const int ikey_step = ((int)ikey_end - (int)ikey) / block;

			if (alpha >= 1.0f && twist_pow2 && theight_pow2)
			{
				unsigned depth_writes = 0;
				int light_prev_idx = -1;
				unsigned light_prev = 0;
				unsigned light_r = 256u, light_g = 256u, light_b = 256u;
				for (int i = 0; i < block; ++i, ++x)
				{
					const size_t idx = (size_t)x;
					if (ikey >= zb_row[idx])
					{
						const uint32_t texel = image->pixels[((it >> 16) & mask_h) * image->width + ((iu >> 16) & mask_w)];
						const int lidx = (int)((unsigned)ilt >> 20) * light_stride + (int)((unsigned)ils >> 20);
						if (lidx != light_prev_idx)
						{
							light_prev_idx = lidx;
							light_prev = light_cache[lidx];
							light_r = (light_prev >> 20) & 1023u;
							light_g = (light_prev >> 10) & 1023u;
							light_b = light_prev & 1023u;
						}
						fb_row[idx] = span_modulate3(texel, light_r, light_g, light_b);
						zb_row[idx] = ikey;
						++depth_writes;
					}
					iu += iu_step; it += it_step; ils += ils_step; ilt += ilt_step; ikey += (uint32_t)ikey_step;
				}
				sw_stats.depth_writes += depth_writes;
				sw_stats.bench_pixels_world += (unsigned)block;
			}
			else
			{
				const int alpha_fixed = (int)(alpha * 256.0f + 0.5f);
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
					const uint32_t texel = image->pixels[ty * image->width + tx];
					const uint32_t lit = span_modulate(texel,
						light_cache[(int)((unsigned)ilt >> 20) * light_stride + (int)((unsigned)ils >> 20)]);
					int sa = ((lit >> 24) * alpha_fixed) >> 8;
					if (sa > 255) sa = 255;
					if (sa != 0 && ikey >= zb_row[idx])
					{
						const uint32_t dst = fb_row[idx];
						const int dw = 255 - sa;
						const uint32_t r = min(255u, ((lit & 0xffu) * sa + (dst & 0xffu) * dw) / 255u);
						const uint32_t g = min(255u, (((lit >> 8) & 0xffu) * sa + ((dst >> 8) & 0xffu) * dw) / 255u);
						const uint32_t bb = min(255u, (((lit >> 16) & 0xffu) * sa + ((dst >> 16) & 0xffu) * dw) / 255u);
						fb_row[idx] = r | (g << 8) | (bb << 16) | 0xff000000u;
						if (sa == 255 && alpha >= 1.0f)
						{
							zb_row[idx] = ikey;
							++sw_stats.depth_writes;
						}
					}
					++sw_stats.bench_pixels_world;
					iu += iu_step; it += it_step; ils += ils_step; ilt += ilt_step; ikey += (uint32_t)ikey_step;
				}
			}
			(void)gradient_w_x;
			uz += gradient_uz_x * block;
			vz += gradient_vz_x * block;
			lsz += gradient_lsz_x * block;
			ltz += gradient_ltz_x * block;
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
			const uint32_t color = modulate(image->pixels[ty * image->width + tx], red, green, blue, alpha);
			SW_PutPixel(x, y, inverse_z, color, alpha >= 1.0f && color_channel(color, 24) == 255, false);
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

static void draw_face(const sw_bsp_t *bsp, const int face_index, const vec3_t origin, const int entity_frame)
{
	if (face_index < 0 || face_index >= bsp->num_faces)
		return;
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
	int animation_frames;
	const sw_image_t *image = texture_animation(bsp, texinfo_index, entity_frame, &animation_frames);
	if (image == NULL)
		return;
	const float quake_amount = (flags & SURF_QUAKE) ? quake_surface_amount() : 0.0f;
	const float flowing_scroll = (flags & SURF_FLOWING) ?
		-64.0f * (sw_refdef.time * 0.5f - floorf(sw_refdef.time * 0.5f)) : 0.0f;
	sw_vertex_t view_vertices[SW_MAX_FACE_VERTS];
	float minimum_s = FLT_MAX;
	float minimum_t = FLT_MAX;
	float maximum_s = -FLT_MAX;
	float maximum_t = -FLT_MAX;
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
		vec3_t point;
		for (int axis = 0; axis < 3; ++axis)
		{
			point[axis] = LittleFloat(bsp->vertices[vertex_index].point[axis]) + origin[axis];
			if (!isfinite(point[axis]))
				return;
		}
		const float base_s = point[0] * LittleFloat(texture->vecs[0][0]) +
			point[1] * LittleFloat(texture->vecs[0][1]) + point[2] * LittleFloat(texture->vecs[0][2]) +
			LittleFloat(texture->vecs[0][3]);
		const float base_t = point[0] * LittleFloat(texture->vecs[1][0]) +
			point[1] * LittleFloat(texture->vecs[1][1]) + point[2] * LittleFloat(texture->vecs[1][2]) +
			LittleFloat(texture->vecs[1][3]);
		if (!isfinite(base_s) || !isfinite(base_t))
			return;
		if (flags & SURF_UNDULATE)
			point[2] += turbsin[TURBSIN_V0(point[0], point[1], sw_refdef.time)] * 0.25f +
				turbsin[TURBSIN_V1(point[0], point[1], sw_refdef.time)] * 0.125f;
		if (quake_amount != 0.0f)
			point[2] += turbsin[TURBSIN_V0(point[0], point[1], sw_refdef.time)] * quake_amount * 0.5f +
				turbsin[TURBSIN_V1(point[0], point[1], sw_refdef.time)] * quake_amount * 0.25f;
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
	const float light_width_value = (ceilf(maximum_s / 16.0f) * 16.0f - minimum_s) / 16.0f + 1.0f;
	const float light_height_value = (ceilf(maximum_t / 16.0f) * 16.0f - minimum_t) / 16.0f + 1.0f;
	if (!isfinite(light_width_value) || !isfinite(light_height_value) ||
		light_width_value < 1.0f || light_height_value < 1.0f ||
		light_width_value > 4096.0f || light_height_value > 4096.0f)
		return;
	const int light_width = (int)light_width_value;
	const int light_height = (int)light_height_value;
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
	if (sw_spans != NULL && sw_spans->value != 0.0f &&
		light_width <= SW_LIGHT_CACHE_MAX && light_height <= SW_LIGHT_CACHE_MAX)
		draw_face_spans(projected, clipped_count, image, bsp, lighting_face, minimum_s,
			minimum_t, light_width, light_height, alpha, face_index);
	else
		for (int i = 1; i + 1 < clipped_count; ++i)
			raster_triangle(projected[0], projected[i], projected[i + 1], image,
				bsp, lighting_face, minimum_s, minimum_t, light_width, light_height, alpha);
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

static qboolean cluster_visible(const sw_bsp_t *bsp, const int view_cluster, const int cluster)
{
	if (view_cluster < 0 || cluster < 0 || bsp->visibility_length < 4)
		return true;
	const int clusters = read_little_int(bsp->visibility);
	if (clusters <= 0 || clusters > (bsp->visibility_length - 4) / 8 ||
		view_cluster >= clusters || cluster >= clusters)
		return true;
	int offset = read_little_int(bsp->visibility + 4 + view_cluster * 8);
	if (offset < 0 || offset >= bsp->visibility_length)
		return true;
	int output = 0;
	const int row = (clusters + 7) / 8;
	while (output < row && offset < bsp->visibility_length)
	{
		const byte value = bsp->visibility[offset++];
		if (value != 0)
		{
			if (cluster / 8 == output)
				return (value & (1 << (cluster & 7))) != 0;
			++output;
		}
		else
		{
			if (offset >= bsp->visibility_length)
				return true;
			const int run = bsp->visibility[offset++];
			if (cluster / 8 >= output && cluster / 8 < output + run)
				return false;
			output += run;
		}
	}
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

static uint32_t *sky_cache;
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

static qboolean sky_cache_matches(const refdef_t *fd, const float rotation_angle)
{
	return sky_cache_key.valid && sky_cache_key.generation == sw_sky_generation &&
		sky_cache_key.framebuffer_width == sw_width && sky_cache_key.framebuffer_height == sw_height &&
		sky_cache_key.x == fd->x && sky_cache_key.y == fd->y &&
		sky_cache_key.width == fd->width && sky_cache_key.height == fd->height &&
		sky_cache_key.fov_x == fd->fov_x && sky_cache_key.fov_y == fd->fov_y &&
		sky_cache_key.viewangles[0] == fd->viewangles[0] &&
		sky_cache_key.viewangles[1] == fd->viewangles[1] &&
		sky_cache_key.viewangles[2] == fd->viewangles[2] &&
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
		uint32_t *replacement = realloc(sky_cache, required_pixels * sizeof(*replacement));
		if (replacement != NULL)
		{
			sky_cache = replacement;
			sky_cache_pixels = required_pixels;
			sky_cache_key.valid = false;
		}
	}
	const float rotation_angle = sw_sky_rotate == 0.0f ? 0.0f : sw_refdef.time * sw_sky_rotate;
	const qboolean rotating = sw_sky_rotate != 0.0f;
	const size_t drawn_pixels = (size_t)max(0, maximum_x - minimum_x) * (size_t)max(0, maximum_y - minimum_y);
	if (!rotating && sky_cache != NULL && sky_cache_matches(fd, rotation_angle))
	{
		for (int y = minimum_y; y < maximum_y; ++y)
		{
			const uint32_t *zb_row = sw_depth + (size_t)y * sw_width;
			const uint32_t *src = sky_cache + y * sw_width + minimum_x;
			uint32_t *dst = sw_color + y * sw_width + minimum_x;
			for (int x = 0; x < maximum_x - minimum_x; ++x)
				if (zb_row[minimum_x + x] == 0)
					dst[x] = src[x];
		}
		sw_stats.sky_pixels += (unsigned)drawn_pixels;
		sw_stats.sky_face_mask |= sky_cache_face_mask;
		return;
	}
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
	uint32_t *const cache_dst = (!rotating && sky_cache != NULL) ? sky_cache : NULL;
	unsigned rendered_face_mask = 0;
	for (int y = minimum_y; y < maximum_y; ++y)
	{
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
		uint32_t *fb_row = sw_color + y * sw_width;
		const uint32_t *zb_row = sw_depth + (size_t)y * sw_width;
		uint32_t *cache_row = cache_dst != NULL ? cache_dst + y * sw_width : NULL;
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
					if (zb_row[x] == 0)
					{
						const int texture_x = ClampI(is >> 16, 0, image->width - 1);
						const int texture_y = ClampI(it >> 16, 0, image->height - 1);
						const uint32_t color = image->pixels[texture_y * image->width + texture_x];
						fb_row[x] = color;
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
					if (zb_row[x] == 0)
					{
						float s, t;
						const int face = sky_coordinates(direction, &s, &t);
						const sw_image_t *image = sw_sky_images[face];
						const int texture_x = ClampI((int)(s * (image->width - 1)), 0, image->width - 1);
						const int texture_y = ClampI((int)(t * (image->height - 1)), 0, image->height - 1);
						const uint32_t color = image->pixels[texture_y * image->width + texture_x];
						fb_row[x] = color;
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
		VectorCopy(fd->viewangles, sky_cache_key.viewangles);
		sky_cache_key.rotation_angle = rotation_angle;
		sky_cache_key.generation = sw_sky_generation;
		sky_cache_key.valid = true;
		sky_cache_face_mask = rendered_face_mask;
	}
}

void SW_RasterWorld(const refdef_t *fd)
{
	if (sw_world_model == NULL || sw_world_model->bsp == NULL || (fd->rdflags & RDF_NOWORLDMODEL))
		return;
	if (sw_lightstyles_changed())
	{
		sw_lightstyles_snapshot();
		++sw_face_light_generation;
		if (sw_face_light_generation == 0)
		{
			for (int i = 0; i < sw_face_light_store_count; ++i)
				sw_face_light_stores[i].generation = 0;
			sw_face_light_generation = 1;
		}
	}
	const sw_bsp_t *bsp = sw_world_model->bsp;
	byte *visible_faces = calloc((size_t)max(1, bsp->num_faces), 1);
	if (visible_faces == NULL)
		return;
	const int leaf_index = point_leaf(bsp, fd->vieworg);
	const int view_cluster = leaf_index >= 0 && leaf_index < bsp->num_leaves ? LittleShort(bsp->leaves[leaf_index].cluster) : -1;
	for (int i = 0; i < bsp->num_leaves; ++i)
	{
		const dleaf_t *leaf = &bsp->leaves[i];
		const int cluster = LittleShort(leaf->cluster);
		const int area = LittleShort(leaf->area);
		if (!cluster_visible(bsp, view_cluster, cluster) ||
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
				visible_faces[index] = 1;
		}
	}
	const vec3_t zero = { 0.0f, 0.0f, 0.0f };
	for (int i = 0; i < bsp->num_faces; ++i)
		if (visible_faces[i])
			draw_face(bsp, i, zero, (int)(fd->time * 2.0f));
	free(visible_faces);
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

static uint32_t sw_white_pixel_storage;
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
	sw_white_pixel_storage = SW_RGBA(entity->color.r, entity->color.g, entity->color.b, 255);
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

typedef struct flex_screen_vertex_s {
	float x;
	float y;
	float inverse_z;
	float s_over_z;
	float t_over_z;
	float red_over_z;
	float green_over_z;
	float blue_over_z;
	float alpha_over_z;
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

static qboolean sample_bsp_light(const vec3_t origin, float *red, float *green, float *blue)
{
	if (sw_world_model == NULL || sw_world_model->bsp == NULL)
		return false;
	const sw_bsp_t *bsp = sw_world_model->bsp;
	float nearest = FLT_MAX;
	const dface_t *nearest_face = NULL;
	vec3_t nearest_point = { 0.0f, 0.0f, 0.0f };
	for (int face_index = 0; face_index < bsp->num_faces; ++face_index)
	{
		const dface_t *face = &bsp->faces[face_index];
		const int flags = LittleLong(bsp->texinfo[LittleShort(face->texinfo)].flags);
		if (flags & SURF_FULLBRIGHT)
			continue;
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
			continue;
		const float drop = (dot3(normal, origin) - distance) / normal[2];
		if (!isfinite(drop) || drop < -0.01f || drop >= nearest || drop > 2048.0f)
			continue;
		vec3_t point = { origin[0], origin[1], origin[2] - drop };
		if (!face_point_inside(bsp, face, point, normal))
			continue;
		nearest = drop;
		nearest_face = face;
		VectorCopy(point, nearest_point);
	}
	if (nearest_face == NULL)
		return false;
	const texinfo_t *texture = &bsp->texinfo[LittleShort(nearest_face->texinfo)];
	const float s = nearest_point[0] * LittleFloat(texture->vecs[0][0]) + nearest_point[1] * LittleFloat(texture->vecs[0][1]) +
		nearest_point[2] * LittleFloat(texture->vecs[0][2]) + LittleFloat(texture->vecs[0][3]);
	const float t = nearest_point[0] * LittleFloat(texture->vecs[1][0]) + nearest_point[1] * LittleFloat(texture->vecs[1][1]) +
		nearest_point[2] * LittleFloat(texture->vecs[1][2]) + LittleFloat(texture->vecs[1][3]);
	float minimum_s = FLT_MAX;
	float minimum_t = FLT_MAX;
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
	const int light_width = (int)((ceilf(maximum_s / 16.0f) * 16.0f - minimum_s) / 16.0f + 1.0f);
	const int light_height = (int)((ceilf(maximum_t / 16.0f) * 16.0f - minimum_t) / 16.0f + 1.0f);
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
			const float distance = sqrtf(max(0.0f, dot3(delta, delta)));
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

// Span-engine triangle raster for flex models and sprites: 16-pixel subdivided
// affine stepping of the five perspective attributes (s/t/r/g/b/a over z) plus
// affine inverse-z. Gouraud color and alpha are interpolated with the same
// fixed-point granularity as the world lightmap path (+/-1 LSB tolerance).
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
			left_x = lp0->x + (lp1->y > lp0->y ? ((y + 0.5f) - lp0->y) * left_slope : lp0->x);
		}
		else
			left_x += left_slope;
		if (right_prev != old_right_prev || y == scan_first)
		{
			right_slope = (rp1->y > rp0->y) ? (rp1->x - rp0->x) / (rp1->y - rp0->y) : 0.0f;
			right_x = rp0->x + (rp1->y > rp0->y ? ((y + 0.5f) - rp0->y) * right_slope : rp0->x);
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
		uint32_t *const fb_row = sw_color + (size_t)y * sw_width;
		uint32_t *const zb_row = sw_depth + (size_t)y * sw_width;
		int x = xa;
		int remaining = xb - xa + 1;
		while (remaining > 0)
		{
			const int block = (x & (SW_SPAN_STEP - 1)) ? min(remaining, SW_SPAN_STEP - (x & (SW_SPAN_STEP - 1))) : min(remaining, SW_SPAN_STEP);
			++sw_stats.bench_flex_blocks;
			if (w <= 0.0f)
				break;
			const float w_end = w + gw_x * block;
			// Flex triangles are small: a single divide per span block captures the
			// z-curve at block start and linearizes within. Span blocks are <=16 px.
			const float iz0 = 1.0f / w;
			const float iz1 = iz0;
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
			uint32_t ikey;
			uint32_t ikey_end;
			memcpy(&ikey, &w, 4);
			memcpy(&ikey_end, &w_end, 4);
			const int ikey_step = ((int)ikey_end - (int)ikey) / block;
			for (int i = 0; i < block; ++i, ++x)
			{
				++sw_stats.bench_pixels_flex;
				const size_t idx = (size_t)x;
				if (!no_depth_test && ikey < zb_row[idx])
					goto advance;
				{
					const int tx = ClampI(iu >> 16, 0, image->width - 1);
					const int ty = ClampI(it >> 16, 0, image->height - 1);
					const uint32_t texel = image->pixels[ty * image->width + tx];
					const int cr = min(512, max(0, ir >> 9)); // fixed [0..512]: 256 == 1.0
					const int cg = min(512, max(0, ig >> 9));
					const int cb = min(512, max(0, ib >> 9));
					const int ca = min(512, max(0, ia >> 9));
					const uint32_t mr = ((texel & 0xffu) * cr + 128u) >> 8;
					const uint32_t mg = (((texel >> 8) & 0xffu) * cg + 128u) >> 8;
					const uint32_t mb = (((texel >> 16) & 0xffu) * cb + 128u) >> 8;
					const int salpha = (int)min(255u, (((texel >> 24) & 0xffu) * ca + 128u) >> 8);
					if (salpha == 0)
						goto advance;
					if (additive)
					{
						const uint32_t dst = fb_row[idx];
						fb_row[idx] = SW_RGBA(
							(byte)min(255u, (min(255u, mr) * salpha + (dst & 0xffu) * 255u) / 255u),
							(byte)min(255u, (min(255u, mg) * salpha + ((dst >> 8) & 0xffu) * 255u) / 255u),
							(byte)min(255u, (min(255u, mb) * salpha + ((dst >> 16) & 0xffu) * 255u) / 255u), 255);
					}
					else if (salpha == 255)
					{
						fb_row[idx] = SW_RGBA((byte)min(255u, mr), (byte)min(255u, mg), (byte)min(255u, mb), 255);
						if (depth_write && !no_depth_test)
						{
							zb_row[idx] = ikey;
							++sw_stats.depth_writes;
						}
					}
					else
					{
						const uint32_t dst = fb_row[idx];
						const int dw = 255 - salpha;
						fb_row[idx] = SW_RGBA(
							(byte)min(255u, (min(255u, mr) * salpha + (dst & 0xffu) * dw) / 255u),
							(byte)min(255u, (min(255u, mg) * salpha + ((dst >> 8) & 0xffu) * dw) / 255u),
							(byte)min(255u, (min(255u, mb) * salpha + ((dst >> 16) & 0xffu) * dw) / 255u), 255);
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
			const uint32_t color = modulate(image->pixels[ty * image->width + tx], red, green, blue, alpha);
			if (color_channel(color, 24) != 0)
				SW_PutPixel(x, y, sw_sprite_no_depth_test ? FLT_MAX : inverse_z, color,
					depth_write && color_channel(color, 24) == 255 && !sw_sprite_no_depth_test, additive);
		}
	++sw_stats.triangles;
	++sw_stats.flex_triangles;
}

// Project flex vertex index `vertex` (world position in lerped[vertex]) once
// and cache the screen coordinates plus inverse-z for every incident triangle
// of this entity. Returns false when the vertex is behind the near plane, in
// which case the caller must fall back to the general clipped path.
static qboolean flex_proj_lookup(const entity_t *entity, const vec3_t lerped[], const float cr,
	const float sr, const float cp, const float sp, const float cy, const float sy, const int vertex,
	flex_proj_cache_t *out)
{
	flex_proj_cache_t *cache = &sw_flex_proj_cache[vertex];
	if (cache->entity_seq != sw_flex_proj_cache_entity_seq)
	{
		vec3_t world;
		entity_transform_basis(entity->origin, cr, sr, cp, sp, cy, sy, lerped[vertex], world);
		const sw_vertex_t view = world_to_view(world, 0.0f, 0.0f);
		const float w = 1.0f / view.z;
		cache->x = sw_refdef.x + sw_refdef.width * 0.5f + view.x * projection_x * w;
		cache->y = sw_refdef.y + sw_refdef.height * 0.5f - view.y * projection_y * w;
		cache->z = view.z;
		cache->inverse_z = w;
		cache->entity_seq = sw_flex_proj_cache_entity_seq;
	}
	*out = *cache;
	return cache->z >= SW_NEAR_CLIP;
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
	if (sw_spans == NULL)
		sw_spans = sw_ri.Cvar_Get("r_swspans", "1", 0);
	if (sw_spans != NULL && sw_spans->value != 0.0f)
		span_raster_flex_triangle(a, b, c, image, depth_write, additive);
	else
		raster_flex_triangle(a, b, c, image, depth_write, additive);
}

void SW_RasterFlexEntity(const entity_t *entity, const sw_model_t *model, const qboolean shadow_pass)
{
	static vec3_t lerped[SW_MAX_FLEX_VERTS];
	static float intensities[SW_MAX_FLEX_VERTS];
	const uclock_t prologue_start = uclock();
	if (entity == NULL || model == NULL || model->flex == NULL || (entity->flags & RF_NODRAW) ||
		!isfinite(entity->scale) || entity->scale <= 0.0f || !isfinite(entity->backlerp) ||
		!isfinite(entity->angles[0]) || !isfinite(entity->angles[1]) || !isfinite(entity->angles[2]))
		return;
	const sw_flex_t *flex = model->flex;
	int frame_index = entity->frame;
	int old_frame_index = entity->oldframe;
	if (frame_index < 0 || frame_index >= flex->header.num_frames) frame_index = 0;
	if (old_frame_index < 0 || old_frame_index >= flex->header.num_frames) old_frame_index = 0;
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
			lerped[vertex][axis] = frame->vertices[vertex].v[axis] * front[axis] +
				old_frame->vertices[vertex].v[axis] * back[axis] + move[axis];
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
	flex_proj_cache_reset();
	const uclock_t swaprefs_start = uclock();
	qboolean use_swap = flex->num_clusters > 0 && entity->swapFrame != NO_SWAP_FRAME &&
		entity->swapFrame >= 0 && entity->swapFrame < flex->header.num_frames;
	Placement_t swap_joints[SW_MAX_FLEX_JOINTS];
	matrix3_t swap_rotations[SW_MAX_FLEX_JOINTS];
	int swap_joint_indices[SW_MAX_FLEX_JOINTS] = { 0 };
	if (use_swap)
	{
		int old_swap = entity->oldSwapFrame;
		if (old_swap < 0 || old_swap >= flex->header.num_frames) old_swap = 0;
		const sw_flex_frame_t *swap_frame = &flex->frames[entity->swapFrame];
		const sw_flex_frame_t *old_swap_frame = &flex->frames[old_swap];
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
				lerped[index][axis] = swap_frame->vertices[index].v[axis] * swap_front[axis] +
					old_swap_frame->vertices[index].v[axis] * swap_back[axis] + swap_move[axis];
		}
		for (int joint = 0; joint < flex->num_clusters; ++joint)
			interpolate_flex_placement(&flex->skeleton_placements[entity->swapFrame * flex->num_clusters + joint],
				&flex->skeleton_placements[old_swap * flex->num_clusters + joint], swap_move, swap_front, swap_back, &swap_joints[joint]);
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
				int old_swap = entity->oldSwapFrame;
				if (old_swap < 0 || old_swap >= flex->header.num_frames) old_swap = 0;
				const sw_flex_frame_t *swap_frame = &flex->frames[entity->swapFrame];
				const sw_flex_frame_t *old_swap_frame = &flex->frames[old_swap];
				float swap_move[3], swap_front[3], swap_back[3];
				for (int axis = 0; axis < 3; ++axis)
				{
					swap_move[axis] = (front_lerp * swap_frame->translate[axis] + back_lerp * old_swap_frame->translate[axis]) * scale;
					swap_front[axis] = front_lerp * swap_frame->scale[axis] * scale;
					swap_back[axis] = back_lerp * old_swap_frame->scale[axis] * scale;
				}
				interpolate_flex_placement(&flex->references[entity->swapFrame * flex->num_references + reference],
					&flex->references[old_swap * flex->num_references + reference], swap_move, swap_front, swap_back, current);
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
	for (int mesh_index = 0; mesh_index < flex->header.num_mesh_nodes; ++mesh_index)
	{
		const fmnodeinfo_t *node = entity->fmnodeinfo != NULL ? &entity->fmnodeinfo[mesh_index] : NULL;
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
		const qboolean use_color = node != NULL && (node->flags & FMNI_USE_COLOR);
		const float node_color[4] = {
			use_color ? node->color.r / 255.0f : 1.0f,
			use_color ? node->color.g / 255.0f : 1.0f,
			use_color ? node->color.b / 255.0f : 1.0f,
			use_color ? node->color.a / 255.0f : entity_alpha
		};
		const sw_flex_mesh_t *mesh = &flex->meshes[mesh_index];
		int position = mesh->start_glcmds;
		const int end = position + mesh->num_glcmds;
		while (position < end)
		{
			const int signed_count = flex->glcmds[position++];
			if (signed_count == 0) break;
			const int count = signed_count < 0 ? -signed_count : signed_count;
			const int command_start = position;
			for (int triangle = 0; triangle < count - 2; ++triangle)
			{
				int command_vertices[3];
				if (signed_count < 0)
				{
					command_vertices[0] = 0;
					command_vertices[1] = triangle + 1;
					command_vertices[2] = triangle + 2;
				}
				else if (triangle & 1)
				{
					command_vertices[0] = triangle + 1;
					command_vertices[1] = triangle;
					command_vertices[2] = triangle + 2;
				}
				else
				{
					command_vertices[0] = triangle;
					command_vertices[1] = triangle + 1;
					command_vertices[2] = triangle + 2;
				}
				vec3_t points[3];
				float uv[3][2];
				float colors[3][4];
				flex_proj_cache_t projected[3];
				int vertex_indices[3];
				qboolean cache_ok = true;
				for (int corner = 0; corner < 3; ++corner)
				{
					const int command = command_start + command_vertices[corner] * 3;
					const int vertex = flex->glcmds[command + 2];
					vertex_indices[corner] = vertex;
					if (vertex < 0 || vertex >= SW_MAX_FLEX_VERTS ||
						!flex_proj_lookup(entity, lerped, cr, sr, cp, sp, cy, sy, vertex, &projected[corner]))
						cache_ok = false;
				}
				// Early trivial-reject, identical to the checks inside
				// span_raster_flex_triangle/draw_flex_triangle: these triangles produce
				// no pixels, so skip colors/corner construction entirely.
				if (cache_ok &&
					((projected[0].x < 0.0f && projected[1].x < 0.0f && projected[2].x < 0.0f) ||
					 (projected[0].x >= (float)sw_width && projected[1].x >= (float)sw_width && projected[2].x >= (float)sw_width) ||
					 (projected[0].y < 0.0f && projected[1].y < 0.0f && projected[2].y < 0.0f) ||
					 (projected[0].y >= (float)sw_height && projected[1].y >= (float)sw_height && projected[2].y >= (float)sw_height) ||
					 (projected[0].inverse_z <= 0.0f && projected[1].inverse_z <= 0.0f && projected[2].inverse_z <= 0.0f)))
				{
					++sw_stats.bench_flex_tris_bbox;
					continue;
				}
				for (int corner = 0; corner < 3; ++corner)
				{
					const int command = command_start + command_vertices[corner] * 3;
					float s, t;
					memcpy(&s, &flex->glcmds[command], 4);
					memcpy(&t, &flex->glcmds[command + 1], 4);
					uv[corner][0] = s;
					uv[corner][1] = t;
					const float intensity = use_color || (entity->flags & RF_FULLBRIGHT) ? 1.0f : intensities[vertex_indices[corner]];
					colors[corner][0] = use_color ? node_color[0] : base_light[0] * intensity;
					colors[corner][1] = use_color ? node_color[1] : base_light[1] * intensity;
					colors[corner][2] = use_color ? node_color[2] : base_light[2] * intensity;
					colors[corner][3] = node_color[3];
				}
				sw_bench_tri_category = 1;
				++sw_stats.bench_flex_tris;
				if (cache_ok && skin != NULL && skin->pixels != NULL)
				{
					flex_screen_vertex_t corners[3];
					for (int corner = 0; corner < 3; ++corner)
					{
						const float w = projected[corner].inverse_z;
						corners[corner].x = projected[corner].x;
						corners[corner].y = projected[corner].y;
						corners[corner].inverse_z = w;
						corners[corner].s_over_z = uv[corner][0] * (float)skin->width * w;
						corners[corner].t_over_z = uv[corner][1] * (float)skin->height * w;
						corners[corner].red_over_z = colors[corner][0] * w;
						corners[corner].green_over_z = colors[corner][1] * w;
						corners[corner].blue_over_z = colors[corner][2] * w;
						corners[corner].alpha_over_z = colors[corner][3] * w;
					}
					dispatch_flex_screen_triangle(corners[0], corners[1], corners[2], skin,
						opaque_depth && !skin->has_alpha, additive);
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
					draw_flex_triangle(points, uv, colors, skin, opaque_depth && !skin->has_alpha, additive);
				}
				sw_bench_tri_category = 0;
			}
			position += count * 3;
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
					draw_dynamic_sprite(entity, model);
					break;
				case SPRITE_VARIABLE:
					draw_variable_sprite(entity, model);
					break;
				case SPRITE_LINE:
					draw_line_sprite(entity, model);
					break;
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
					for (int face = 0; face < model->num_faces; ++face)
						draw_face(model->bsp, model->first_face + face, entity->origin, entity->frame);
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

static uint32_t particle_atlas_sample(const sw_image_t *image, const byte type_index, const float u, const float v)
{
	if (image == NULL)
		return SW_RGBA(255, 255, 255, 255);
	const float *st = sw_particle_st[type_index];
	const float s = st[0] + (st[2] - st[0]) * u;
	const float t = st[1] + (st[3] - st[1]) * v;
	const int ix = ClampI((int)(s * image->width), 0, image->width - 1);
	const int iy = ClampI((int)(t * image->height), 0, image->height - 1);
	return image->pixels[iy * image->width + ix];
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
	if (point || radius < 2 || image == NULL)
	{
		for (int py = -radius; py <= radius; ++py)
			for (int px = -radius; px <= radius; ++px)
				if (px * px + py * py <= radius * radius)
					SW_PutPixel(x + px, y + py, depth, SW_RGBA(color.r, color.g, color.b, color.a), false, additive);
		++sw_stats.particles;
		return;
	}
	// Textured quad, screen-aligned like the production GL1 particle pass.
	for (int py = -radius; py < radius; ++py)
		for (int px = -radius; px < radius; ++px)
		{
			const uint32_t source = particle_atlas_sample(image, type_index, (float)(px + radius) / (radius * 2),
				(float)(py + radius) / (radius * 2));
			const uint32_t out = modulate(source, color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
				color.a / 255.0f);
			if (color_channel(out, 24) == 0 && !additive)
				continue;
			SW_PutPixel(x + px, y + py, depth, out, false, additive);
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
		for (int py = -radius; py <= radius; ++py)
			for (int px = -radius; px <= radius; ++px)
			{
				const float distance = sqrtf((float)(px * px + py * py));
				if (distance > radius)
					continue;
				const byte alpha = (byte)((1.0f - distance / radius) * light->color.a * 0.35f);
				SW_PutPixel(x + px, y + py, depth, SW_RGBA(light->color.r, light->color.g, light->color.b, alpha), false, true);
			}
		++sw_stats.dynamic_lights;
	}
}

int SW_RenderFrame(const refdef_t *fd)
{
	if (fd == NULL || sw_color == NULL || fd->width <= 0 || fd->height <= 0 ||
		fd->fov_x <= 1.0f || fd->fov_x >= 179.0f || fd->fov_y <= 1.0f || fd->fov_y >= 179.0f)
		return 0;
	sw_refdef = *fd;
	setup_view(fd);
	const uclock_t frame_start = uclock();
	uclock_t mark = frame_start;
	// Sky runs AFTER the world pass and writes only where depth is still far
	// (the sky draws no depth, so world-covered pixels cost one int compare).
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
	if (fd->blend[3] > 0.0f)
		SW_BlendRect(0, 0, sw_width, sw_height, SW_RGBA(
			(byte)ClampI((int)(fd->blend[0] * 255.0f), 0, 255),
			(byte)ClampI((int)(fd->blend[1] * 255.0f), 0, 255),
			(byte)ClampI((int)(fd->blend[2] * 255.0f), 0, 255),
			(byte)ClampI((int)(fd->blend[3] * 255.0f), 0, 255)));
	{
		const uclock_t next = uclock();
		sw_stats.bench_us_blend += (unsigned long long)(next - mark);
		sw_stats.bench_us_frame += (unsigned long long)(next - frame_start);
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
