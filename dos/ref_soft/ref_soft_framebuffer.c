#include "ref_soft_local.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint32_t *sw_color;
uint32_t *sw_depth;
int sw_width;
int sw_height;
h2dos_soft_stats_t sw_stats;

static SDL_Window *sw_window;
static SDL_Surface *sw_surface;

uint32_t SW_RGBA(const byte r, const byte g, const byte b, const byte a)
{
	// XRGB8888 in-memory order (B,G,R,X little-endian): matches the window
	// surface pixel format so presenting collapses to a straight copy.
	return (uint32_t)b | ((uint32_t)g << 8) | ((uint32_t)r << 16) | ((uint32_t)a << 24);
}

qboolean SW_FrameBufferInit(void *window)
{
	if (window == NULL)
		return false;

	sw_window = window;
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
	sw_color = calloc(pixels, sizeof(*sw_color));
	sw_depth = malloc(pixels * sizeof(*sw_depth));
	if (sw_color == NULL || sw_depth == NULL)
	{
		SW_FrameBufferShutdown();
		return false;
	}
	SW_Clear(SW_RGBA(0, 0, 0, 255));
	return true;
}

void SW_FrameBufferShutdown(void)
{
	free(sw_depth);
	free(sw_color);
	sw_depth = NULL;
	sw_color = NULL;
	sw_surface = NULL;
	sw_window = NULL;
	sw_width = 0;
	sw_height = 0;
}

void SW_Clear(const uint32_t color)
{
	const uclock_t start = uclock();
	const size_t pixels = (size_t)sw_width * (size_t)sw_height;
	for (size_t i = 0; i < pixels; ++i)
		sw_color[i] = color;
	// Depth stores inverse-z (larger = closer), so the far plane is zero.
	memset(sw_depth, 0, pixels * sizeof(*sw_depth));
	sw_stats.bench_us_clear += (unsigned long long)(uclock() - start);
}

static byte channel(const uint32_t color, const int shift)
{
	return (byte)((color >> shift) & 255u);
}

void SW_PutPixel(const int x, const int y, const float depth, const uint32_t color,
	const qboolean depth_write, const qboolean additive)
{
	// depth is inverse-z (larger = closer); FLT_MAX means "always nearest".
	if ((unsigned)x >= (unsigned)sw_width || (unsigned)y >= (unsigned)sw_height || depth <= 0.0f)
		return;

	const size_t index = (size_t)y * (size_t)sw_width + (size_t)x;
	// All stored depths are non-negative floats, so their bit patterns order as
	// unsigned ints exactly like the floats did, with no FPU compare.
	uint32_t depth_key;
	memcpy(&depth_key, &depth, 4);
	if (depth_key < sw_depth[index])
		return;

	const byte alpha = channel(color, 24);
	if (alpha == 255 && !additive)
	{
		sw_color[index] = color;
	}
	else if (alpha != 0)
	{
		const uint32_t destination = sw_color[index];
		const int source_weight = alpha;
		const int destination_weight = additive ? 255 : 255 - alpha;
		const byte r = (byte)min(255, (channel(color, 16) * source_weight + channel(destination, 16) * destination_weight) / 255);
		const byte g = (byte)min(255, (channel(color, 8) * source_weight + channel(destination, 8) * destination_weight) / 255);
		const byte b = (byte)min(255, (channel(color, 0) * source_weight + channel(destination, 0) * destination_weight) / 255);
		sw_color[index] = SW_RGBA(r, g, b, 255);
	}

	if (depth_write)
	{
		sw_depth[index] = depth_key;
		++sw_stats.depth_writes;
	}
}

void SW_BlendRect(const int x, const int y, const int w, const int h, const uint32_t color)
{
	if (w <= 0 || h <= 0)
		return;
	const int start_x = (int)max(0ll, -(long long)x);
	const int start_y = (int)max(0ll, -(long long)y);
	const int end_x = (int)min((long long)w, (long long)sw_width - x);
	const int end_y = (int)min((long long)h, (long long)sw_height - y);
	for (int row = start_y; row < end_y; ++row)
		for (int column = start_x; column < end_x; ++column)
			SW_PutPixel(x + column, y + row, FLT_MAX, color, false, false);
}

void SW_Present(void)
{
	const uclock_t start = uclock();
	if (sw_surface == NULL || sw_color == NULL)
		return;
	if (!SDL_LockSurface(sw_surface))
	{
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: SDL_LockSurface failed: %s\n", SDL_GetError());
		return;
	}
	const bool converted = SDL_ConvertPixels(sw_width, sw_height,
		SDL_PIXELFORMAT_XRGB8888, sw_color, sw_width * (int)sizeof(*sw_color),
		sw_surface->format, sw_surface->pixels, sw_surface->pitch);
	SDL_UnlockSurface(sw_surface);
	if (!converted || !SDL_UpdateWindowSurface(sw_window))
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
			const uint32_t pixel = sw_color[(size_t)y * (size_t)sw_width + (size_t)x];
			const byte rgb[3] = { channel(pixel, 16), channel(pixel, 8), channel(pixel, 0) };
			if (fwrite(rgb, 1, sizeof(rgb), file) != sizeof(rgb))
			{
				fclose(file);
				return false;
			}
		}
	}
	return fclose(file) == 0;
}
