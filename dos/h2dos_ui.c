#include "h2dos_ui.h"

#include <ctype.h>
#include <stddef.h>

#define GLYPH_WIDTH 5
#define GLYPH_HEIGHT 7
#define GLYPH_ADVANCE 6

typedef struct glyph_s {
	char character;
	Uint8 rows[GLYPH_HEIGHT];
} glyph_t;

static const glyph_t glyphs[] = {
	{'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
	{'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
	{'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
	{'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
	{'I', {14, 4, 4, 4, 4, 4, 14}}, {'J', {7, 2, 2, 2, 18, 18, 12}},
	{'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
	{'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
	{'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
	{'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
	{'S', {15, 16, 16, 14, 1, 1, 30}}, {'T', {31, 4, 4, 4, 4, 4, 4}},
	{'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
	{'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
	{'Y', {17, 17, 10, 4, 4, 4, 4}}, {'Z', {31, 1, 2, 4, 8, 16, 31}},
	{'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
	{'2', {14, 17, 1, 2, 4, 8, 31}}, {'3', {30, 1, 1, 14, 1, 1, 30}},
	{'4', {2, 6, 10, 18, 31, 2, 2}}, {'5', {31, 16, 16, 30, 1, 1, 30}},
	{'6', {14, 16, 16, 30, 17, 17, 14}}, {'7', {31, 1, 2, 4, 8, 8, 8}},
	{'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 1, 14}},
	{':', {0, 4, 4, 0, 4, 4, 0}}, {'-', {0, 0, 0, 31, 0, 0, 0}},
	{'.', {0, 0, 0, 0, 0, 6, 6}}, {'/', {1, 2, 2, 4, 8, 8, 16}},
	{'>', {16, 8, 4, 2, 4, 8, 16}}, {'+', {0, 4, 4, 31, 4, 4, 0}}
};

bool H2DOS_PrepareSurfacePalette(SDL_Surface *surface)
{
	SDL_Palette *palette = SDL_GetSurfacePalette(surface);
	SDL_Color colors[256];
	int i;

	if (!SDL_ISPIXELFORMAT_INDEXED(surface->format))
		return true;
	if (!palette)
		palette = SDL_CreateSurfacePalette(surface);
	if (!palette)
		return false;

	for (i = 0; i < 256; ++i) {
		colors[i].r = (Uint8)(((i >> 5) & 7) * 255 / 7);
		colors[i].g = (Uint8)(((i >> 2) & 7) * 255 / 7);
		colors[i].b = (Uint8)((i & 3) * 255 / 3);
		colors[i].a = SDL_ALPHA_OPAQUE;
	}
	return SDL_SetPaletteColors(palette, colors, 0, 256);
}

static Uint32 color(SDL_Surface *surface, Uint8 red, Uint8 green, Uint8 blue)
{
	return SDL_MapSurfaceRGB(surface, red, green, blue);
}

static const Uint8 *find_glyph(char character)
{
	size_t i;
	const char upper = (char)toupper((unsigned char)character);

	for (i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i)
		if (glyphs[i].character == upper)
			return glyphs[i].rows;
	return NULL;
}

static void draw_text(SDL_Surface *surface, int x, int y, const char *text, Uint32 ink)
{
	for (; *text; ++text, x += GLYPH_ADVANCE) {
		const Uint8 *rows = find_glyph(*text);
		int row;

		if (!rows)
			continue;
		for (row = 0; row < GLYPH_HEIGHT; ++row) {
			int column;
			for (column = 0; column < GLYPH_WIDTH; ++column) {
				if (rows[row] & (1U << (GLYPH_WIDTH - column - 1))) {
					SDL_Rect pixel = {x + column, y + row, 1, 1};
					SDL_FillSurfaceRect(surface, &pixel, ink);
				}
			}
		}
	}
}

static void fill(SDL_Surface *surface, int x, int y, int width, int height, Uint32 fill_color)
{
	SDL_Rect rect = {x, y, width, height};
	SDL_FillSurfaceRect(surface, &rect, fill_color);
}

void H2DOS_DrawShell(SDL_Surface *surface, Uint32 frame, int selected_item, float mouse_x, float mouse_y)
{
	static const char *items[] = {"CONSOLE", "RESOURCE", "INPUT", "AUDIO"};
	const Uint32 background = color(surface, 12, 8, 22);
	const Uint32 panel = color(surface, 28, 20, 44);
	const Uint32 border = color(surface, 115, 72, 58);
	const Uint32 text = color(surface, 230, 213, 175);
	const Uint32 dim = color(surface, 160, 145, 125);
	const Uint32 selected = color(surface, 72, 104, 80);
	const Uint32 warning = color(surface, 185, 84, 62);
	int i;

	SDL_FillSurfaceRect(surface, NULL, background);
	fill(surface, 0, 0, surface->w, 18, border);
	draw_text(surface, 10, 5, "HERETIC II DOS ENGINE SHELL", text);

	fill(surface, 8, 25, 95, 106, panel);
	fill(surface, 110, 25, 202, 106, panel);
	for (i = 0; i < 4; ++i) {
		const int y = 36 + i * 21;
		if (i == selected_item)
			fill(surface, 14, y - 4, 83, 15, selected);
		draw_text(surface, 20, y, items[i], i == selected_item ? text : dim);
	}

	draw_text(surface, 119, 35, "ENGINE CORE", dim);
	draw_text(surface, 224, 35, "PASS", text);
	draw_text(surface, 119, 53, "PAK RESOURCE", dim);
	draw_text(surface, 224, 53, "PASS", text);
	draw_text(surface, 119, 71, "VESA VIDEO", dim);
	draw_text(surface, 224, 71, "PASS", text);
	draw_text(surface, 119, 89, "SB16 AUDIO", dim);
	draw_text(surface, 224, 89, "PASS", text);
	draw_text(surface, 119, 107, "EVENT QUEUE", dim);
	draw_text(surface, 224, 107, "PASS", text);

	fill(surface, 8, 139, 304, 22, warning);
	draw_text(surface, 19, 147, "NO GAME DATA - DIAGNOSTIC ONLY", text);
	draw_text(surface, 9, 170, "ARROWS OR MOUSE SELECT  ESC QUITS", dim);

	fill(surface, 8, 188, 304, 4, panel);
	fill(surface, 8, 188, 12 + (int)(frame % 284U), 4, border);

	if (mouse_x >= 0.0f && mouse_y >= 0.0f) {
		const int x = (int)mouse_x;
		const int y = (int)mouse_y;
		fill(surface, x - 3, y, 7, 1, text);
		fill(surface, x, y - 3, 1, 7, text);
	}
}
