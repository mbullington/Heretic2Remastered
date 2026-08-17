//
// snd_sdl3.h
//
// Copyright 2025 mxd
//

#pragma once

#include "snd_local.h"

#ifndef H2_NO_OPENAL
extern cvar_t* s_hrtf; //mxd. Toggle HRTF spatialization (OpenAL Soft Aureal 3D mode).
#endif

extern qboolean SNDSDL3_BackendInit(void);
extern void SNDSDL3_BackendShutdown(void);
extern qboolean SNDSDL3_LockBuffer(void);
extern void SNDSDL3_UnlockBuffer(void);

#ifdef H2_DOS
typedef struct
{
	qboolean backend_initialized;
	qboolean soundblaster_driver;
	unsigned long callback_calls;
	unsigned long submitted_bytes;
	unsigned long submitted_nonzero_samples;
	unsigned long mixed_nonzero_frames;
	unsigned long cached_sounds;
	unsigned long cached_source_samples;
} h2dos_sound_stats_t;

extern qboolean SNDSDL3_GetDOSStats(h2dos_sound_stats_t* stats);
#endif

extern void SNDSDL3_Update(void);
extern void SNDSDL3_Spatialize(channel_t* ch);
extern qboolean SNDSDL3_Cache(sfx_t* sfx, const wavinfo_t* info, byte* data);
extern void SNDSDL3_RawSamples(int num_samples, uint rate, int width, int num_channels, const byte* data, float volume);