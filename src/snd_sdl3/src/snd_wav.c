//
// snd_wav.c
//
// Copyright 1998 Raven Software
//

#include "snd_wav.h"
#include "snd_main.h"
#include "snd_sdl3.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef struct
{
	const byte* data;
	size_t length;
} wav_chunk_t;

static uint16_t read_u16(const byte* data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const byte* data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static qboolean chunk_named(const wav_chunk_t* chunk, const char* name)
{
	return chunk->data != NULL && memcmp(chunk->data - 8, name, 4) == 0;
}

static qboolean next_chunk(const byte* wav, const size_t end, size_t* offset, wav_chunk_t* chunk)
{
	if (*offset > end || end - *offset < 8)
		return false;

	const size_t header = *offset;
	const uint32_t declared = read_u32(wav + header + 4);
	const size_t data_offset = header + 8;
	if ((size_t)declared > end - data_offset)
		return false;

	const size_t padded = (size_t)declared + (declared & 1u);
	if (padded < declared || padded > end - data_offset)
		return false;

	chunk->data = wav + data_offset;
	chunk->length = declared;
	*offset = data_offset + padded;
	return true;
}

static wavinfo_t GetWavinfo(const char* name, const byte* wav, const int wavlength)
{
	wavinfo_t info = { 0 };
	info.loopstart = -1;
	if (wav == NULL || wavlength < 12 || memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0)
	{
		si.Com_Printf("Sound %s is missing a complete RIFF/WAVE header\n", name);
		return info;
	}

	const uint32_t riff_length = read_u32(wav + 4);
	if (riff_length < 4 || (size_t)riff_length > (size_t)wavlength - 8)
	{
		si.Com_Printf("Sound %s has an invalid RIFF length\n", name);
		return info;
	}

	const size_t riff_end = 8 + (size_t)riff_length;
	wav_chunk_t format = { 0 };
	wav_chunk_t data = { 0 };
	wav_chunk_t cue = { 0 };
	wav_chunk_t list_after_cue = { 0 };
	size_t offset = 12;
	while (offset < riff_end)
	{
		wav_chunk_t chunk;
		if (!next_chunk(wav, riff_end, &offset, &chunk))
		{
			si.Com_Printf("Sound %s has a truncated RIFF chunk\n", name);
			return info;
		}
		if (format.data == NULL && chunk_named(&chunk, "fmt "))
			format = chunk;
		else if (data.data == NULL && chunk_named(&chunk, "data"))
			data = chunk;
		else if (cue.data == NULL && chunk_named(&chunk, "cue "))
			cue = chunk;
		else if (cue.data != NULL && list_after_cue.data == NULL && chunk_named(&chunk, "LIST"))
			list_after_cue = chunk;
	}

	if (format.data == NULL || format.length < 16)
	{
		si.Com_Printf("Sound %s is missing a complete fmt chunk\n", name);
		return info;
	}
	if (read_u16(format.data) != 1)
	{
		si.Com_Printf("Sound %s is not Microsoft PCM\n", name);
		return info;
	}

	const uint16_t channels = read_u16(format.data + 2);
	const uint32_t rate = read_u32(format.data + 4);
	const uint32_t byte_rate = read_u32(format.data + 8);
	const uint16_t block_align = read_u16(format.data + 12);
	const uint16_t bits = read_u16(format.data + 14);
	const uint32_t width = bits / 8u;
	if ((channels != 1 && channels != 2) || (bits != 8 && bits != 16) || rate == 0 || rate > 192000 ||
		block_align != channels * width || (uint64_t)rate * block_align != byte_rate)
	{
		si.Com_Printf("Sound %s has an invalid PCM format\n", name);
		return info;
	}
	if (data.data == NULL || data.length == 0 || data.length % block_align != 0 ||
		data.length / width > INT_MAX || data.data - wav > INT_MAX)
	{
		si.Com_Printf("Sound %s is missing valid sample data\n", name);
		return info;
	}

	info.channels = channels;
	info.rate = (int)rate;
	info.width = (int)width;
	info.samples = (int)(data.length / width);
	info.dataofs = (int)(data.data - wav);

	if (cue.data != NULL)
	{
		if (cue.length < 28 || read_u32(cue.data) == 0 || read_u32(cue.data + 24) > INT_MAX)
		{
			si.Com_Printf("Sound %s has an invalid cue chunk\n", name);
			memset(&info, 0, sizeof(info));
			return info;
		}
		info.loopstart = (int)read_u32(cue.data + 24);
		if (info.loopstart > info.samples)
		{
			si.Com_Printf("Sound %s has a bad loop start\n", name);
			memset(&info, 0, sizeof(info));
			return info;
		}
		if (list_after_cue.data != NULL && list_after_cue.length >= 24 &&
			memcmp(list_after_cue.data + 20, "mark", 4) == 0)
		{
			const uint32_t loop_samples = read_u32(list_after_cue.data + 16);
			if (loop_samples > INT_MAX || info.loopstart > INT_MAX - (int)loop_samples)
			{
				si.Com_Printf("Sound %s has an overflowing loop length\n", name);
				memset(&info, 0, sizeof(info));
				return info;
			}
			const int loop_end = info.loopstart + (int)loop_samples;
			if (loop_end > info.samples)
			{
				si.Com_Printf("Sound %s has a bad loop length\n", name);
				memset(&info, 0, sizeof(info));
				return info;
			}
			info.samples = loop_end;
		}
	}

	return info;
}

#ifdef H2_DOS
qboolean H2DOS_WavFileIsValid(const char* name)
{
	byte* data = NULL;
	const int length = si.FS_LoadFile(name, (void**)&data);
	if (data == NULL || length <= 0)
	{
		if (data != NULL)
			si.FS_FreeFile(data);
		return false;
	}
	const wavinfo_t info = GetWavinfo(name, data, length);
	si.FS_FreeFile(data);
	return info.channels == 1 || info.channels == 2;
}
#endif

sfxcache_t* S_LoadSound(sfx_t* s)
{
	char name_buffer[MAX_QPATH];

	// See if still in memory.
	if (s->cache != NULL)
		return s->cache;

	// Load it in. //H2: no s->truename check.
	if (s->name[0] == '#')
		strcpy_s(name_buffer, sizeof(name_buffer), &s->name[1]); //mxd. strcpy -> strcpy_s.
	else if (s->name[0] == '*') //H2: check done in different place.
		return NULL;
	else
		Com_sprintf(name_buffer, sizeof(name_buffer), "%s/%s", s_sounddir->string, s->name); //H2: use s_sounddir cvar.

	byte* data;
	const int size = si.FS_LoadFile(name_buffer, (void**)&data);

	if (data == NULL || size <= 0)
	{
		si.Com_DPrintf("S_LoadSound: couldn't load '%s'\n", name_buffer); //H2: function name added to message.
		if (data != NULL)
			si.FS_FreeFile(data);
		return NULL;
	}

	const wavinfo_t info = GetWavinfo(s->name, data, size);

	if (info.channels != 1 && info.channels != 2) // YQ2: also stereo.
	{
		si.Com_Printf("S_LoadSound: skipping '%s' (invalid WAV)\n", s->name);
		si.FS_FreeFile(data);
		return NULL;
	}

	if (!SNDSDL3_Cache(s, &info, &data[info.dataofs]))
	{
		si.Com_Printf("S_LoadSound: couldn't precache '%s'\n", name_buffer);
		si.FS_FreeFile(data);
		return NULL;
	}

	si.FS_FreeFile(data);
	return s->cache;
}
