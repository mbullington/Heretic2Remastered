#include "ref_soft_local.h"

#include <float.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

// 565 colorkey convention: transparent pixels are 0x0000; alpha semitones
// quantize away (a >= 128 becomes opaque). Opaque pure black is nudged to
// 0x0001 so it never aliases with the transparent key.
// B2b indexed pipeline: images hold the raw 8-bit palette indices. The first
// loaded M8 fixes the global 256-color palette; everything else (M32, raw
// RGBA, 565 API callers) routes through nearest-color LUTs derived from it.
static paletteRGB_t sw_palette_rgb[256];
static qboolean sw_palette_ready;
unsigned sw_palette_version;
uint16_t sw_fb_palette[256];
byte sw_idx_from_555[32768];
byte sw_shade_lut[64 * 256];

static unsigned sw_channel_error(const int dr, const int dg, const int db)
{
	return (unsigned)(dr * dr * 3 + dg * dg * 6 + db * db * 2);
}

static byte sw_nearest_index(const int r, const int g, const int b)
{
	byte best = 0;
	unsigned best_err = ~0u;
	for (int i = 0; i < 255; ++i)
	{
		const unsigned e = sw_channel_error(sw_palette_rgb[i].r - r, sw_palette_rgb[i].g - g, sw_palette_rgb[i].b - b);
		if (e < best_err) { best_err = e; best = (byte)i; }
	}
	return best;
}

static void sw_palette_ingest(const paletteRGB_t *palette)
{
	if (sw_palette_ready)
		return;
	memcpy(sw_palette_rgb, palette, sizeof(sw_palette_rgb));
	sw_palette_ready = true;
	++sw_palette_version;
	for (int i = 0; i < 256; ++i)
		sw_fb_palette[i] = ((uint16_t)(palette[i].r >> 3) << 11) | ((uint16_t)(palette[i].g >> 2) << 5) | (uint16_t)(palette[i].b >> 3);
	for (int key = 0; key < 32768; ++key)
	{
		const int r = (key >> 10) & 31, g = (key >> 5) & 31, b = key & 31;
		sw_idx_from_555[key] = sw_nearest_index((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2));
	}
	// shade LUT: 8 hue families x 8 levels, quantized chromatic lighting
	// family 0 = achromatic axes (neutral white); 1-7 = hue families at
	// half-saturation strength so mild tints stay mild.
	static const int hue_r[8] = { 256, 288, 269, 243, 205, 205, 237, 269 };
	static const int hue_g[8] = { 256, 237, 250, 269, 276, 237, 218, 224 };
	static const int hue_b[8] = { 256, 205, 205, 218, 237, 288, 282, 256 };
	for (int shade = 0; shade < 64; ++shade)
	{
		const int hf = shade >> 3, lv = shade & 7;
		const int scale = lv + 1;
		for (int idx = 0; idx < 256; ++idx)
		{
			const int r = min(255, sw_palette_rgb[idx].r * hue_r[hf] * scale / (256 * 8));
			const int g = min(255, sw_palette_rgb[idx].g * hue_g[hf] * scale / (256 * 8));
			const int b = min(255, sw_palette_rgb[idx].b * hue_b[hf] * scale / (256 * 8));
			sw_shade_lut[(shade << 8) | idx] = sw_nearest_index(r, g, b);
		}
	}
}

