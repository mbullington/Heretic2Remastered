// cl_mp4_avfoundation.m -- MP4/MKV cinematic backend for macOS via AVFoundation.

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

#include "client.h"
#include "cl_mp4.h"

#include <math.h>

#define MP4_AUDIO_ACCUM_SIZE (512 * 1024)

#define MP4_LOAD_IDLE     0
#define MP4_LOAD_OK       2
#define MP4_LOAD_FAILED   3

typedef struct MP4PlaybackInfo_s
{
	AVURLAsset* asset;
	AVAssetReader* reader;
	AVAssetReaderTrackOutput* video_output;
	AVAssetReaderTrackOutput* audio_output;

	int frame;
	float fps;

	int vid_width;
	int vid_height;
	byte* video_frame;

	int snd_rate;
	int snd_channels;
	int snd_width;
	byte* audio_accum;
	byte* audio_leftover;
	size_t audio_leftover_size;
	qboolean has_audio;
	qboolean audio_at_end;

	qboolean at_end;
	qboolean valid;
	LONG load_state;
	char load_filepath[MAX_OSPATH];
} MP4PlaybackInfo_t;

static MP4PlaybackInfo_t mpi;

static void MP4_FreeState(const qboolean close_cinematic)
{
	if (close_cinematic && mpi.valid)
		re.DrawCloseCinematic();

	if (mpi.reader != nil)
	{
		[mpi.reader cancelReading];
		[mpi.reader release];
		mpi.reader = nil;
	}
	if (mpi.video_output != nil)
	{
		[mpi.video_output release];
		mpi.video_output = nil;
	}
	if (mpi.audio_output != nil)
	{
		[mpi.audio_output release];
		mpi.audio_output = nil;
	}
	if (mpi.asset != nil)
	{
		[mpi.asset release];
		mpi.asset = nil;
	}

	if (mpi.video_frame != NULL)
	{
		free(mpi.video_frame);
		mpi.video_frame = NULL;
	}
	if (mpi.audio_accum != NULL)
	{
		free(mpi.audio_accum);
		mpi.audio_accum = NULL;
	}
	if (mpi.audio_leftover != NULL)
	{
		free(mpi.audio_leftover);
		mpi.audio_leftover = NULL;
	}

	memset(&mpi, 0, sizeof(mpi));
}

static float MP4_GetTrackFPS(AVAssetTrack* track)
{
	float fps = track.nominalFrameRate;
	if (fps > 0.0f)
		return fps;

	const CMTime min_frame_duration = track.minFrameDuration;
	if (CMTIME_IS_NUMERIC(min_frame_duration) && min_frame_duration.value > 0 && min_frame_duration.timescale > 0)
		return (float)min_frame_duration.timescale / (float)min_frame_duration.value;

	return 15.0f;
}

