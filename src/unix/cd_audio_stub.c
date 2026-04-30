// cd_audio_stub.c -- no native CD audio backend on macOS/POSIX.

#include "cd_audio.h"

void CDAudio_Init(void) { }
void CDAudio_Shutdown(void) { }
void CDAudio_Play(int track, qboolean looping) { (void)track; (void)looping; }
void CDAudio_Stop(void) { }
void CDAudio_Update(void) { }
qboolean CDAudio_IsActive(void) { return false; }