// 10-bit light channel (white = 256) -> 6-bit chromatic shade index.
int SW_ShadeFromLight(const unsigned r10, const unsigned g10, const unsigned b10)
{
	const unsigned luma = (r10 * 3u + g10 * 6u + b10 * 2u + 11u) / 11u;
	int lv = (int)(luma * 8u / 289u);
	lv = lv < 0 ? 0 : (lv > 7 ? 7 : lv);
	const unsigned mx = max(r10, max(g10, b10));
	int hf = 0;
	if (mx > 96u)
	{
		if (r10 >= g10 + (g10 >> 2) && r10 >= b10 + (b10 >> 2)) hf = (g10 > b10 + (b10 >> 1)) ? 2 : 1;
		else if (g10 >= r10 + (r10 >> 2) && g10 >= b10 + (b10 >> 2)) hf = (b10 > r10 + (r10 >> 1)) ? 4 : 3;
		else if (b10 >= r10 + (r10 >> 2) && b10 >= g10 + (g10 >> 2)) hf = (r10 > g10 + (g10 >> 1)) ? 6 : 5;
		else hf = (r10 >= b10) ? 7 : 6;
	}
	return (hf << 3) | lv;
}

byte sw_idx_from_rgb(const unsigned r, const unsigned g, const unsigned b)
{
	const unsigned key = ((r >> 3) << 10) | (((g >> 3) << 5) | (b >> 3));
	return sw_palette_ready ? sw_idx_from_555[key] : 0;
}


static sw_image_t sw_images[SW_MAX_IMAGES];
static int sw_num_images;
static sw_image_t *sw_fallback;
static sw_image_t *sw_conchars;
static sw_image_t *sw_font_images[2];

typedef struct sw_font_char_s {
	float xl;
	float yt;
	float xr;
	float yb;
	int w;
	int h;
	int baseline;
} sw_font_char_t;

static sw_font_char_t *sw_fonts[2];
static int cinematic_width;
static int cinematic_height;

static qboolean range_valid(const int length, const size_t offset, const size_t bytes)
{
	return length >= 0 && offset <= (size_t)length && bytes <= (size_t)length - offset;
}

static qboolean font_metrics_valid(const sw_font_char_t *font, const int length)
{
	if (font == NULL || length < 224 * (int)sizeof(*font))
		return false;
	for (int i = 0; i < 224; ++i)
	{
		const sw_font_char_t *glyph = &font[i];
		if (!isfinite(glyph->xl) || !isfinite(glyph->yt) || !isfinite(glyph->xr) || !isfinite(glyph->yb) ||
			glyph->xl < 0.0f || glyph->yt < 0.0f || glyph->xr > 1.0f || glyph->yb > 1.0f ||
			glyph->xl > glyph->xr || glyph->yt > glyph->yb || glyph->w < 0 || glyph->w > 4096 ||
			glyph->h < 0 || glyph->h > 4096 || glyph->baseline < -4096 || glyph->baseline > 4096 ||
			((glyph->w == 0) != (glyph->h == 0)) ||
			(glyph->w > 0 && (glyph->xl == glyph->xr || glyph->yt == glyph->yb)))
			return false;
	}
	return true;
}

static sw_image_t *new_image(const char *name, const int width, const int height)
{
	if (sw_num_images == SW_MAX_IMAGES || width <= 0 || height <= 0 ||
		(size_t)width > SIZE_MAX / (size_t)height ||
		(size_t)width * (size_t)height > SIZE_MAX / sizeof(uint32_t))
		return NULL;

	sw_image_t *image = &sw_images[sw_num_images++];
	memset(image, 0, sizeof(*image));
	strcpy_s(image->name, sizeof(image->name), name);
	image->width = width;
	image->height = height;
	image->num_frames = 1;
	image->pixels = malloc((size_t)width * (size_t)height * sizeof(*image->pixels));
	if (image->pixels == NULL)
	{
		--sw_num_images;
		return NULL;
	}
	image->registration_sequence = sw_registration_sequence;
	return image;
}

