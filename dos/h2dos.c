#include "h2dos_platform.h"
#include "qcommon.h"
#include "h2dos_ui.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define LOG_PATH "H2DOS.LOG"
#define MARKER_PATH "H2ENG.OK"
#define SCREENSHOT_PATH "H2DOS.PPM"
#define RUN_TIME_MS 2000
#define TONE_RATE 22050
#define TONE_SAMPLES (TONE_RATE / 4)
#define RESOURCE_TEXT "H2DOS RESOURCE PATH PASS\n"

typedef struct shell_input_s {
	int selected_item;
	float mouse_x;
	float mouse_y;
	Uint32 events;
	Uint32 key_down;
	Uint32 key_up;
	Uint32 mouse_motion;
	Uint32 mouse_button;
	bool running;
} shell_input_t;

static FILE *log_file;

static void sanitize_run_id(char *output, size_t output_size, const char *input)
{
	size_t i;

	if (!input || !input[0])
		input = "MANUAL";

	for (i = 0; i + 1 < output_size && input[i]; ++i) {
		const unsigned char ch = (unsigned char)input[i];
		output[i] = isalnum(ch) ? (char)toupper(ch) : '_';
	}
	output[i] = '\0';
}

static int fail(const char *stage, const char *detail)
{
	if (log_file) {
		fprintf(log_file, "stage=%s\nerror=%s\nstatus=FAIL\n", stage, detail ? detail : SDL_GetError());
		fclose(log_file);
		log_file = NULL;
	}
	H2DOS_SetLog(NULL);
	SDL_Quit();
	return 1;
}

static int original_game_data_present(void)
{
	static const char *names[] = {"Htic2-0.pak", "Htic2-1.pak"};
	int i;

	for (i = 0; i < (int)SDL_arraysize(names); ++i) {
		char path[MAX_OSPATH];
		FILE *file;
		Com_sprintf(path, sizeof(path), "%s/%s", FS_Gamedir(), names[i]);
		file = fopen(path, "rb");
		if (!file)
			return 0;
		fclose(file);
	}

	return 1;
}

static int verify_engine_core(void)
{
	Cbuf_Init();
	Cmd_Init();
	Cvar_Init();

	Cbuf_AddText(
		"set h2_port dos\n"
		"set h2_counter 41\n"
		"alias h2_promote \"set h2_counter 42\"\n"
		"h2_promote\n"
		"set h2_macro $h2_counter\n"
		"set h2_wait before\n"
		"wait\n"
		"set h2_wait after\n");
	Cbuf_Execute();

	if (strcmp(Cvar_VariableString("h2_port"), "dos") != 0 ||
		Cvar_VariableInt("h2_counter") != 42 ||
		strcmp(Cvar_VariableString("h2_macro"), "42") != 0 ||
		strcmp(Cvar_VariableString("h2_wait"), "before") != 0)
		return 0;

	Cbuf_Execute();
	return strcmp(Cvar_VariableString("h2_wait"), "after") == 0;
}

static int verify_resource_path(void)
{
	void *data = NULL;
	const int length = FS_LoadFile("doscheck.txt", &data);
	const int expected_length = (int)strlen(RESOURCE_TEXT);
	const int passed = length == expected_length && file_from_pak &&
		data && memcmp(data, RESOURCE_TEXT, (size_t)expected_length) == 0;

	if (data)
		FS_FreeFile(data);
	return passed;
}

static void handle_event(const SDL_Event *event, shell_input_t *input)
{
	++input->events;
	switch (event->type) {
		case SDL_EVENT_QUIT:
			input->running = false;
			break;
		case SDL_EVENT_KEY_DOWN:
			++input->key_down;
			if (event->key.key == SDLK_ESCAPE)
				input->running = false;
			else if (event->key.key == SDLK_UP)
				input->selected_item = (input->selected_item + 3) % 4;
			else if (event->key.key == SDLK_DOWN)
				input->selected_item = (input->selected_item + 1) % 4;
			break;
		case SDL_EVENT_KEY_UP:
			++input->key_up;
			break;
		case SDL_EVENT_MOUSE_MOTION:
			++input->mouse_motion;
			input->mouse_x = event->motion.x;
			input->mouse_y = event->motion.y;
			if (input->mouse_x >= 8.0f && input->mouse_x < 103.0f &&
				input->mouse_y >= 29.0f && input->mouse_y < 124.0f) {
				input->selected_item = ((int)input->mouse_y - 29) / 21;
				if (input->selected_item > 3)
					input->selected_item = 3;
			}
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			++input->mouse_button;
			break;
		default:
			break;
	}
}

