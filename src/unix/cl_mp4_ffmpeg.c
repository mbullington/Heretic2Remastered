// cl_mp4_ffmpeg.c -- MP4/MKV cinematic backend for Unix via FFmpeg/libavcodec.

#include "client.h"
#include "cl_mp4.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>

#define MP4_LOAD_IDLE     0
#define MP4_LOAD_OK       2
#define MP4_LOAD_FAILED   3

typedef struct MP4PlaybackInfo_s
{
	AVFormatContext* format;
	AVCodecContext* codec;
	AVPacket* packet;
	AVFrame* frame_data;
	struct SwsContext* sws;
	int video_stream;

	int frame;
	float fps;
	int vid_width;
	int vid_height;
	byte* video_frame;

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

	if (mpi.sws != NULL)
	{
		sws_freeContext(mpi.sws);
		mpi.sws = NULL;
	}
	if (mpi.frame_data != NULL)
	{
		av_frame_free(&mpi.frame_data);
		mpi.frame_data = NULL;
	}
	if (mpi.packet != NULL)
	{
		av_packet_free(&mpi.packet);
		mpi.packet = NULL;
	}
	if (mpi.codec != NULL)
	{
		avcodec_free_context(&mpi.codec);
		mpi.codec = NULL;
	}
	if (mpi.format != NULL)
	{
		avformat_close_input(&mpi.format);
		mpi.format = NULL;
	}
	if (mpi.video_frame != NULL)
	{
		free(mpi.video_frame);
		mpi.video_frame = NULL;
	}

	memset(&mpi, 0, sizeof(mpi));
}

static float MP4_StreamFPS(const AVStream* stream)
{
	AVRational rate = stream->avg_frame_rate;
	if (rate.num == 0 || rate.den == 0)
		rate = stream->r_frame_rate;
	if (rate.num == 0 || rate.den == 0)
		return 15.0f;

	const double fps = av_q2d(rate);
	return (fps > 0.0) ? (float)fps : 15.0f;
}

static qboolean MP4_CopyDecodedFrame(void)
{
	uint8_t* dst_data[4] = { mpi.video_frame, NULL, NULL, NULL };
	int dst_linesize[4] = { mpi.vid_width * 4, 0, 0, 0 };

	const int scaled = sws_scale(mpi.sws,
		(const uint8_t* const*)mpi.frame_data->data,
		mpi.frame_data->linesize,
		0,
		mpi.vid_height,
		dst_data,
		dst_linesize);

	av_frame_unref(mpi.frame_data);
	return scaled == mpi.vid_height;
}

static qboolean MP4_ReceiveVideoFrame(void)
{
	while (true)
	{
		const int ret = avcodec_receive_frame(mpi.codec, mpi.frame_data);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			return false;
		if (ret < 0)
			return false;

		if (MP4_CopyDecodedFrame())
			return true;
	}
}

qboolean MP4_Open(const char* filepath)
{
	MP4_FreeState(false);
	strncpy_s(mpi.load_filepath, sizeof(mpi.load_filepath), filepath, sizeof(mpi.load_filepath) - 1);
	mpi.video_stream = -1;

	if (avformat_open_input(&mpi.format, filepath, NULL, NULL) < 0)
	{
		Com_Printf("MP4_Open: FFmpeg could not open '%s'.\n", filepath);
		mpi.load_state = MP4_LOAD_FAILED;
		return false;
	}

	if (avformat_find_stream_info(mpi.format, NULL) < 0)
	{
		Com_Printf("MP4_Open: FFmpeg could not read stream info for '%s'.\n", filepath);
		MP4_FreeState(false);
		mpi.load_state = MP4_LOAD_FAILED;
		return false;
	}

	mpi.video_stream = av_find_best_stream(mpi.format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (mpi.video_stream < 0)
	{
		Com_Printf("MP4_Open: no video stream in '%s'.\n", filepath);
		MP4_FreeState(false);
		mpi.load_state = MP4_LOAD_FAILED;
		return false;
	}

	AVStream* stream = mpi.format->streams[mpi.video_stream];
	const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
	if (decoder == NULL)
	{
		Com_Printf("MP4_Open: no FFmpeg decoder for '%s'.\n", filepath);
		MP4_FreeState(false);
		mpi.load_state = MP4_LOAD_FAILED;
		return false;
	}

	mpi.codec = avcodec_alloc_context3(decoder);
	if (mpi.codec == NULL || avcodec_parameters_to_context(mpi.codec, stream->codecpar) < 0 || avcodec_open2(mpi.codec, decoder, NULL) < 0)
	{
		Com_Printf("MP4_Open: failed to open FFmpeg decoder for '%s'.\n", filepath);
		MP4_FreeState(false);
		mpi.load_state = MP4_LOAD_FAILED;
		return false;
	}

	mpi.vid_width = mpi.codec->width;
	mpi.vid_height = mpi.codec->height;
	mpi.fps = MP4_StreamFPS(stream);

	if (mpi.vid_width <= 0 || mpi.vid_height <= 0)
	{
		MP4_FreeState(false);
		mpi.load_state = MP4_LOAD_FAILED;
		return false;
	}

	mpi.sws = sws_getContext(mpi.vid_width, mpi.vid_height, mpi.codec->pix_fmt,
		mpi.vid_width, mpi.vid_height, AV_PIX_FMT_BGRA,
		SWS_BILINEAR, NULL, NULL, NULL);
	mpi.packet = av_packet_alloc();
	mpi.frame_data = av_frame_alloc();
	mpi.video_frame = (byte*)malloc((size_t)(mpi.vid_width * mpi.vid_height * 4));

	if (mpi.sws == NULL || mpi.packet == NULL || mpi.frame_data == NULL || mpi.video_frame == NULL)
	{
		MP4_FreeState(false);
		mpi.load_state = MP4_LOAD_FAILED;
		return false;
	}

	mpi.load_state = MP4_LOAD_OK;
	Com_Printf("MP4_Open: prepared '%s' with FFmpeg.\n", filepath);
	return true;
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

	Com_Printf("MP4_Open: %dx%d @ %.1f fps, audio off.\n",
		mpi.vid_width, mpi.vid_height, mpi.fps);

	return true;
}

void MP4_Shutdown(void)
{
	MP4_FreeState(true);
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
	if (!mpi.valid || mpi.at_end || mpi.codec == NULL)
		return;

	if (MP4_ReceiveVideoFrame())
	{
		mpi.frame++;
		return;
	}

	while (true)
	{
		const int read = av_read_frame(mpi.format, mpi.packet);
		if (read < 0)
		{
			avcodec_send_packet(mpi.codec, NULL);
			if (MP4_ReceiveVideoFrame())
			{
				mpi.frame++;
				return;
			}

			mpi.at_end = true;
			return;
		}

		if (mpi.packet->stream_index != mpi.video_stream)
		{
			av_packet_unref(mpi.packet);
			continue;
		}

		const int sent = avcodec_send_packet(mpi.codec, mpi.packet);
		av_packet_unref(mpi.packet);

		if (sent < 0 && sent != AVERROR(EAGAIN))
			continue;

		if (MP4_ReceiveVideoFrame())
		{
			mpi.frame++;
			return;
		}
	}
}