static sw_image_t *load_m8(const char *name, const sw_image_kind_t kind)
{
	void *file = NULL;
	const int length = sw_ri.FS_LoadFile(name, &file);
	if (length < (int)sizeof(miptex_t) || file == NULL)
		return NULL;

	const miptex_t *mip = file;
	const uint width = LittleLong(mip->width[0]);
	const uint height = LittleLong(mip->height[0]);
	const uint offset = LittleLong(mip->offsets[0]);
	const int num_frames = LittleLong(mip->value);
	if (LittleLong(mip->version) != MIP_VERSION || width == 0 || height == 0 ||
		width > 4096 || height > 4096 || num_frames < 0 || num_frames > 255 ||
		(size_t)width > SIZE_MAX / height ||
		!range_valid(length, offset, (size_t)width * height))
	{
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: rejected malformed M8 '%s'\n", name);
		sw_ri.FS_FreeFile(file);
		return NULL;
	}

	sw_image_t *image = new_image(name, (int)width, (int)height);
	if (image != NULL)
	{
		image->num_frames = max(1, num_frames);
		sw_palette_ingest(mip->palette);
		const byte *indices = (const byte *)file + offset;
		const qboolean colorkey = kind == SW_IMAGE_SPRITE || kind == SW_IMAGE_SKIN;
		for (size_t i = 0; i < (size_t)width * height; ++i)
		{
			image->pixels[i] = indices[i];
			image->has_alpha |= colorkey && indices[i] == SW_TRANSPARENT_INDEX;
		}
		++sw_stats.m8_images;
	}
	sw_ri.FS_FreeFile(file);
	return image;
}

static sw_image_t *load_m32(const char *name, const sw_image_kind_t kind)
{
	void *file = NULL;
	const int length = sw_ri.FS_LoadFile(name, &file);
	if (length < (int)sizeof(miptex32_t) || file == NULL)
		return NULL;

	const miptex32_t *mip = file;
	const uint width = LittleLong(mip->width[0]);
	const uint height = LittleLong(mip->height[0]);
	const uint offset = LittleLong(mip->offsets[0]);
	const int num_frames = LittleLong(mip->num_frames);
	if (LittleLong(mip->version) != MIP32_VERSION || width == 0 || height == 0 ||
		width > 4096 || height > 4096 || num_frames < 0 || num_frames > 255 ||
		(size_t)width > SIZE_MAX / height)
	{
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: rejected malformed M32 '%s'\n", name);
		sw_ri.FS_FreeFile(file);
		return NULL;
	}
	const size_t count = (size_t)width * height;
	if (count > SIZE_MAX / 4 || !range_valid(length, offset, count * 4))
	{
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: rejected malformed M32 '%s'\n", name);
		sw_ri.FS_FreeFile(file);
		return NULL;
	}

	sw_image_t *image = new_image(name, (int)width, (int)height);
	if (image != NULL)
	{
		image->num_frames = max(1, num_frames);
		const byte *source = (const byte *)file + offset;
		for (size_t i = 0; i < count; ++i)
		{
			// WALL textures render through alpha-ignoring paths; their zero alpha
			// bytes must not become the transparent colorkey. Others keep it.
			const byte a = kind == SW_IMAGE_WALL ? 255 : source[i * 4 + 3];
			image->pixels[i] = a == 0 ? SW_TRANSPARENT_INDEX : sw_idx_from_rgb(source[i * 4], source[i * 4 + 1], source[i * 4 + 2]);
			image->has_alpha |= kind != SW_IMAGE_WALL && a != 255;
		}
		++sw_stats.m32_images;
	}
	sw_ri.FS_FreeFile(file);
	return image;
}

static sw_image_t *make_fallback(void)
{
	sw_image_t *image = new_image("<synthetic-missing-asset>", 16, 16);
	if (image == NULL)
		return NULL;
	for (int y = 0; y < image->height; ++y)
		for (int x = 0; x < image->width; ++x)
			image->pixels[y * image->width + x] = ((x ^ y) & 8) ? sw_idx_from_rgb(255, 0, 255) : sw_idx_from_rgb(20, 20, 20);
	image->synthetic_fallback = true;
	return image;
}

