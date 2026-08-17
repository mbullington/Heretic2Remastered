#pragma once

#include <SDL3/SDL.h>

bool H2DOS_PrepareSurfacePalette(SDL_Surface *surface);
void H2DOS_DrawShell(SDL_Surface *surface, Uint32 frame, int selected_item, float mouse_x, float mouse_y);