static int save_screenshot(SDL_Surface *surface)
{
	FILE *file = fopen(SCREENSHOT_PATH, "wb");
	int x;
	int y;

	if (!file)
		return 0;
	fprintf(file, "P6\n%d %d\n255\n", surface->w, surface->h);
	for (y = 0; y < surface->h; ++y) {
		for (x = 0; x < surface->w; ++x) {
			Uint8 pixel[3];
			if (!SDL_ReadSurfacePixel(surface, x, y, &pixel[0], &pixel[1], &pixel[2], NULL) ||
				fwrite(pixel, 1, sizeof(pixel), file) != sizeof(pixel)) {
				fclose(file);
				return 0;
			}
		}
	}

	return fclose(file) == 0;
}

static int verify_event_pipeline(shell_input_t *input)
{
	SDL_Event event;

	SDL_zero(event);
	event.type = SDL_EVENT_KEY_DOWN;
	event.key.key = SDLK_DOWN;
	if (!SDL_PushEvent(&event))
		return 0;

	SDL_zero(event);
	event.type = SDL_EVENT_KEY_UP;
	event.key.key = SDLK_DOWN;
	if (!SDL_PushEvent(&event))
		return 0;

	SDL_zero(event);
	event.type = SDL_EVENT_MOUSE_MOTION;
	event.motion.x = 50.0f;
	event.motion.y = 76.0f;
	if (!SDL_PushEvent(&event))
		return 0;

	SDL_zero(event);
	event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
	event.button.button = SDL_BUTTON_LEFT;
	if (!SDL_PushEvent(&event))
		return 0;

	while (SDL_PollEvent(&event))
		handle_event(&event, input);

	return input->key_down >= 1 && input->key_up >= 1 &&
		input->mouse_motion >= 1 && input->mouse_button >= 1;
}