void SW_ImageInit(void)
{
	sw_num_images = 0;
	sw_fallback = sw_synthetic_fixture ? make_fallback() : NULL;
	sw_conchars = SW_FindImage("pics/misc/conchars.m32", SW_IMAGE_PIC);
	for (int i = 0; i < 2; ++i)
	{
		char image_name[MAX_QPATH];
		char metrics_name[MAX_QPATH];
		Com_sprintf(image_name, sizeof(image_name), "pics/misc/font%d.m32", i + 1);
		Com_sprintf(metrics_name, sizeof(metrics_name), "pics/misc/font%d.fnt", i + 1);
		sw_font_images[i] = SW_FindImage(image_name, SW_IMAGE_PIC);
		void *metrics = NULL;
		const int length = sw_ri.FS_LoadFile(metrics_name, &metrics);
		if (font_metrics_valid(metrics, length))
			sw_fonts[i] = metrics;
		else if (metrics != NULL)
		{
			sw_ri.Con_Printf(PRINT_ALL, "ref_soft: rejected malformed font metrics '%s'\n", metrics_name);
			sw_ri.FS_FreeFile(metrics);
		}
	}
}

void SW_ImageShutdown(void)
{
	for (int i = 0; i < 2; ++i)
	{
		if (sw_fonts[i] != NULL)
			sw_ri.FS_FreeFile(sw_fonts[i]);
		sw_fonts[i] = NULL;
		sw_font_images[i] = NULL;
	}
	for (int i = 0; i < sw_num_images; ++i)
		free(sw_images[i].pixels);
	memset(sw_images, 0, sizeof(sw_images));
	sw_num_images = 0;
	sw_fallback = NULL;
	sw_conchars = NULL;
}

sw_image_t *SW_FindImage(const char *name, const sw_image_kind_t kind)
{
	if (name == NULL || name[0] == '\0')
		return NULL;
	for (int i = 0; i < sw_num_images; ++i)
	{
		if (Q_stricmp(sw_images[i].name, name) == 0)
		{
			sw_images[i].registration_sequence = sw_registration_sequence;
			return &sw_images[i];
		}
	}

	const char *extension = strrchr(name, '.');
	sw_image_t *image = NULL;
	if (extension != NULL && Q_stricmp(extension, ".m8") == 0)
		image = load_m8(name, kind);
	else if (extension != NULL && Q_stricmp(extension, ".m32") == 0)
		image = load_m32(name, kind);

	if (image == NULL)
	{
		++sw_stats.missing_assets;
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: missing or unsupported image '%s'%s\n", name,
			sw_synthetic_fixture ? " (using labeled synthetic fallback)" : "");
		return sw_fallback;
	}
	return image;
}

void SW_FreeUnusedImages(void)
{
	/* Stable pointers are required by the client. Reclamation happens at shutdown. */
}


void SW_DrawImage(const int x, const int y, const int w, const int h, const sw_image_t *image, const float alpha)
{
	if (image == NULL || image->pixels == NULL || w <= 0 || h <= 0 || !isfinite(alpha) || alpha <= 0.0f)
		return;
	const int start_x = (int)max(0ll, -(long long)x);
	const int start_y = (int)max(0ll, -(long long)y);
	const int end_x = (int)min((long long)w, (long long)sw_width - x);
	const int end_y = (int)min((long long)h, (long long)sw_height - y);
	const byte base_alpha = (byte)ClampI((int)(255.0f * min(alpha, 1.0f)), 0, 255);
	for (int dy = start_y; dy < end_y; ++dy)
	{
		const int source_y = (int)((long long)dy * image->height / h);
		for (int dx = start_x; dx < end_x; ++dx)
		{
			const int source_x = (int)((long long)dx * image->width / w);
			const byte px = image->pixels[source_y * image->width + source_x];
			SW_PutPixel(x + dx, y + dy, FLT_MAX, sw_fb_palette[px],
				(px != 0 && px != SW_TRANSPARENT_INDEX) ? base_alpha : 0, false, false);
		}
	}
	++sw_stats.ui_draws;
}

