#include "ref_soft_local.h"

#include <float.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

// 565 colorkey convention: transparent pixels are 0x0000; alpha semitones
// quantize away (a >= 128 becomes opaque). Opaque pure black is nudged to
// 0x0001 so it never aliases with the transparent key.
static uint16_t sw_pack_texel(const unsigned r, const unsigned g, const unsigned b, const unsigned a)
{
	if (a < 128u)
		return 0;
	const uint16_t v = SW_RGB565(r, g, b);
	return v != 0 ? v : 1;
}

// Magenta/trace diagnostics: the renderer never intentionally generates
// 0xF81F, so any magenta in converted pixels identifies the converter or a
// source using 255,0,255 as content. Also flags texel==0 in images that are
// not carrying colorkey semantics (opaque-black becoming a hole risk).
static void audit_image(const sw_image_t *image)
{
	if (image == NULL || image->pixels == NULL)
		return;
	const size_t count = (size_t)image->width * (size_t)image->height;
	size_t magentas = 0, zeros = 0;
	for (size_t i = 0; i < count; ++i)
	{
		if (image->pixels[i] == 0xF81Fu)
			++magentas;
		else if (image->pixels[i] == 0)
			++zeros;
	}
	if (magentas > 0 || (zeros > 0 && !image->has_alpha))
		sw_ri.Con_Printf(PRINT_DEVELOPER,
			"ref_soft: image-audit %s magenta=%u transparent=%u has_alpha=%d synthetic=%d\n",
			image->name, (unsigned)magentas, (unsigned)zeros, image->has_alpha ? 1 : 0,
			image->synthetic_fallback ? 1 : 0);
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
		const byte *indices = (const byte *)file + offset;
		qboolean has_zero = false;
		for (size_t i = 0; i < (size_t)width * height; ++i)
		{
			const paletteRGB_t color = mip->palette[indices[i]];
			const byte alpha = ((kind == SW_IMAGE_SPRITE || kind == SW_IMAGE_SKIN) && indices[i] == 255) ? 0 : 255;
			image->pixels[i] = sw_pack_texel(color.r, color.g, color.b, alpha);
			image->has_alpha |= alpha != 255;
			has_zero |= image->pixels[i] == 0;
		}
		image->no_zero_texel = !has_zero;
		// E4 (ideas #4): retain the index8 source + packed palette row for
		// skin images so the intensity-shade CLUT can be built per entity.
		if (kind == SW_IMAGE_SKIN)
		{
			image->src8 = malloc((size_t)width * (size_t)height);
			if (image->src8 != NULL)
				memcpy(image->src8, indices, (size_t)width * (size_t)height);
			for (int i = 0; i < 256; ++i)
				image->pal565[i] = (i == 255) ? 0 : sw_pack_texel(mip->palette[i].r,
					mip->palette[i].g, mip->palette[i].b, 255);
		}
		++sw_stats.m8_images;
		audit_image(image);
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
		qboolean has_zero = false;
		for (size_t i = 0; i < count; ++i)
		{
			// WALL textures render through alpha-ignoring paths, so their zero
			// alpha bytes must NOT become the 565 transparent key (that turned
			// whole walls/sky black). Everything else keeps colorkey semantics.
			const byte a = kind == SW_IMAGE_WALL ? 255 : source[i * 4 + 3];
			image->pixels[i] = sw_pack_texel(source[i * 4], source[i * 4 + 1], source[i * 4 + 2], a);
			image->has_alpha |= kind != SW_IMAGE_WALL && source[i * 4 + 3] != 255;
			has_zero |= image->pixels[i] == 0;
		}
		image->no_zero_texel = !has_zero;
		++sw_stats.m32_images;
		audit_image(image);
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
			image->pixels[y * image->width + x] = ((x ^ y) & 8) ? SW_RGB565(255, 0, 255) : SW_RGB565(20, 20, 20);
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
	{
		free(sw_images[i].pixels);
		free(sw_images[i].src8);
	}
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

	// Extension-sibling fallback: real H2 content ships some pics as .m8 that
	// the remastered client requests as .m32 (menu/num_X, misc/textback, ...)
	// and vice versa (misc/particle shipped m32, requested m8). Without this,
	// every such pic surfaced as the magenta fallback checkerboard.
	if (image == NULL && extension != NULL &&
		(Q_stricmp(extension, ".m8") == 0 || Q_stricmp(extension, ".m32") == 0))
	{
		char sibling[MAX_QPATH];
		const size_t stem = (size_t)(extension - name);
		if (stem + 5 <= sizeof(sibling))
		{
			memcpy(sibling, name, stem);
			strcpy_s(sibling + stem, sizeof(sibling) - stem,
				Q_stricmp(extension, ".m8") == 0 ? ".m32" : ".m8");
			image = Q_stricmp(extension, ".m8") == 0 ? load_m32(sibling, kind) : load_m8(sibling, kind);
			if (image != NULL)
				strcpy_s(image->name, sizeof(image->name), name);
		}
	}
	// Alias table for art that exists in the original H2 paks only under the
	// retail layout while the remastered client requests remaster-layout paths.
	// The paks are read-only, so aliasing is the only mechanism available.
	static const struct {
		const char *requested;
		const char *alias;
	} k_skin_aliases[] = {
		{ "players/male/Corvus.m8",		"models/player/elf/!skin.m8" },
		{ "players/male/CorvusDmg.m8",	"models/player/elf/!skindmg.m8" },
		{ "players/male/CorvusP1.m8",		"models/player/elf/!skinp1.m8" },
		{ "players/male/CorvusP1Dmg.m8",	"models/player/elf/!skinp1dmg.m8" },
		{ "players/male/CorvusP2.m8",		"models/player/elf/!skinp2.m8" },
		{ "players/male/CorvusP2Dmg.m8",	"models/player/elf/!skinp2dmg.m8" },
		{ "players/male/reflect.m8",		"models/player/elf/!skinreflect.m8" },
	};
	if (image == NULL)
	{
		for (size_t i = 0; i < SDL_arraysize(k_skin_aliases); ++i)
		{
			if (Q_stricmp(name, k_skin_aliases[i].requested) == 0)
			{
				const char *alias_ext = strrchr(k_skin_aliases[i].alias, '.');
				if (alias_ext != NULL && Q_stricmp(alias_ext, ".m8") == 0)
					image = load_m8(k_skin_aliases[i].alias, kind);
				else if (alias_ext != NULL && Q_stricmp(alias_ext, ".m32") == 0)
					image = load_m32(k_skin_aliases[i].alias, kind);
				if (image != NULL)
					strcpy_s(image->name, sizeof(image->name), name);
				break;
			}
		}
	}

	if (image == NULL)
	{
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
			const uint16_t px = image->pixels[source_y * image->width + source_x];
			SW_PutPixel(x + dx, y + dy, FLT_MAX, px, px != 0 ? base_alpha : 0, false, false);
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
			const uint16_t source = sw_conchars->pixels[(source_y + py / scale) * sw_conchars->width + source_x + px / scale];
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
			SW_PutPixel(x + px, y + py, FLT_MAX, image->pixels[sy * image->width + sx], 255, false, false);
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
				image->pixels[sy * image->width + sx],
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