int main(int argc, char **argv)
{
	char run_id[32];
	SDL_Window *window = NULL;
	SDL_Surface *surface = NULL;
	SDL_AudioStream *audio = NULL;
	SDL_AudioSpec audio_spec = {SDL_AUDIO_S16, 1, TONE_RATE};
	Sint16 tone[TONE_SAMPLES];
	shell_input_t input = {0, -1.0f, -1.0f, 0, 0, 0, 0, 0, true};
	const bool *keyboard;
	const char *video_driver;
	const char *audio_driver;
	Uint64 started;
	Uint32 setup_events;
	int keyboard_keys = 0;
	int has_game_data;
	int i;

	sanitize_run_id(run_id, sizeof(run_id), argc > 1 ? argv[1] : NULL);
	remove(MARKER_PATH);
	remove(SCREENSHOT_PATH);
	log_file = fopen(LOG_PATH, "w");
	if (!log_file)
		return 2;

	H2DOS_InitArgs(argc, argv);
	H2DOS_SetLog(log_file);
	Set_Com_Printf(Com_Printf);
	fprintf(log_file,
		"h2dos=HERETIC2-DOS-VERTICAL-SLICE-2\n"
		"run-id=%s\n"
		"binding=monolithic-static\n"
		"opengl=disabled\nopenal=disabled\nffmpeg=disabled\nnetwork=disabled\n"
		"stage=starting\n",
		run_id);
	fflush(log_file);

	if (!verify_engine_core())
		return fail("engine-core", "command/cvar self-test failed");

	fprintf(log_file,
		"core-command-buffer=PASS\n"
		"core-cvar=PASS\n"
		"core-alias=PASS\n"
		"core-macro-expansion=PASS\n"
		"core-wait=PASS\n");

	FS_InitFilesystem();
	if (!verify_resource_path())
		return fail("engine-resource", "synthetic PAK fixture was not loaded through FS_LoadFile");
	fprintf(log_file,
		"engine-filesystem=PASS\n"
		"resource-container=H2TEST/base/Htic2-9.pak\n"
		"resource-entry=doscheck.txt\n"
		"resource-kind=synthetic-non-game-test-fixture\n");

	has_game_data = original_game_data_present();
	fprintf(log_file, "original-game-data=%s\n", has_game_data ? "detected-not-loaded" : "absent");
	fflush(log_file);

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
		return fail("SDL_Init", NULL);

	video_driver = SDL_GetCurrentVideoDriver();
	audio_driver = SDL_GetCurrentAudioDriver();
	if (!video_driver || strcmp(video_driver, "vesa") != 0)
		return fail("video-driver", video_driver ? video_driver : "no video driver");
	if (!audio_driver || strcmp(audio_driver, "soundblaster") != 0)
		return fail("audio-driver", audio_driver ? audio_driver : "no audio driver");

	window = SDL_CreateWindow("Heretic II DOS vertical slice", 320, 200, 0);
	if (!window)
		return fail("SDL_CreateWindow", NULL);
	surface = SDL_GetWindowSurface(window);
	if (!surface) {
		SDL_DestroyWindow(window);
		return fail("SDL_GetWindowSurface", NULL);
	}
	if (!H2DOS_PrepareSurfacePalette(surface)) {
		SDL_DestroyWindow(window);
		return fail("surface-palette", NULL);
	}
	fprintf(log_file,
		"surface-format=%s\n"
		"surface-palette=%s\n"
		"surface-map-sample=%lu\n",
		SDL_GetPixelFormatName(surface->format),
		SDL_GetSurfacePalette(surface) ? "available" : "none",
		(unsigned long)SDL_MapSurfaceRGB(surface, 230, 213, 175));
	fflush(log_file);

	keyboard = SDL_GetKeyboardState(&keyboard_keys);
	if (!keyboard || keyboard_keys <= 0) {
		SDL_DestroyWindow(window);
		return fail("input-init", "keyboard state unavailable");
	}
	SDL_GetMouseState(&input.mouse_x, &input.mouse_y);
	SDL_PumpEvents();
	if (!verify_event_pipeline(&input)) {
		SDL_DestroyWindow(window);
		return fail("input-event-pipeline", "keyboard/mouse event dispatch failed");
	}
	setup_events = input.events;
	input.events = input.key_down = input.key_up = input.mouse_motion = input.mouse_button = 0;

	H2DOS_DrawShell(surface, 0, input.selected_item, input.mouse_x, input.mouse_y);
	if (!save_screenshot(surface)) {
		SDL_DestroyWindow(window);
		return fail("save-screenshot", "could not write RGB PPM screenshot");
	}
	if (!SDL_UpdateWindowSurface(window)) {
		SDL_DestroyWindow(window);
		return fail("SDL_UpdateWindowSurface", NULL);
	}

	for (i = 0; i < TONE_SAMPLES; ++i)
		tone[i] = ((i / 25) & 1) ? 4500 : -4500;
	audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, NULL, NULL);
	if (!audio) {
		SDL_DestroyWindow(window);
		return fail("SDL_OpenAudioDeviceStream", NULL);
	}
	if (!SDL_PutAudioStreamData(audio, tone, sizeof(tone)) ||
		!SDL_FlushAudioStream(audio) ||
		!SDL_ResumeAudioStreamDevice(audio)) {
		SDL_DestroyAudioStream(audio);
		SDL_DestroyWindow(window);
		return fail("audio-playback", NULL);
	}

	started = SDL_GetTicks();
	while (input.running && SDL_GetTicks() - started < RUN_TIME_MS) {
		SDL_Event event;
		while (SDL_PollEvent(&event))
			handle_event(&event, &input);
		H2DOS_DrawShell(surface, (Uint32)((SDL_GetTicks() - started) / 8), input.selected_item, input.mouse_x, input.mouse_y);
		if (!SDL_UpdateWindowSurface(window)) {
			SDL_DestroyAudioStream(audio);
			SDL_DestroyWindow(window);
			return fail("video-present-loop", NULL);
		}
		SDL_Delay(16);
	}

	fprintf(log_file,
		"video-driver=%s\n"
		"audio-driver=%s\n"
		"video-size=%dx%d\n"
		"ui=interactive-diagnostic-shell\n"
		"screenshot=%s\n"
		"frames-presented=bounded-loop\n"
		"audio-queued-ms=250\n"
		"keyboard-state-keys=%d\n"
		"mouse-state-read=PASS\n"
		"input-event-pipeline=PASS\n"
		"input-pipeline-events=%lu\n"
		"runtime-key-down-events=%lu\n"
		"runtime-key-up-events=%lu\n"
		"runtime-mouse-motion-events=%lu\n"
		"runtime-mouse-button-events=%lu\n"
		"runtime-events-polled=%lu\n"
		"runtime-ms=%lu\n"
		"status=PASS\n",
		video_driver,
		audio_driver,
		surface->w,
		surface->h,
		SCREENSHOT_PATH,
		keyboard_keys,
		(unsigned long)setup_events,
		(unsigned long)input.key_down,
		(unsigned long)input.key_up,
		(unsigned long)input.mouse_motion,
		(unsigned long)input.mouse_button,
		(unsigned long)input.events,
		(unsigned long)(SDL_GetTicks() - started));
	fclose(log_file);
	log_file = NULL;
	H2DOS_SetLog(NULL);

	{
		FILE *marker = fopen(MARKER_PATH, "w");
		if (!marker) {
			SDL_DestroyAudioStream(audio);
			SDL_DestroyWindow(window);
			SDL_Quit();
			return 3;
		}
		fprintf(marker, "H2DOS VERTICAL SLICE PASS\nrun-id=%s\n", run_id);
		fclose(marker);
	}

	SDL_DestroyAudioStream(audio);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