static sw_image_t *find_pic(const char *name)
{
	char path[MAX_QPATH];
	if (name == NULL)
		return NULL;
	if (name[0] == '/' || name[0] == '\\')
		return SW_FindImage(name + 1, SW_IMAGE_PIC);
	Com_sprintf(path, sizeof(path), "pics/%s", name);
	return SW_FindImage(path, SW_IMAGE_PIC);
}

void SW_DrawGetPicSize(int *w, int *h, const char *name)
{
	const sw_image_t *image = find_pic(name);
	*w = image != NULL ? image->width : 0;
	*h = image != NULL ? image->height : 0;
}

void SW_DrawPic(const int x, const int y, const int scale, const char *name, const float alpha)
{
	const sw_image_t *image = find_pic(name);
	if (image != NULL)
		SW_DrawImage(x, y, image->width * max(1, scale), image->height * max(1, scale), image, alpha);
}

void SW_DrawStretchPic(int x, int y, int w, int h, const char *name, const float alpha,
	const DrawStretchPicScaleMode_t mode)
{
	const sw_image_t *image = find_pic(name);
	if (mode == DSP_SCALE_SCREEN)
	{
		x = x * sw_width / 640;
		y = y * sw_height / 480;
		w = w * sw_width / 640;
		h = h * sw_height / 480;
	}
	else if (mode == DSP_SCALE_4x3)
	{
		const int width_4x3 = sw_height * 4 / 3;
		x = (sw_width - width_4x3) / 2 + x * width_4x3 / 640;
		y = y * sw_height / 480;
		w = w * width_4x3 / 640;
		h = h * sw_height / 480;
	}
	SW_DrawImage(x, y, w, h, image, alpha);
}

static uint16_t tint(const uint16_t source, const paletteRGBA_t color, byte *out_alpha)
{
	const byte r = (byte)((((source >> 8) & 0xF8u) * color.r) / 255u);
	const byte g = (byte)((((source >> 3) & 0xFCu) * color.g) / 255u);
	const byte b = (byte)((((source << 3) & 0xF8u) * color.b) / 255u);
	if (out_alpha != NULL)
		*out_alpha = source != 0 ? color.a : 0;
	return source != 0 ? SW_RGB565(r, g, b) : 0;
}

static void draw_char_layer(const int x, const int y, const int scale, const int character, const paletteRGBA_t color)
{
	if (sw_conchars == NULL || sw_conchars->pixels == NULL || scale <= 0 || scale > 64)
		return;
	const int code = character & 255;
	if ((code & 127) == 32)
		return;
	const int cell_width = sw_conchars->width / 16;
	const int cell_height = sw_conchars->height / 16;
	const int source_x = (code & 15) * cell_width;
	const int source_y = (code >> 4) * cell_height;
	for (int py = 0; py < cell_height * scale; ++py)
		for (int px = 0; px < cell_width * scale; ++px)
		{
			const uint16_t source = sw_fb_palette[sw_conchars->pixels[(source_y + py / scale) * sw_conchars->width + source_x + px / scale]];
			byte glyph_alpha;
			const uint16_t glyph = tint(source, color, &glyph_alpha);
			SW_PutPixel(x + px, y + py, FLT_MAX, glyph, glyph_alpha, false, false);
		}
	++sw_stats.ui_draws;
}

void SW_DrawChar(const int x, const int y, const int scale, const int c, const paletteRGBA_t color, const qboolean shadow)
{
	if (shadow)
	{
		const paletteRGBA_t shade = { .r = 24, .g = 24, .b = 24, .a = (byte)(color.a * 3 / 4) };
		draw_char_layer(x + scale, y + scale, scale, c, shade);
	}
	draw_char_layer(x, y, scale, c, color);
}

