// cl_mp4_stub.c -- MP4/WMF cinematic backend is Windows-only for now.

#include "cl_mp4.h"

qboolean MP4_Open(const char* filepath) { (void)filepath; return false; }
qboolean MP4_IsLoading(void) { return false; }
qboolean MP4_FinishOpen(void) { return false; }
void MP4_Shutdown(void) { }
qboolean MP4_IsOpen(void) { return false; }
qboolean MP4_AtEnd(void) { return true; }
void MP4_NextFrame(void) { }
const byte* MP4_GetVideoFrame(void) { return NULL; }
int MP4_GetWidth(void) { return 0; }
int MP4_GetHeight(void) { return 0; }
float MP4_GetFPS(void) { return 15.0f; }
int MP4_GetFrame(void) { return 0; }
