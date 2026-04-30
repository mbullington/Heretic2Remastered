//
// cl_mp4.h
//
// MP4/MKV cinematic playback via the platform video backend.
//

#pragma once

#include "../qcommon/qcommon.h"

// Opens an MP4/MKV file and starts backend initialisation.
// Windows Media Foundation loads asynchronously; AVFoundation/FFmpeg load synchronously.
// Call MP4_IsLoading / MP4_FinishOpen from the main thread to complete.
qboolean    MP4_Open(const char* filepath);

// Returns true while the background loading thread is still working.
qboolean    MP4_IsLoading(void);

// Finalises a successful background load on the main thread (creates GPU
// resources via re.DrawInitCinematic).  Returns true on success.
qboolean    MP4_FinishOpen(void);

// Releases backend resources and calls re.DrawCloseCinematic.
void        MP4_Shutdown(void);

// Returns true while an MP4 file is open and playing.
qboolean    MP4_IsOpen(void);

// Returns true when the last video frame has been decoded.
qboolean    MP4_AtEnd(void);

// Decodes and stores the next video frame; also pushes the matching
// audio chunk to se.RawSamples.
void        MP4_NextFrame(void);

// Returns a pointer to the current BGRA frame (vid_width * vid_height * 4 bytes).
const byte* MP4_GetVideoFrame(void);

int         MP4_GetWidth(void);
int         MP4_GetHeight(void);
float       MP4_GetFPS(void);
int         MP4_GetFrame(void);