void SW_DrawTileClear(const int x, const int y, const int w, const int h, const char *name)
{
	const sw_image_t *image = find_pic(name);
	if (image == NULL || w <= 0 || h <= 0)
		return;
	const int start_x = (int)max(0ll, -(long long)x);
	const int start_y = (int)max(0ll, -(long long)y);
	const int end_x = (int)min((long long)w, (long long)sw_width - x);
	const int end_y = (int)min((long long)h, (long long)sw_height - y);
	for (int py = start_y; py < end_y; ++py)
		for (int px = start_x; px < end_x; ++px)
		{
			const int sx = (int)(((long long)x + px) % image->width + image->width) % image->width;
			const int sy = (int)(((long long)y + py) % image->height + image->height) % image->height;
			SW_PutPixel(x + px, y + py, FLT_MAX, sw_fb_palette[image->pixels[sy * image->width + sx]], 255, false, false);
		}
	++sw_stats.ui_draws;
}

void SW_DrawFill(const int x, const int y, const int w, const int h, const paletteRGBA_t color)
{
	SW_BlendRect(x, y, w, h, SW_RGB565(color.r, color.g, color.b), color.a);
	++sw_stats.ui_draws;
}

void SW_DrawFadeScreen(const paletteRGBA_t color)
{
	SW_BlendRect(0, 0, sw_width, sw_height, SW_RGB565(color.r, color.g, color.b), color.a);
}

static const sw_font_char_t *font_character(const sw_font_char_t *font, const byte character)
{
	const sw_font_char_t *definition = &font[character - 32];
	return definition->w != 0 ? definition : &font[14];
}

static void draw_font_character(const int x, const int y, const sw_font_char_t *definition,
	const sw_image_t *image, const float alpha)
{
	if (image == NULL)
		return;
	const int source_x = ClampI((int)(definition->xl * image->width), 0, image->width - 1);
	const int source_y = ClampI((int)(definition->yt * image->height), 0, image->height - 1);
	const int source_right = ClampI((int)ceilf(definition->xr * image->width), source_x + 1, image->width);
	const int source_bottom = ClampI((int)ceilf(definition->yb * image->height), source_y + 1, image->height);
	const int destination_x = x * sw_width / 640;
	const int destination_y = (y - definition->baseline) * sw_height / 480;
	const int destination_width = max(1, definition->w * sw_width / 640);
	const int destination_height = max(1, definition->h * sw_height / 480);
	for (int py = 0; py < destination_height; ++py)
		for (int px = 0; px < destination_width; ++px)
		{
			const int sx = source_x + px * (source_right - source_x) / destination_width;
			const int sy = source_y + py * (source_bottom - source_y) / destination_height;
			SW_PutPixel(destination_x + px, destination_y + py, FLT_MAX,
				sw_fb_palette[image->pixels[sy * image->width + sx]],
				(byte)ClampI((int)((image->pixels[sy * image->width + sx] != 0 ? 255 : 0) * alpha), 0, 255), false, false);
		}
}

void SW_DrawBigFont(int x, int y, const char *text, const float alpha)
{
	if (text == NULL || !isfinite(alpha) || alpha <= 0.0f)
		return;
	if (sw_fonts[0] == NULL || sw_fonts[1] == NULL)
	{
		paletteRGBA_t color = { .r = 255, .g = 255, .b = 255, .a = (byte)ClampI((int)(alpha * 255.0f), 0, 255) };
		for (; *text != '\0'; ++text, x += 16)
			SW_DrawChar(x, y, 2, (byte)*text, color, true);
		return;
	}
	int font_index = 0;
	const int original_x = x;
	for (byte character = (byte)*text++; character != 0; character = (byte)*text++)
	{
		switch (character)
		{
			case 1: x = (640 - SW_BigFontLength(text)) / 2; break;
			case 2: font_index = 0; break;
			case 3: font_index = 1; break;
			case '\t': x += 63; break;
			case '\n': y += 18; x = original_x; break;
			case '\r': break;
			case 32: x += 8; break;
			default:
				if (character > 32)
				{
					const sw_font_char_t *definition = font_character(sw_fonts[font_index], character);
					draw_font_character(x, y, definition, sw_font_images[font_index], alpha);
					x += definition->w;
				}
				break;
		}
	}
}

