#include "h2dos_ui.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define LOG_PATH "HELLODOS.LOG"
#define MARKER_PATH "H2DOS.OK"
#define RUN_TIME_MS 1500
#define TONE_RATE 22050
#define TONE_SAMPLES (TONE_RATE / 4)

static FILE *log_file;

static void sanitize_run_id(char *output, size_t output_size, const char *input)
{
	size_t i;

	if (!input || !input[0]) {
		input = "MANUAL";
	}

	for (i = 0; i + 1 < output_size && input[i]; ++i) {
		unsigned char ch = (unsigned char)input[i];
		output[i] = isalnum(ch) ? (char)toupper(ch) : '_';
	}
	output[i] = '\0';
}

static int fail(const char *stage)
{
	if (log_file) {
		fprintf(log_file, "stage=%s\nerror=%s\nstatus=FAIL\n", stage, SDL_GetError());
		fclose(log_file);
		log_file = NULL;
	}
	SDL_Quit();
	return 1;
}

static void paint_test_pattern(SDL_Surface *surface, Uint32 frame)
{
	static const Uint8 colors[][3] = {
		{20, 20, 70}, {30, 80, 160}, {30, 150, 160}, {50, 180, 90},
		{210, 180, 40}, {220, 100, 40}, {180, 45, 55}, {120, 40, 130}
	};
	const int band_width = surface->w / (int)SDL_arraysize(colors);
	int i;

	for (i = 0; i < (int)SDL_arraysize(colors); ++i) {
		SDL_Rect band = {i * band_width, 0, i == (int)SDL_arraysize(colors) - 1 ? surface->w - i * band_width : band_width, surface->h};
		Uint32 color = SDL_MapSurfaceRGB(surface, colors[i][0], colors[i][1], colors[i][2]);
		SDL_FillSurfaceRect(surface, &band, color);
	}

	{
		SDL_Rect pulse = {(int)(frame % (Uint32)(surface->w - 40)), surface->h / 2 - 20, 40, 40};
		Uint32 white = SDL_MapSurfaceRGB(surface, 240, 240, 240);
		SDL_FillSurfaceRect(surface, &pulse, white);
	}
}

int main(int argc, char **argv)
{
	char run_id[32];
	SDL_Window *window = NULL;
	SDL_Surface *surface = NULL;
	SDL_AudioStream *audio = NULL;
	SDL_AudioSpec audio_spec = {SDL_AUDIO_S16, 1, TONE_RATE};
	Sint16 tone[TONE_SAMPLES];
	const bool *keyboard;
	const char *video_driver;
	const char *audio_driver;
	Uint64 started;
	Uint32 event_count = 0;
	int keyboard_keys = 0;
	int i;

	sanitize_run_id(run_id, sizeof(run_id), argc > 1 ? argv[1] : NULL);
	remove(MARKER_PATH);
	log_file = fopen(LOG_PATH, "w");
	if (!log_file) {
		return 2;
	}
	fprintf(log_file, "hellodos=SDL3-DOS-MILESTONE-0\nrun-id=%s\nstage=starting\n", run_id);
	fflush(log_file);

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
		return fail("SDL_Init");
	}

	video_driver = SDL_GetCurrentVideoDriver();
	audio_driver = SDL_GetCurrentAudioDriver();
	if (!video_driver || strcmp(video_driver, "vesa") != 0) {
		SDL_SetError("expected DOS VESA driver, got %s", video_driver ? video_driver : "(null)");
		return fail("video-driver");
	}
	if (!audio_driver || strcmp(audio_driver, "soundblaster") != 0) {
		SDL_SetError("expected DOS Sound Blaster driver, got %s", audio_driver ? audio_driver : "(null)");
		return fail("audio-driver");
	}

	window = SDL_CreateWindow("Heretic II DOS SDL3 proof", 320, 200, 0);
	if (!window) {
		return fail("SDL_CreateWindow");
	}
	surface = SDL_GetWindowSurface(window);
	if (!surface) {
		SDL_DestroyWindow(window);
		return fail("SDL_GetWindowSurface");
	}
	if (!H2DOS_PrepareSurfacePalette(surface)) {
		SDL_DestroyWindow(window);
		return fail("surface-palette");
	}
	paint_test_pattern(surface, 0);
	if (!SDL_UpdateWindowSurface(window)) {
		SDL_DestroyWindow(window);
		return fail("SDL_UpdateWindowSurface");
	}

	keyboard = SDL_GetKeyboardState(&keyboard_keys);
	if (!keyboard || keyboard_keys <= 0) {
		SDL_DestroyWindow(window);
		SDL_SetError("keyboard state unavailable");
		return fail("input-init");
	}
	SDL_GetMouseState(NULL, NULL);
	SDL_PumpEvents();

	for (i = 0; i < TONE_SAMPLES; ++i) {
		tone[i] = ((i / 25) & 1) ? 5000 : -5000;
	}
	audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, NULL, NULL);
	if (!audio) {
		SDL_DestroyWindow(window);
		return fail("SDL_OpenAudioDeviceStream");
	}
	if (!SDL_PutAudioStreamData(audio, tone, sizeof(tone)) ||
		!SDL_FlushAudioStream(audio) ||
		!SDL_ResumeAudioStreamDevice(audio)) {
		SDL_DestroyAudioStream(audio);
		SDL_DestroyWindow(window);
		return fail("audio-playback");
	}

	started = SDL_GetTicks();
	while (SDL_GetTicks() - started < RUN_TIME_MS) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			++event_count;
		}
		paint_test_pattern(surface, (Uint32)((SDL_GetTicks() - started) / 8));
		if (!SDL_UpdateWindowSurface(window)) {
			SDL_DestroyAudioStream(audio);
			SDL_DestroyWindow(window);
			return fail("video-present-loop");
		}
		SDL_Delay(16);
	}

	fprintf(log_file,
		"video-driver=%s\naudio-driver=%s\nvideo-size=%dx%d\naudio-queued-ms=250\nkeyboard-keys=%d\nmouse-state-read=1\nevents-polled=%lu\nruntime-ms=%lu\nstatus=PASS\n",
		video_driver,
		audio_driver,
		surface->w,
		surface->h,
		keyboard_keys,
		(unsigned long)event_count,
		(unsigned long)(SDL_GetTicks() - started));
	fclose(log_file);
	log_file = NULL;

	{
		FILE *marker = fopen(MARKER_PATH, "w");
		if (!marker) {
			SDL_DestroyAudioStream(audio);
			SDL_DestroyWindow(window);
			SDL_Quit();
			return 3;
		}
		fprintf(marker, "HELLODOS PASS\nrun-id=%s\n", run_id);
		fclose(marker);
	}

	SDL_DestroyAudioStream(audio);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
