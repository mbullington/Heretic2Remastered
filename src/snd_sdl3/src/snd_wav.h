//
// snd_wav.h
//
// Copyright 1998 Raven Software
//

#pragma once

#include "snd_local.h"

extern sfxcache_t* S_LoadSound(sfx_t* s);

#ifdef H2_DOS
extern qboolean H2DOS_WavFileIsValid(const char* name);
#endif