int SW_BigFontLength(const char *text)
{
	if (text == NULL)
		return 0;
	if (sw_fonts[0] == NULL || sw_fonts[1] == NULL)
		return (int)strlen(text) * 16;
	int width = 0;
	int font_index = 0;
	for (byte character = (byte)*text++; character != 0; character = (byte)*text++)
	{
		if (character == 1 || character == '\t' || character == '\n')
			break;
		if (character == 2 || character == 3)
			font_index = character - 2;
		else if (character == 32)
			width += 8;
		else if (character > 32 && character != '\r')
			width += font_character(sw_fonts[font_index], character)->w;
	}
	return width;
}

void SW_BookDrawPic(const char *name, const float scale, const float alpha)
{
	if (!isfinite(scale) || !isfinite(alpha) || scale <= 0.0f || scale > 4.0f || alpha <= 0.0f)
		return;
	const sw_model_t *model = SW_RegisterModel(name);
	if (model == NULL || model->kind != SW_MODEL_BOOK || model->book == NULL)
	{
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: could not draw book '%s'\n", name != NULL ? name : "<null>");
		return;
	}
	const float book_width = (float)LittleLong(model->book->bheader.total_w);
	const float book_height = (float)LittleLong(model->book->bheader.total_h);
	float display_width = (float)sw_width;
	const float display_height = (float)sw_height;
	int offset_x = 0;
	if (display_width * 0.75f > display_height && book_width / book_height <= 4.0f / 3.0f + 0.01f)
	{
		display_width = display_height * 4.0f / 3.0f;
		offset_x = (sw_width - (int)display_width) / 2;
	}
	int offset_y = 0;
	offset_x += (int)((book_width - book_width * scale) * 0.5f * display_width / book_width);
	offset_y += (int)((book_height - book_height * scale) * 0.5f * display_height / book_height);
	for (int i = 0; i < model->num_frames; ++i)
	{
		const bookframe_t *frame = &model->book->bframes[i];
		const int x = (int)floorf(LittleLong(frame->x) * display_width / book_width * scale);
		const int y = (int)floorf(LittleLong(frame->y) * display_height / book_height * scale);
		const int w = (int)ceilf(LittleLong(frame->w) * display_width / book_width * scale);
		const int h = (int)ceilf(LittleLong(frame->h) * display_height / book_height * scale);
		SW_DrawImage(offset_x + x, offset_y + y, w, h, model->book_images[i], alpha);
	}
}

qboolean SW_TestFontFile(const char *name)
{
	void *file = NULL;
	const int length = sw_ri.FS_LoadFile(name, &file);
	const qboolean valid = font_metrics_valid(file, length);
	if (file != NULL)
		sw_ri.FS_FreeFile(file);
	return valid;
}

void SW_DrawInitCinematic(const int width, const int height)
{
	cinematic_width = width;
	cinematic_height = height;
}

void SW_DrawCloseCinematic(void)
{
	cinematic_width = cinematic_height = 0;
}

void SW_DrawCinematic(const byte *data, const paletteRGB_t *palette)
{
	if (data == NULL || palette == NULL || cinematic_width <= 0 || cinematic_height <= 0)
		return;
	for (int y = 0; y < sw_height; ++y)
		for (int x = 0; x < sw_width; ++x)
		{
			const byte index = data[(y * cinematic_height / sw_height) * cinematic_width + x * cinematic_width / sw_width];
			SW_PutPixel(x, y, FLT_MAX, SW_RGB565(palette[index].r, palette[index].g, palette[index].b), 255, false, false);
		}
}