static qboolean MP4_InitVideoOutput(AVAssetTrack* video_track)
{
	const CGSize natural_size = video_track.naturalSize;
	mpi.vid_width = (int)fabs(natural_size.width);
	mpi.vid_height = (int)fabs(natural_size.height);
	mpi.fps = MP4_GetTrackFPS(video_track);

	if (mpi.vid_width <= 0 || mpi.vid_height <= 0 || mpi.fps <= 0.0f)
		return false;

	NSDictionary* video_settings = @{
		(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
	};

	mpi.video_output = [[AVAssetReaderTrackOutput alloc] initWithTrack:video_track outputSettings:video_settings];
	if (mpi.video_output == nil)
		return false;

	mpi.video_output.alwaysCopiesSampleData = NO;

	if (![mpi.reader canAddOutput:mpi.video_output])
		return false;

	[mpi.reader addOutput:mpi.video_output];

	mpi.video_frame = (byte*)malloc((size_t)(mpi.vid_width * mpi.vid_height * 4));
	return mpi.video_frame != NULL;
}

static void MP4_InitAudioOutput(void)
{
	NSArray* audio_tracks = [mpi.asset tracksWithMediaType:AVMediaTypeAudio];
	if (audio_tracks.count == 0)
		return;

	AVAssetTrack* audio_track = [audio_tracks objectAtIndex:0];

	mpi.snd_rate = 48000;
	mpi.snd_channels = 2;
	mpi.snd_width = 2;

	NSArray* format_descriptions = audio_track.formatDescriptions;
	if (format_descriptions.count > 0)
	{
		CMAudioFormatDescriptionRef format = (CMAudioFormatDescriptionRef)[format_descriptions objectAtIndex:0];
		const AudioStreamBasicDescription* asbd = CMAudioFormatDescriptionGetStreamBasicDescription(format);
		if (asbd != NULL)
		{
			if (asbd->mSampleRate > 0.0)
				mpi.snd_rate = (int)(asbd->mSampleRate + 0.5);
			if (asbd->mChannelsPerFrame > 0)
				mpi.snd_channels = (int)asbd->mChannelsPerFrame;
		}
	}

	NSDictionary* audio_settings = @{
		AVFormatIDKey : @(kAudioFormatLinearPCM),
		AVLinearPCMBitDepthKey : @16,
		AVLinearPCMIsFloatKey : @NO,
		AVLinearPCMIsBigEndianKey : @NO,
		AVLinearPCMIsNonInterleaved : @NO
	};

	mpi.audio_output = [[AVAssetReaderTrackOutput alloc] initWithTrack:audio_track outputSettings:audio_settings];
	if (mpi.audio_output == nil)
		return;

	mpi.audio_output.alwaysCopiesSampleData = NO;
	if (![mpi.reader canAddOutput:mpi.audio_output])
	{
		[mpi.audio_output release];
		mpi.audio_output = nil;
		return;
	}

	[mpi.reader addOutput:mpi.audio_output];

	mpi.audio_accum = (byte*)malloc(MP4_AUDIO_ACCUM_SIZE);
	mpi.audio_leftover = (byte*)malloc(MP4_AUDIO_ACCUM_SIZE);
	if (mpi.audio_accum == NULL || mpi.audio_leftover == NULL)
	{
		if (mpi.audio_accum != NULL) { free(mpi.audio_accum); mpi.audio_accum = NULL; }
		if (mpi.audio_leftover != NULL) { free(mpi.audio_leftover); mpi.audio_leftover = NULL; }
		return;
	}

	mpi.has_audio = true;
}

static void MP4_ReadAudioForFrame(void)
{
	if (!mpi.has_audio || mpi.audio_at_end || mpi.audio_output == nil || mpi.audio_accum == NULL || mpi.fps <= 0.0f)
		return;

	const size_t bytes_per_frame = (size_t)((float)(mpi.snd_rate * mpi.snd_width * mpi.snd_channels) / mpi.fps + 0.5f);
	if (bytes_per_frame == 0 || bytes_per_frame > MP4_AUDIO_ACCUM_SIZE)
		return;

	size_t total_bytes = 0;

	if (mpi.audio_leftover_size > 0)
	{
		memcpy(mpi.audio_accum, mpi.audio_leftover, mpi.audio_leftover_size);
		total_bytes = mpi.audio_leftover_size;
		mpi.audio_leftover_size = 0;
	}

	while (total_bytes < bytes_per_frame)
	{
		CMSampleBufferRef sample = [mpi.audio_output copyNextSampleBuffer];
		if (sample == NULL)
		{
			mpi.audio_at_end = true;
			break;
		}

		CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
		if (block != NULL)
		{
			const size_t len = CMBlockBufferGetDataLength(block);
			if (total_bytes + len <= MP4_AUDIO_ACCUM_SIZE)
			{
				CMBlockBufferCopyDataBytes(block, 0, len, mpi.audio_accum + total_bytes);
				total_bytes += len;
			}
		}

		CFRelease(sample);
	}

	if (total_bytes > bytes_per_frame && mpi.audio_leftover != NULL)
	{
		const size_t excess = total_bytes - bytes_per_frame;
		memcpy(mpi.audio_leftover, mpi.audio_accum + bytes_per_frame, excess);
		mpi.audio_leftover_size = excess;
		total_bytes = bytes_per_frame;
	}

	if (total_bytes > 0)
	{
		const int samples = (int)(total_bytes / (size_t)(mpi.snd_width * mpi.snd_channels));
		se.RawSamples(samples, mpi.snd_rate, mpi.snd_width, mpi.snd_channels,
			mpi.audio_accum, Cvar_VariableValue("s_volume"));
	}
}

qboolean MP4_Open(const char* filepath)
{
	@autoreleasepool
	{
		MP4_FreeState(false);

		strncpy_s(mpi.load_filepath, sizeof(mpi.load_filepath), filepath, sizeof(mpi.load_filepath) - 1);

		NSString* path = [NSString stringWithUTF8String:filepath];
		if (path == nil)
		{
			mpi.load_state = MP4_LOAD_FAILED;
			return false;
		}

		NSURL* url = [NSURL fileURLWithPath:path];
		mpi.asset = [[AVURLAsset alloc] initWithURL:url options:nil];
		if (mpi.asset == nil)
		{
			mpi.load_state = MP4_LOAD_FAILED;
			return false;
		}

		NSArray* video_tracks = [mpi.asset tracksWithMediaType:AVMediaTypeVideo];
		if (video_tracks.count == 0)
		{
			Com_Printf("MP4_Open: no video track in '%s'.\n", filepath);
			MP4_FreeState(false);
			mpi.load_state = MP4_LOAD_FAILED;
			return false;
		}

		NSError* error = nil;
		mpi.reader = [[AVAssetReader alloc] initWithAsset:mpi.asset error:&error];
		if (mpi.reader == nil)
		{
			Com_Printf("MP4_Open: AVAssetReader failed for '%s'.\n", filepath);
			MP4_FreeState(false);
			mpi.load_state = MP4_LOAD_FAILED;
			return false;
		}

		if (!MP4_InitVideoOutput([video_tracks objectAtIndex:0]))
		{
			Com_Printf("MP4_Open: failed to configure video output for '%s'.\n", filepath);
			MP4_FreeState(false);
			mpi.load_state = MP4_LOAD_FAILED;
			return false;
		}

		MP4_InitAudioOutput();

		if (![mpi.reader startReading])
		{
			Com_Printf("MP4_Open: failed to start AVAssetReader for '%s'.\n", filepath);
			MP4_FreeState(false);
			mpi.load_state = MP4_LOAD_FAILED;
			return false;
		}

		mpi.load_state = MP4_LOAD_OK;
		Com_Printf("MP4_Open: prepared '%s' with AVFoundation.\n", filepath);
		return true;
	}
}

qboolean MP4_IsLoading(void)
{
	return false;
}

qboolean MP4_FinishOpen(void)
{
	if (mpi.load_state != MP4_LOAD_OK)
	{
		Com_Printf("MP4_Open: failed to open '%s'.\n", mpi.load_filepath);
		MP4_FreeState(false);
		return false;
	}

	mpi.valid = true;
	re.DrawInitCinematic(mpi.vid_width, mpi.vid_height);

	Com_Printf("MP4_Open: %dx%d @ %.1f fps, audio %s.\n",
		mpi.vid_width, mpi.vid_height, mpi.fps,
		mpi.has_audio ? "on" : "off");

	return true;
}

void MP4_Shutdown(void)
{
	@autoreleasepool
	{
		MP4_FreeState(true);
	}
}

qboolean MP4_IsOpen(void)    { return mpi.valid; }
qboolean MP4_AtEnd(void)     { return mpi.at_end; }
int      MP4_GetWidth(void)  { return mpi.vid_width; }
int      MP4_GetHeight(void) { return mpi.vid_height; }
float    MP4_GetFPS(void)    { return mpi.fps > 0.0f ? mpi.fps : 15.0f; }
int      MP4_GetFrame(void)  { return mpi.frame; }

const byte* MP4_GetVideoFrame(void)
{
	return mpi.video_frame;
}

void MP4_NextFrame(void)
{
	if (!mpi.valid || mpi.at_end || mpi.video_output == nil)
		return;

	@autoreleasepool
	{
		CMSampleBufferRef sample = [mpi.video_output copyNextSampleBuffer];
		if (sample == NULL)
		{
			mpi.at_end = true;
			return;
		}

		CVImageBufferRef image = CMSampleBufferGetImageBuffer(sample);
		if (image != NULL)
		{
			CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);

			const byte* src = (const byte*)CVPixelBufferGetBaseAddress(image);
			const size_t src_stride = CVPixelBufferGetBytesPerRow(image);
			const size_t dst_stride = (size_t)mpi.vid_width * 4;

			for (int y = 0; y < mpi.vid_height; y++)
				memcpy(mpi.video_frame + (size_t)y * dst_stride, src + (size_t)y * src_stride, dst_stride);

			CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
		}

		CFRelease(sample);

		mpi.frame++;
		MP4_ReadAudioForFrame();
	}
}
