#include "qcommon.h"
#include "Angles.h"
#include "Angles.h"
#include "p_dll.h"
#include "server.h"
#include "clfx_dll.h"
#include "cl_skeletons.h"
#include "Reference.h"
#include "q_Surface.h"
#include "q_Sprite.h"
#include "ParticleFlags.h"
#include "Vector.h"
#include "ref_soft_public.h"
#include "snd_sdl3.h"
#include "snd_wav.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define H2DOS_LOG "SILVER.LOG"
#define H2DOS_MARKER "SILVER.OK"
#define H2DOS_ZONE_TAG 0x2d05
#define H2DOS_MIN_FRAME_USEC 16667
#define H2DOS_BENCH_RNG_SEED 1337u

// DOS command lines are limited to ~126 characters by the PSP, so long proof
// invocations also accept DJGPP environment variables as fallbacks.
static qboolean argument_flag(const int argc, char** argv, const char* name, const char* env)
{
	for (int i = 1; i < argc; ++i)
		if (strcmp(argv[i], name) == 0)
			return true;
	return env != NULL && getenv(env) != NULL;
}

static int argument_value(const int argc, char** argv, const char* name, const char* env, const int fallback)
{
	for (int i = 1; i + 1 < argc; ++i)
		if (strcmp(argv[i], name) == 0)
			return atoi(argv[i + 1]);
	const char* value = env != NULL ? getenv(env) : NULL;
	return value != NULL ? atoi(value) : fallback;
}

static const char* argument_string(const int argc, char** argv, const char* name, const char* env, const char* fallback)
{
	for (int i = 1; i + 1 < argc; ++i)
		if (strcmp(argv[i], name) == 0)
			return argv[i + 1];
	const char* value = env != NULL ? getenv(env) : NULL;
	return value != NULL ? value : fallback;
}

static void log_append(const char* format, ...)
{
	FILE* log = fopen(H2DOS_LOG, "a");
	if (log == NULL)
		return;

	va_list arguments;
	va_start(arguments, format);
	vfprintf(log, format, arguments);
	va_end(arguments);
	fclose(log);
}

static void verify_fixture(void)
{
	void* data = NULL;
	const int length = FS_LoadFile("SYNTHETIC-NOT-GAME.txt", &data);
	static const char identity[] = "Heretic2Remastered DOS synthetic engine-path fixture.";
	if (length < (int)sizeof(identity) - 1 || data == NULL ||
		memcmp(data, identity, sizeof(identity) - 1) != 0)
		Sys_Error("Synthetic DOS fixture identity check failed");
	const unsigned checksum = Com_BlockChecksum(data, length);
	FS_FreeFile(data);
	log_append("fixture-filesystem=PASS\nfixture-block-checksum=%08x\n", checksum);
}

static void verify_mounted_paks(const char* basedir)
{
	char pattern[MAX_OSPATH];
	int pak_count = 0;
	Com_sprintf(pattern, sizeof(pattern), "%s/base/*.pak", basedir);
	for (char* path = Sys_FindFirst(pattern, 0, SFF_SUBDIR); path != NULL; path = Sys_FindNext(0, SFF_SUBDIR))
	{
		const char* slash = strrchr(path, '/');
		const char* backslash = strrchr(path, '\\');
		const char* name = path;
		if (slash != NULL && (backslash == NULL || slash > backslash))
			name = slash + 1;
		else if (backslash != NULL)
			name = backslash + 1;
		if (Q_stricmp(name, "Htic2-9.pak") != 0)
		{
			Sys_FindClose();
			Sys_Error("Unexpected PAK mounted in synthetic proof: %s", path);
		}
		++pak_count;
	}
	Sys_FindClose();
	if (pak_count != 1)
		Sys_Error("Synthetic proof expected exactly one fixture PAK, found %d", pak_count);
	log_append("input-pak-files=%d\nmounted-unexpected-paks=absent\noriginal-game-data=absent\n", pak_count);
}

static long file_size_bytes(const char* path)
{
	FILE* file = fopen(path, "rb");
	if (file == NULL)
		Sys_Error("Real-data proof could not open allowlisted PAK: %s", path);
	fseek(file, 0, SEEK_END);
	const long size = ftell(file);
	fclose(file);
	return size;
}

static void verify_realdata_mounts(const char* basedir)
{
	static const char* allowlisted[] = { "Htic2-0.pak", "Htic2-1.pak" };
	char pattern[MAX_OSPATH];
	int pak_count = 0;
	int allowlisted_count = 0;
	Com_sprintf(pattern, sizeof(pattern), "%s/base/*.pak", basedir);
	for (char* path = Sys_FindFirst(pattern, 0, SFF_SUBDIR); path != NULL; path = Sys_FindNext(0, SFF_SUBDIR))
	{
		const char* slash = strrchr(path, '/');
		const char* backslash = strrchr(path, '\\');
		const char* name = path;
		if (slash != NULL && (backslash == NULL || slash > backslash))
			name = slash + 1;
		else if (backslash != NULL)
			name = backslash + 1;
		qboolean allowed = false;
		for (int i = 0; i < (int)(sizeof(allowlisted) / sizeof(allowlisted[0])); ++i)
			if (Q_stricmp(name, allowlisted[i]) == 0)
				allowed = true;
		if (!allowed)
		{
			Sys_FindClose();
			Sys_Error("Unexpected PAK mounted in real-data proof: %s", path);
		}
		++pak_count;
		++allowlisted_count;
		char full_path[MAX_OSPATH];
		Com_sprintf(full_path, sizeof(full_path), "%s/base/%s", basedir, name);
		log_append("real-data-pak=%s size-bytes=%ld\n", name, file_size_bytes(full_path));
	}
	Sys_FindClose();
	if (pak_count != 2 || allowlisted_count != 2)
		Sys_Error("Real-data proof expected the two allowlisted original PAKs, found %d", pak_count);
	if (FS_LoadFile("SYNTHETIC-NOT-GAME.txt", NULL) != -1)
		Sys_Error("Synthetic fixture leaked into the real-data proof");
	log_append("input-pak-files=%d\nmounted-paks-allowlisted=PASS\noriginal-game-data=read-only-user-paks\n", pak_count);
}

static void verify_static_bindings(void)
{
	void* game = H2R_LoadLibrary("gamex86");
	void* effects = H2R_LoadLibrary("Client Effects");
	void* renderer = H2R_LoadLibrary("ref_soft");
	void* sound = H2R_LoadLibrary("snd_sdl3");
	if (game == NULL || H2R_GetProcAddress(game, "GetGameAPI") == NULL)
		Sys_Error("Static game API binding failed");
	if (effects == NULL || H2R_GetProcAddress(effects, "GetfxAPI") == NULL)
		Sys_Error("Static client-effects API binding failed");
	if (renderer == NULL || H2R_GetProcAddress(renderer, "GetRefAPI") == NULL)
		Sys_Error("Static software renderer API binding failed");
	if (sound == NULL || H2R_GetProcAddress(sound, "GetSoundAPI") == NULL)
		Sys_Error("Static SDL3 sound API binding failed");
	H2R_FreeLibrary(game);
	H2R_FreeLibrary(effects);
	H2R_FreeLibrary(renderer);
	H2R_FreeLibrary(sound);
	log_append(
		"static-game-binding=PASS\nstatic-client-effects-binding=PASS\n"
		"static-renderer-binding=PASS\nstatic-sound-binding=PASS\n");
}

static void verify_zone_allocator(void)
{
	byte* zeroed = Z_Malloc(64);
	for (int i = 0; i < 64; ++i)
		if (zeroed[i] != 0)
			Sys_Error("Z_Malloc returned non-zero memory at byte %d", i);
	Z_Free(zeroed);

	byte* tagged = Z_TagMalloc(96, H2DOS_ZONE_TAG);
	memset(tagged, 0xa5, 96);
	Z_FreeTags(H2DOS_ZONE_TAG);
	log_append("zone-malloc-zero=PASS\nzone-tag-cleanup=PASS\n");
}

static void verify_renderer(void)
{
	if (!H2DOS_SoftMalformedFixturesRejected())
		Sys_Error("Software renderer accepted a malformed BSP, BK, font, or FM fixture");
	log_append("renderer-malformed-bsp-rejected=10\nrenderer-malformed-book-rejected=1\nrenderer-malformed-font-rejected=1\nrenderer-malformed-fm-rejected=18\n");
	lightstyle_t lightstyles[MAX_LIGHTSTYLES];
	particle_t particles[2];
	dlight_t light;
	entity_t sprite_entity;
	entity_t flex_entities[3];
	entity_t shadow_entity;
	entity_t* entities[4];
	entity_t* alpha_entities[1];
	fmnodeinfo_t flex_nodes[3];
	LERPedReferences_t references;
	struct model_s* sprite_model;
	struct model_s* flex_model;
	struct image_s* shadow_skin;
	refdef_t frame;
	unsigned old_pose_checksum;
	unsigned midpoint_pose_checksum;
	unsigned new_pose_checksum;
	unsigned skeletal_pose_checksum;
	unsigned surface_old_checksum;
	unsigned surface_new_checksum;
	unsigned sky_checksums[6];
	paletteRGBA_t white = { .r = 255, .g = 255, .b = 255, .a = 255 };
	paletteRGBA_t title = { .r = 255, .g = 220, .b = 150, .a = 255 };

	cls.disable_screen = false;
	M_Menu_Main_f();
	SCR_UpdateScreen();
	cls.realtime += 400;
	cls.m_menuscale = 1.0f;
	cls.m_menualpha = 1.0f;
	SCR_UpdateScreen();
	re.BookDrawPic("book/back/b_conback8.bk", 0.75f, 0.85f);
	if (!H2DOS_SoftSavePPM("MENU.PPM"))
		Sys_Error("Could not save production menu framebuffer");
	cls.key_dest = key_console;
	Com_Printf("DOS SOFTWARE RENDERER - SYNTHETIC FIXTURE - NOT GAMEPLAY\n");
	SCR_UpdateScreen();
	if (!H2DOS_SoftSavePPM("CONSOLE.PPM"))
		Sys_Error("Could not save production console framebuffer");
	M_ForceMenuOff();
	cls.key_dest = key_game;

	re.BeginRegistration("synthetic");
	const vec3_t sky_axis = { 0.0f, 0.0f, 1.0f };
	re.SetSky("synthetic", 0.0f, sky_axis);
	Surface_t queried_surface;
	const vec3_t query_start = { 32.0f, 0.0f, 0.0f };
	const vec3_t query_end = { 112.0f, 0.0f, 0.0f };
	const vec3_t query_miss = { 48.0f, 0.0f, 0.0f };
	vec3_t invalid_query = { NAN, 0.0f, 0.0f };
	if (re.FindSurface(query_start, query_end, &queried_surface) != 1 || queried_surface.plane == NULL ||
		queried_surface.poly.verts == NULL || queried_surface.poly.numverts != 4 ||
		!(queried_surface.poly.flags & SURF_FLOWING) || fabsf(queried_surface.point[0] - 96.0f) > 0.01f ||
		fabsf(queried_surface.normal[0] + 1.0f) > 0.01f ||
		fabsf(queried_surface.poly.fraction - 0.8f) > 0.001f ||
		re.FindSurface(query_start, query_miss, &queried_surface) != -1 ||
		re.FindSurface(invalid_query, query_end, &queried_surface) != 0)
		Sys_Error("Production FindSurface did not return bounded BSP polygon semantics");
	static const char* malformed_flex[] = {
		"models/malformed-fm-truncated-header.fm", "models/malformed-fm-unterminated-ident.fm",
		"models/malformed-fm-negative-size.fm", "models/malformed-fm-duplicate-header.fm",
		"models/malformed-fm-missing-header.fm", "models/malformed-fm-wrong-version.fm",
		"models/malformed-fm-count-overflow.fm", "models/malformed-fm-framesize.fm",
		"models/malformed-fm-nonfinite-frame.fm", "models/malformed-fm-missing-terminator.fm",
		"models/malformed-fm-int-min.fm", "models/malformed-fm-xyz-index.fm",
		"models/malformed-fm-mesh-range.fm", "models/malformed-fm-skin-name.fm",
		"models/malformed-fm-skeleton-index.fm", "models/malformed-fm-reference-type.fm",
		"models/malformed-fm-reference-nonfinite.fm", "models/malformed-fm-compressed.fm"
	};
	for (size_t i = 0; i < sizeof(malformed_flex) / sizeof(malformed_flex[0]); ++i)
		if (re.RegisterModel(malformed_flex[i]) != NULL)
			Sys_Error("Production RegisterModel accepted malformed FM '%s'", malformed_flex[i]);
	sprite_model = re.RegisterModel("models/synthetic.sp2");
	flex_model = re.RegisterModel("models/synthetic_actor/tris.fm");
	qboolean shadow_skin_loaded = false;
	shadow_skin = re.RegisterSkin("models/synthetic-shadow.m8", &shadow_skin_loaded);
	if (sprite_model == NULL || flex_model == NULL || shadow_skin == NULL || !shadow_skin_loaded)
		Sys_Error("Synthetic sprite, FM, or shadow skin failed production registration");
	if (re.GetReferencedID(flex_model) != REF_CORVUS)
		Sys_Error("Synthetic FM did not expose REF_CORVUS through production refexport");
	memset(lightstyles, 0, sizeof(lightstyles));
	for (int i = 0; i < MAX_LIGHTSTYLES; ++i)
	{
		lightstyles[i].rgb[0] = 1.0f;
		lightstyles[i].rgb[1] = 1.0f;
		lightstyles[i].rgb[2] = 1.0f;
		lightstyles[i].white = 1.0f;
	}
	memset(&sprite_entity, 0, sizeof(sprite_entity));
	sprite_entity.model = &sprite_model;
	sprite_entity.origin[0] = 68.0f;
	sprite_entity.origin[1] = 55.0f;
	sprite_entity.origin[2] = 0.0f;
	sprite_entity.scale = 1.0f;
	sprite_entity.color = white;
	entities[0] = &sprite_entity;

	memset(flex_entities, 0, sizeof(flex_entities));
	for (int i = 0; i < 3; ++i)
	{
		flex_entities[i].model = &flex_model;
		flex_entities[i].origin[0] = 70.0f;
		flex_entities[i].origin[1] = -28.0f + i * 28.0f;
		flex_entities[i].origin[2] = 0.0f;
		flex_entities[i].scale = 1.0f;
		flex_entities[i].color = white;
		flex_entities[i].rootJoint = NULL_ROOT_JOINT;
		flex_entities[i].swapFrame = NO_SWAP_FRAME;
		flex_entities[i].oldSwapFrame = NO_SWAP_FRAME;
		entities[i + 1] = &flex_entities[i];
	}
	flex_entities[0].frame = 0;
	flex_entities[0].oldframe = 0;
	flex_entities[1].frame = 1;
	flex_entities[1].oldframe = 0;
	flex_entities[1].backlerp = 0.5f;
	flex_entities[2].frame = 1;
	flex_entities[2].oldframe = 0;
	flex_entities[2].backlerp = 0.5f;
	flex_entities[2].rootJoint = 0;
	memset(flex_nodes, 0, sizeof(flex_nodes));
	flex_nodes[0].flags = FMNI_USE_SKIN;
	flex_nodes[0].skin = 0;
	flex_nodes[1].flags = FMNI_USE_COLOR;
	flex_nodes[1].color = (paletteRGBA_t){ .r = 60, .g = 230, .b = 110, .a = 255 };
	flex_nodes[2].flags = FMNI_NO_DRAW;
	flex_entities[2].fmnodeinfo = flex_nodes;

	memset(&references, 0, sizeof(references));
	references.refType = REF_CORVUS;
	references.jointIDs = jointIDs[REF_CORVUS];
	references.lastUpdate = -(REF_MINCULLTIME * 2.0f);
	flex_entities[1].referenceInfo = &references;
	memset(skeletal_joints, 0, sizeof(skeletal_joints));
	memset(joint_nodes, 0, sizeof(joint_nodes));
	SK_CreateSkeleton(SKEL_CORVUS, 0);
	skeletal_joints[CORVUS_UPPERBACK].angles[ROLL] = 0.28f;
	skeletal_joints[CORVUS_HEAD].angles[ROLL] = -0.38f;

	memset(&shadow_entity, 0, sizeof(shadow_entity));
	shadow_entity.model = &flex_model;
	shadow_entity.origin[0] = 64.0f;
	shadow_entity.origin[2] = -47.0f;
	shadow_entity.angles[PITCH] = (float)M_PI * 0.5f;
	shadow_entity.frame = 1;
	shadow_entity.oldframe = 1;
	shadow_entity.scale = 0.75f;
	shadow_entity.color = (paletteRGBA_t){ .r = 255, .g = 255, .b = 255, .a = 145 };
	shadow_entity.flags = RF_FULLBRIGHT | RF_TRANSLUCENT | RF_ALPHA_TEXTURE;
	shadow_entity.rootJoint = NULL_ROOT_JOINT;
	shadow_entity.swapFrame = NO_SWAP_FRAME;
	shadow_entity.oldSwapFrame = NO_SWAP_FRAME;
	shadow_entity.skin = shadow_skin;
	alpha_entities[0] = &shadow_entity;

	memset(particles, 0, sizeof(particles));
	particles[0].origin[0] = 56.0f;
	particles[0].origin[1] = -18.0f;
	particles[0].origin[2] = 14.0f;
	particles[0].scale = 1.5f;
	particles[0].color = (paletteRGBA_t){ .r = 255, .g = 180, .b = 40, .a = 220 };
	particles[1].origin[0] = 60.0f;
	particles[1].origin[1] = 22.0f;
	particles[1].origin[2] = -10.0f;
	particles[1].scale = 2.0f;
	particles[1].color = (paletteRGBA_t){ .r = 80, .g = 180, .b = 255, .a = 180 };

	memset(&light, 0, sizeof(light));
	light.origin[0] = 72.0f;
	light.origin[2] = 18.0f;
	light.intensity = 18.0f;
	light.color = (paletteRGBA_t){ .r = 255, .g = 90, .b = 40, .a = 220 };

	memset(&frame, 0, sizeof(frame));
	frame.width = viddef.width;
	frame.height = viddef.height;
	frame.fov_x = 90.0f;
	frame.fov_y = 74.0f;
	frame.vieworg[0] = 32.0f;
	frame.lightstyles = lightstyles;
	frame.num_entities = 4;
	frame.entities = entities;
	frame.num_alpha_entities = 1;
	frame.alpha_entities = alpha_entities;
	frame.time = 0.25f;
	frame.num_dlights = 1;
	frame.dlights = &light;
	frame.num_particles = 1;
	frame.particles = particles;
	frame.anum_particles = 1;
	frame.aparticles = &particles[1];

	frame.num_entities = 0;
	frame.num_alpha_entities = 0;
	frame.num_dlights = 0;
	frame.num_particles = 0;
	frame.anum_particles = 0;
	Cvar_SetValue("quake_amount", 80.0f);
	frame.time = 0.0f;
	re.BeginFrame(0.0f);
	re.RenderFrame(&frame);
	re.EndFrame();
	surface_old_checksum = H2DOS_SoftFramebufferChecksum();
	if (!H2DOS_SoftSavePPM("SURFOLD.PPM"))
		Sys_Error("Could not save synthetic special-surface baseline");
	frame.time = 0.75f;
	re.BeginFrame(0.0f);
	re.RenderFrame(&frame);
	re.EndFrame();
	surface_new_checksum = H2DOS_SoftFramebufferChecksum();
	if (!H2DOS_SoftSavePPM("SURFNEW.PPM") || surface_old_checksum == surface_new_checksum)
		Sys_Error("Animated, flowing, turbulent, undulate, and quake surfaces did not change pixels");
	Cvar_SetValue("quake_amount", 0.0f);

	frame.x = viddef.width / 2 - 16;
	frame.y = viddef.height / 2 - 16;
	frame.width = 32;
	frame.height = 32;
	const vec3_t sky_views[6] = {
		{ 0.0f, 0.0f, 0.0f }, { 0.0f, 90.0f, 0.0f }, { 0.0f, 180.0f, 0.0f },
		{ 0.0f, 270.0f, 0.0f }, { -90.0f, 0.0f, 0.0f }, { 90.0f, 0.0f, 0.0f }
	};
	for (int direction = 0; direction < 6; ++direction)
	{
		VectorCopy(sky_views[direction], frame.viewangles);
		frame.time = 0.0f;
		re.BeginFrame(0.0f);
		re.RenderFrame(&frame);
		re.EndFrame();
		sky_checksums[direction] = H2DOS_SoftFramebufferChecksum();
		for (int previous = 0; previous < direction; ++previous)
			if (sky_checksums[previous] == sky_checksums[direction])
				Sys_Error("Six-sided sky direction %d did not select a distinct synthetic face", direction);
	}
	const vec3_t invalid_sky_axis = { NAN, NAN, NAN };
	re.SetSky("synthetic", NAN, invalid_sky_axis);
	re.SetSky("synthetic", 25.0f, sky_axis);
	frame.x = 0;
	frame.y = 0;
	frame.width = viddef.width;
	frame.height = viddef.height;
	VectorSet(frame.viewangles, -25.0f, 35.0f, 0.0f);
	frame.time = 1.0f;
	re.BeginFrame(0.0f);
	re.RenderFrame(&frame);
	re.EndFrame();
	if (!H2DOS_SoftSavePPM("SKY.PPM"))
		Sys_Error("Could not save six-sided projected sky proof");
	re.SetSky("synthetic", 0.0f, sky_axis);

	VectorClear(frame.viewangles);
	frame.time = 0.25f;
	frame.num_entities = 4;
	frame.num_alpha_entities = 1;
	frame.num_dlights = 1;
	frame.num_particles = 1;
	frame.anum_particles = 1;
	entity_t* final_first_entity = entities[0];
	entities[0] = &flex_entities[0];
	frame.num_entities = 1;
	frame.num_alpha_entities = 0;
	flex_entities[0].frame = 0;
	flex_entities[0].oldframe = 0;
	re.BeginFrame(0.0f);
	re.RenderFrame(&frame);
	re.EndFrame();
	old_pose_checksum = H2DOS_SoftFramebufferChecksum();
	if (!H2DOS_SoftSavePPM("FLEXOLD.PPM"))
		Sys_Error("Could not save synthetic old-frame FM proof");

	flex_entities[0].frame = 1;
	flex_entities[0].oldframe = 0;
	flex_entities[0].backlerp = 0.5f;
	flex_entities[0].referenceInfo = &references;
	re.BeginFrame(0.0f);
	re.RenderFrame(&frame);
	re.EndFrame();
	midpoint_pose_checksum = H2DOS_SoftFramebufferChecksum();
	if (!H2DOS_SoftSavePPM("FLEXMID.PPM") || midpoint_pose_checksum == old_pose_checksum)
		Sys_Error("Synthetic FM midpoint interpolation did not change framebuffer pixels");
	const Placement_t *left_hand = &references.references[CORVUS_LEFTHAND].placement;
	const Placement_t *head_reference = &references.references[CORVUS_HELL_HEAD].placement;
	if (fabsf(left_hand->origin[0]) > 0.001f || fabsf(left_hand->origin[1] + 12.0f) > 0.001f ||
		fabsf(left_hand->origin[2] - 9.0f) > 0.001f || fabsf(head_reference->origin[0]) > 0.001f ||
		fabsf(head_reference->origin[1] - 2.5f) > 0.001f || fabsf(head_reference->origin[2] - 34.5f) > 0.001f)
		Sys_Error("Synthetic FM reference midpoint did not match exact decoded coordinates");

	flex_entities[0].frame = 1;
	flex_entities[0].oldframe = 1;
	flex_entities[0].backlerp = 0.0f;
	flex_entities[0].referenceInfo = NULL;
	re.BeginFrame(0.0f);
	re.RenderFrame(&frame);
	re.EndFrame();
	new_pose_checksum = H2DOS_SoftFramebufferChecksum();
	if (!H2DOS_SoftSavePPM("FLEXNEW.PPM") || new_pose_checksum == midpoint_pose_checksum ||
		new_pose_checksum == old_pose_checksum)
		Sys_Error("Synthetic FM new frame did not produce a distinct framebuffer");

	flex_entities[0].oldframe = 0;
	flex_entities[0].backlerp = 0.5f;
	flex_entities[0].rootJoint = 0;
	re.BeginFrame(0.0f);
	re.RenderFrame(&frame);
	re.EndFrame();
	skeletal_pose_checksum = H2DOS_SoftFramebufferChecksum();
	if (!H2DOS_SoftSavePPM("FLEXSKEL.PPM") || skeletal_pose_checksum == midpoint_pose_checksum ||
		skeletal_pose_checksum == new_pose_checksum)
		Sys_Error("Synthetic FM skeletal rotation did not produce a distinct framebuffer");

	flex_entities[0].frame = 0;
	flex_entities[0].oldframe = 0;
	flex_entities[0].backlerp = 0.0f;
	flex_entities[0].rootJoint = NULL_ROOT_JOINT;
	entities[0] = final_first_entity;
	frame.num_entities = 4;
	frame.num_alpha_entities = 1;
	re.BeginFrame(0.0f);
	re.RenderFrame(&frame);
	re.DrawStretchPic(8, 8, 144, 28, "misc/synthetic-panel.m32", 0.9f, DSP_NONE);
	for (int i = 0; "SYNTHETIC RENDER FIXTURE"[i] != '\0'; ++i)
		re.DrawChar(14 + i * 8, 18, 1, "SYNTHETIC RENDER FIXTURE"[i], title, true);
	re.EndFrame();
	if (!H2DOS_SoftSavePPM("RENDER.PPM"))
		Sys_Error("Could not save production world framebuffer");

	// Sprite-geometry variants (dynamic/variable/line) through the production path.
	entity_t dynamic_sprite;
	entity_t variable_sprite;
	entity_t line_sprite;
	entity_t* variant_entities[1];
	static svertex_t variable_verts[5];
	unsigned variant_checksums[3];
	memset(&dynamic_sprite, 0, sizeof(dynamic_sprite));
	dynamic_sprite.model = &sprite_model;
	dynamic_sprite.spriteType = SPRITE_DYNAMIC;
	dynamic_sprite.origin[0] = 64.0f;
	dynamic_sprite.origin[1] = -22.0f;
	dynamic_sprite.origin[2] = 6.0f;
	dynamic_sprite.scale = 1.0f;
	dynamic_sprite.color = white;
	dynamic_sprite.verts[0] = (svertex_t){ -6.0f, -6.0f, 0.0f, 1.0f };
	dynamic_sprite.verts[1] = (svertex_t){ -6.0f, 6.0f, 0.0f, 0.0f };
	dynamic_sprite.verts[2] = (svertex_t){ 6.0f, 6.0f, 1.0f, 0.0f };
	dynamic_sprite.verts[3] = (svertex_t){ 6.0f, -6.0f, 1.0f, 1.0f };

	memset(&variable_sprite, 0, sizeof(variable_sprite));
	variable_sprite.model = &sprite_model;
	variable_sprite.spriteType = SPRITE_VARIABLE;
	variable_sprite.origin[0] = 64.0f;
	variable_sprite.origin[1] = 0.0f;
	variable_sprite.origin[2] = 6.0f;
	variable_sprite.scale = 1.0f;
	variable_sprite.color = white;
	variable_sprite.verts_p = variable_verts;
	variable_sprite.numVerts = 5;
	for (int i = 0; i < 5; ++i)
	{
		const float angle = (float)i * 1.25663706f;
		variable_verts[i] = (svertex_t){ 7.0f * cosf(angle), 7.0f * sinf(angle),
			0.5f + 0.5f * cosf(angle), 0.5f + 0.5f * sinf(angle) };
	}

	memset(&line_sprite, 0, sizeof(line_sprite));
	line_sprite.model = &sprite_model;
	line_sprite.spriteType = SPRITE_LINE;
	line_sprite.color = white;
	line_sprite.scale = 2.0f;
	line_sprite.scale2 = 5.0f;
	line_sprite.tile = 2.0f;
	line_sprite.tileoffset = 0.0f;
	VectorSet(line_sprite.startpos, 64.0f, 14.0f, 2.0f);
	VectorSet(line_sprite.endpos, 64.0f, 26.0f, 14.0f);

	variant_entities[0] = &dynamic_sprite;
	entity_t* variant_members[3] = { &dynamic_sprite, &variable_sprite, &line_sprite };
	frame.num_entities = 1;
	frame.num_alpha_entities = 0;
	frame.num_dlights = 0;
	frame.num_particles = 0;
	frame.anum_particles = 0;
	frame.entities = variant_entities;
	for (int variant = 0; variant < 3; ++variant)
	{
		variant_entities[0] = variant_members[variant];
		re.BeginFrame(0.0f);
		re.RenderFrame(&frame);
		re.EndFrame();
		variant_checksums[variant] = H2DOS_SoftFramebufferChecksum();
	}
	if (variant_checksums[0] == variant_checksums[1] || variant_checksums[0] == variant_checksums[2] ||
		variant_checksums[1] == variant_checksums[2])
		Sys_Error("Sprite geometry variants did not produce distinct framebuffers");

	// Additive line sprite through the alpha entity pass.
	line_sprite.flags = RF_TRANS_ADD;
	frame.num_entities = 0;
	frame.num_alpha_entities = 1;
	frame.alpha_entities = variant_entities;
	variant_entities[0] = &line_sprite;
	re.BeginFrame(0.0f);
	re.RenderFrame(&frame);
	re.EndFrame();
	const unsigned additive_line_checksum = H2DOS_SoftFramebufferChecksum();
	if (additive_line_checksum == variant_checksums[2])
		Sys_Error("Additive-transparency line sprite did not change blending outcome");
	line_sprite.flags = 0;
	frame.alpha_entities = alpha_entities;
	frame.entities = entities;

	// Effect-specific particles: distinct atlas types, flags, and additive pass.
	particle_t effect_particles[7];
	unsigned particle_checksums[4];
	memset(effect_particles, 0, sizeof(effect_particles));
	const int atlas_types[4] = { 28, 33, 22, 35 }; // FIRE0, SPARK_G, STAR, FIREBALL
	for (int i = 0; i < 7; ++i)
	{
		effect_particles[i].origin[0] = 64.0f;
		effect_particles[i].origin[1] = -24.0f + (float)(i * 8);
		effect_particles[i].origin[2] = 12.0f;
		effect_particles[i].scale = 3.0f;
		effect_particles[i].color = white;
	}
	frame.num_particles = 1;
	frame.anum_particles = 0;
	frame.particles = &effect_particles[6];
	for (int type_index = 0; type_index < 4; ++type_index)
	{
		effect_particles[6].type = atlas_types[type_index];
		re.BeginFrame(0.0f);
		re.RenderFrame(&frame);
		re.EndFrame();
		particle_checksums[type_index] = H2DOS_SoftFramebufferChecksum();
		for (int previous = 0; previous < type_index; ++previous)
			if (particle_checksums[previous] == particle_checksums[type_index])
				Sys_Error("Particle atlas types %d and %d did not produce distinct framebuffers",
					atlas_types[previous], atlas_types[type_index]);
	}
	effect_particles[6].type = 22 | PFL_SOFT_MASK;
	re.BeginFrame(0.0f);
	re.RenderFrame(&frame);
	re.EndFrame();
	if (H2DOS_SoftFramebufferChecksum() == particle_checksums[2])
		Sys_Error("PFL_SOFT_MASK point particle did not diverge from textured particle");

	effect_particles[0].type = 28;   // PART_32x32_FIRE0
	effect_particles[1].type = 33 | PFL_LM_COLOR; // PART_16x16_SPARK_G + lightmap tint
	effect_particles[2].type = 16;   // PART_8x8_BUBBLE
	effect_particles[3].type = 38;   // PART_8x8_RED_X
	effect_particles[4].type = 50;   // PART_32x32_BUBBLE
	effect_particles[5].type = 0;    // PART_4x4_WHITE
	effect_particles[6].type = 24;   // PART_16x16_LIGHTNING (additive pass below)
	frame.num_particles = 6;
	frame.particles = effect_particles;
	frame.anum_particles = 1;
	frame.aparticles = &effect_particles[6];
	re.BeginFrame(0.0f);
	re.RenderFrame(&frame);
	re.EndFrame();
	if (!H2DOS_SoftSavePPM("EFFECTS.PPM"))
		Sys_Error("Could not save production effects framebuffer");
	frame.num_particles = 1;
	frame.particles = particles;
	frame.anum_particles = 1;
	frame.aparticles = &particles[1];

	frame.num_entities = 0;
	frame.num_alpha_entities = 0;
	re.BeginFrame(0.0f);
	re.RenderFrame(&frame);
	re.EndFrame();
	for (int reference = 0; reference < NUM_REFERENCES_CORVUS; ++reference)
		for (int axis = 0; axis < 3; ++axis)
			if (!isfinite(references.references[reference].placement.origin[axis]) ||
				!isfinite(references.references[reference].placement.direction[axis]) ||
				!isfinite(references.references[reference].placement.up[axis]))
				Sys_Error("Synthetic FM reference interpolation produced a non-finite placement");
	re.EndRegistration();

	const h2dos_soft_stats_t* stats = H2DOS_SoftGetStats();
	if (stats->frames_presented < 15 || stats->world_faces < 1 || stats->triangles < 2 ||
		stats->depth_writes < 1 || stats->sprite_entities < 5 || stats->dynamic_sprites < 1 ||
		stats->variable_sprites < 1 || stats->line_sprites < 2 || stats->particles < 2 ||
		stats->texture_particles < 4 ||
		stats->dynamic_lights < 1 || stats->flex_models != 1 || stats->flex_entities < 6 ||
		stats->flex_triangles < 12 || stats->flex_shadow_entities < 1 || stats->reference_updates < 1 ||
		stats->sky_pixels < 1 || stats->sky_face_mask != 0x3f || stats->animated_faces < 1 ||
		stats->flowing_faces < 1 || stats->turbulent_faces < 1 || stats->undulate_faces < 1 ||
		stats->quake_faces < 1 || stats->surface_queries != 1 || stats->m8_images < 17 || stats->m32_images < 4)
		Sys_Error("Software renderer production counters did not cross proof boundary");
	log_append(
		"renderer-api=PASS\nrenderer-console=PASS\nrenderer-world=PASS\n"
		"renderer-sprite=PASS\nrenderer-particles=PASS\nrenderer-dlight=PASS\n"
		"renderer-sky-six-sided=PASS\nrenderer-sky-invalid-params-sanitized=PASS\n"
		"renderer-sprite-variants=dynamic-variable-line-PASS\n"
		"renderer-sprite-additive-line=PASS\n"
		"renderer-effect-particles=typed-atlas-softmask-additive-PASS\n"
		"renderer-effects-screenshot=EFFECTS.PPM\n"
		"renderer-special-surfaces=animation-flow-warp-undulate-quake-PASS\nrenderer-find-surface=PASS\n"
		"renderer-surface-old-screenshot=SURFOLD.PPM\nrenderer-surface-new-screenshot=SURFNEW.PPM\n"
		"renderer-sky-screenshot=SKY.PPM\nrenderer-flex-accepted=1\nrenderer-flex-animation=PASS\nrenderer-flex-skeleton=PASS\n"
		"renderer-flex-references=REF_CORVUS-PASS\nrenderer-flex-node-policy=PASS\n"
		"renderer-flex-lighting-dlight=PASS\nrenderer-flex-shadow=PASS\n"
		"renderer-flex-fixture=synthetic-non-game\n"
		"renderer-flex-old-screenshot=FLEXOLD.PPM\nrenderer-flex-midpoint-screenshot=FLEXMID.PPM\n"
		"renderer-flex-new-screenshot=FLEXNEW.PPM\nrenderer-flex-skeletal-screenshot=FLEXSKEL.PPM\n"
		"renderer-flex-reference-left-hand=0.000,-12.000,9.000\n"
		"renderer-flex-reference-head=0.000,2.500,34.500\n"
		"renderer-flex-old-checksum=%08x\nrenderer-flex-midpoint-checksum=%08x\n"
		"renderer-flex-new-checksum=%08x\nrenderer-flex-skeletal-checksum=%08x\n"
		"renderer-surface-old-checksum=%08x\nrenderer-surface-new-checksum=%08x\n"
		"renderer-sky-checksums=%08x,%08x,%08x,%08x,%08x,%08x\n"
		"renderer-framebuffer=%dx%d\nrenderer-frames-presented=%u\n"
		"renderer-ui-draws=%u\nrenderer-world-faces=%u\nrenderer-triangles=%u\n"
		"renderer-depth-writes=%u\nrenderer-sprite-entities=%u\nrenderer-particles-drawn=%u\n"
		"renderer-sprite-dynamic=%u\nrenderer-sprite-variable=%u\nrenderer-sprite-line=%u\n"
		"renderer-particles-textured=%u\n"
		"renderer-sprite-variant-checksums=%08x,%08x,%08x\n"
		"renderer-particle-type-checksums=%08x,%08x,%08x,%08x\n"
		"renderer-dynamic-lights=%u\nrenderer-flex-models=%u\nrenderer-flex-entities=%u\n"
		"renderer-flex-triangles=%u\nrenderer-flex-shadow-entities=%u\nrenderer-reference-updates=%u\n"
		"renderer-sky-pixels=%u\nrenderer-sky-face-mask=%02x\nrenderer-animated-faces=%u\n"
		"renderer-flowing-faces=%u\nrenderer-turbulent-faces=%u\nrenderer-undulate-faces=%u\n"
		"renderer-quake-faces=%u\nrenderer-surface-queries=%u\n"
		"renderer-m8-images=%u\nrenderer-m32-images=%u\n"
		"renderer-missing-assets=%u\nrenderer-menu-screenshot=MENU.PPM\n"
		"renderer-console-screenshot=CONSOLE.PPM\nrenderer-world-screenshot=RENDER.PPM\n",
		old_pose_checksum, midpoint_pose_checksum, new_pose_checksum, skeletal_pose_checksum,
		surface_old_checksum, surface_new_checksum, sky_checksums[0], sky_checksums[1],
		sky_checksums[2], sky_checksums[3], sky_checksums[4], sky_checksums[5],
		viddef.width, viddef.height, stats->frames_presented, stats->ui_draws,
		stats->world_faces, stats->triangles, stats->depth_writes,
		stats->sprite_entities, stats->particles, stats->dynamic_sprites, stats->variable_sprites,
		stats->line_sprites, stats->texture_particles,
		variant_checksums[0], variant_checksums[1], variant_checksums[2],
		particle_checksums[0], particle_checksums[1], particle_checksums[2], particle_checksums[3],
		stats->dynamic_lights,
		stats->flex_models, stats->flex_entities, stats->flex_triangles,
		stats->flex_shadow_entities, stats->reference_updates,
		stats->sky_pixels, stats->sky_face_mask, stats->animated_faces, stats->flowing_faces,
		stats->turbulent_faces, stats->undulate_faces, stats->quake_faces, stats->surface_queries,
		stats->m8_images, stats->m32_images, stats->missing_assets);
}

static void verify_realdata_renderer_begin(void)
{
	cls.disable_screen = false;
	M_Menu_Main_f();
	SCR_UpdateScreen();
	cls.realtime += 400;
	cls.m_menuscale = 1.0f;
	cls.m_menualpha = 1.0f;
	SCR_UpdateScreen();
	re.BookDrawPic("book/back/b_conback8.bk", 0.75f, 0.85f);
	if (!H2DOS_SoftSavePPM("REALMENU.PPM"))
		Sys_Error("Could not save original-data menu framebuffer");
	cls.key_dest = key_console;
	Com_Printf("DOS SOFTWARE RENDERER - ORIGINAL PAKS - READ-ONLY - NOT GAMEPLAY\n");
	SCR_UpdateScreen();
	if (!H2DOS_SoftSavePPM("REALCON.PPM"))
		Sys_Error("Could not save original-data console framebuffer");
	M_ForceMenuOff();
	cls.key_dest = key_game;
	log_append(
		"realdata-menu-screenshot=REALMENU.PPM\n"
		"realdata-console-screenshot=REALCON.PPM\n"
		"realdata-menu=PASS\nrealdata-console=PASS\n");
}

static void log_realdata_stats(void)
{
	const h2dos_soft_stats_t* stats = H2DOS_SoftGetStats();
	if (stats->frames_presented < 8 || stats->world_faces < 10 || stats->m32_images < 5 ||
		stats->m8_images < 3 || stats->flex_models < 1 || stats->flex_entities < 1 ||
		stats->triangles < 1 || stats->depth_writes < 1)
		Sys_Error("Original-data production render counters did not cross proof boundary");
	log_append(
		"realdata-frames-presented=%u\nrealdata-world-faces=%u\n"
		"realdata-triangles=%u\nrealdata-depth-writes=%u\n"
		"realdata-sprite-entities=%u\nrealdata-particles-drawn=%u\n"
		"realdata-textured-particles=%u\n"
		"realdata-flex-models=%u\nrealdata-flex-entities=%u\n"
		"realdata-flex-triangles=%u\nrealdata-reference-updates=%u\n"
		"realdata-sky-pixels=%u\nrealdata-sky-face-mask=%02x\n"
		"realdata-m8-images=%u\nrealdata-m32-images=%u\n"
		"realdata-missing-assets=%u\n"
		"realdata-active-client-screenshot=CLIENT.PPM\n",
		stats->frames_presented, stats->world_faces, stats->triangles, stats->depth_writes,
		stats->sprite_entities, stats->particles, stats->texture_particles, stats->flex_models,
		stats->flex_entities, stats->flex_triangles, stats->reference_updates, stats->sky_pixels,
		stats->sky_face_mask, stats->m8_images, stats->m32_images, stats->missing_assets);
}

static void log_bench_summary(void)
{
	const h2dos_soft_stats_t* stats = H2DOS_SoftGetStats();
	const unsigned long long frames = stats->bench_frames > 0 ? stats->bench_frames : 1;
	const unsigned walked_world = stats->bench_walked_world;
	const unsigned inside_world = stats->bench_pixels_world;
	const unsigned walked_flex = stats->bench_walked_flex + stats->bench_walked_sprite;
	const unsigned inside_flex = stats->bench_pixels_flex + stats->bench_pixels_sprite;
	log_append(
		"bench-frames=%u\nbench-us-total=%llu\n"
		"bench-us-clear=%llu\nbench-us-sky=%llu\nbench-us-world=%llu\n"
		"bench-us-entities=%llu\nbench-us-particles=%llu\nbench-us-lights=%llu\n"
		"bench-us-blend=%llu\nbench-us-present=%llu\nbench-us-frame=%llu\n"
		"bench-avg-us-frame=%llu\nbench-avg-us-clear=%llu\nbench-avg-us-sky=%llu\n"
		"bench-avg-us-world=%llu\nbench-avg-us-entities=%llu\n"
		"bench-avg-us-particles=%llu\nbench-avg-us-lights=%llu\n"
		"bench-avg-us-blend=%llu\nbench-avg-us-present=%llu\n"
		"bench-walked-world-px=%u\nbench-inside-world-px=%u\n"
		"bench-world-faces=%u\nbench-world-spans=%u\nbench-world-blocks=%u\n"
		"bench-world-lightbuild-px=%u\n"
		"bench-avg-us-world-setup=%llu\nbench-avg-us-world-spans=%llu\n"
		"bench-walked-flex-px=%u\nbench-inside-flex-px=%u\n"
		"bench-world-walk-waste-pct=%u\nbench-flex-walk-waste-pct=%u\n"
		"bench-flex-vertex-lerps=%llu\nbench-avg-us-flex-prologue=%llu\n"
		"bench-avg-us-ent-flex=%llu\nbench-avg-us-ent-sprite=%llu\n"
		"bench-model-flex=%u\nbench-model-sprite=%u\n"
		"bench-avg-us-flex-swaprefs=%llu\nbench-avg-us-flex-tris=%llu\n"
		"bench-flex-tris=%u\nbench-flex-tris-fast=%u\nbench-flex-tris-clip=%u\n"
		"bench-avg-us-flex-setup=%llu\nbench-avg-us-flex-dispatch=%llu\n"
		"bench-flex-tris-bbox=%u\nbench-flex-tris-behind=%u\n"
		"bench-avg-us-ent-brush=%llu\nbench-brush-faces=%u\nbench-brush-culled=%u\nbench-flex-spans=%u\nbench-flex-blocks=%u\n",		stats->bench_frames, (unsigned long long)uclock(),
		stats->bench_us_clear, stats->bench_us_sky, stats->bench_us_world,
		stats->bench_us_entities, stats->bench_us_particles, stats->bench_us_lights,
		stats->bench_us_blend, stats->bench_us_present, stats->bench_us_frame,
		stats->bench_us_frame / frames, stats->bench_us_clear / frames,
		stats->bench_us_sky / frames, stats->bench_us_world / frames,
		stats->bench_us_entities / frames, stats->bench_us_particles / frames,
		stats->bench_us_lights / frames, stats->bench_us_blend / frames,
		stats->bench_us_present / frames,
		walked_world, inside_world, stats->bench_world_faces,
		stats->bench_world_spans, stats->bench_world_blocks, stats->bench_world_lightbuild_px,
		stats->bench_us_world_setup / frames, stats->bench_us_world_spans / frames,
		walked_flex, inside_flex,
		walked_world > 0 ? (walked_world - inside_world) * 100u / walked_world : 0,
		walked_flex > 0 ? (walked_flex - inside_flex) * 100u / walked_flex : 0,
		stats->bench_flex_vertex_lerps, stats->bench_us_flex_prologue / frames,
		stats->bench_us_ent_flex / frames, stats->bench_us_ent_sprite / frames,
		stats->bench_model_flex, stats->bench_model_sprite,
		stats->bench_us_flex_swaprefs / frames, stats->bench_us_flex_tris / frames,
		stats->bench_flex_tris, stats->bench_flex_tris_fast, stats->bench_flex_tris_clip,
		stats->bench_us_flex_setup / frames, stats->bench_us_flex_dispatch / frames,
		stats->bench_flex_tris_bbox, stats->bench_flex_tris_behind,
		stats->bench_us_ent_brush / frames, stats->bench_brush_faces, stats->bench_brush_culled,
		stats->bench_flex_spans, stats->bench_flex_blocks);
	log_append("bench-sky-cache-hits=%u\nbench-sky-cache-misses=%u\n",
		stats->sky_cache_hits, stats->sky_cache_misses);
	log_append("bench-world-zaccepted=%u\nbench-sky-pixels=%u\nbench-sky-holes=%u\nbench-flex-tris-affine=%u\n",
		stats->bench_world_zaccepted, stats->sky_pixels, stats->bench_sky_holes, stats->bench_flex_tris_affine);
	log_append("bench-zcollide-eq=%llu\nbench-zcollide-near=%llu\nbench-flex-tris-wind=%u\n",
		(unsigned long long)stats->bench_zcollide_eq, (unsigned long long)stats->bench_zcollide_near,
		stats->bench_flex_tris_wind);
	log_append("bench-magenta-px=%u\nbench-magenta-max=%u\nbench-magenta-saved=%u\n",
		stats->bench_magenta_px, stats->bench_magenta_max, stats->bench_magenta_saved);
	log_append("bench-mag-world=%u\nbench-mag-world-alpha=%u\nbench-mag-flex=%u\nbench-mag-putpixel=%u\nbench-mag-frame=%u\nbench-mag-surface=%llu\n",
		stats->bench_mag_world, stats->bench_mag_world_alpha, stats->bench_mag_flex,
		stats->bench_mag_putpixel, stats->bench_mag_frame, (unsigned long long)stats->bench_mag_surface);
}

static void begin_audio_proof(const qboolean realdata)
{
	h2dos_sound_stats_t stats;
	const char* fixture_name = realdata ? "Monsters/chicken/peck1.wav" : "dos/synthetic.wav";
	if (!realdata)
	{
		if (H2DOS_WavFileIsValid("sound/dos/malformed-truncated.wav") ||
			H2DOS_WavFileIsValid("sound/dos/malformed-zero-width.wav") ||
			H2DOS_WavFileIsValid("sound/dos/malformed-data-length.wav"))
			Sys_Error("Production WAV parser accepted a malformed fixture");
		log_append("sound-malformed-wav-rejected=3\n");
	}
	if (se.title == NULL || strcmp(se.title, "SDL3 Software Mixer") != 0)
		Sys_Error("DOS production sound API did not initialize the software mixer");
	if (!SNDSDL3_GetDOSStats(&stats) || !stats.backend_initialized || !stats.soundblaster_driver)
		Sys_Error("DOS production sound backend is not using SDL3 Sound Blaster");
	if (se.RegisterSound(fixture_name) == NULL)
		Sys_Error("Real-data WAV failed engine sound registration");
	se.StartLocalSound(fixture_name);
	log_append(
		"sound-api=SDL3 Software Mixer\n"
		"sound-driver=soundblaster\n"
		"sound-fixture=sound/%s\n"
		"sound-fixture-kind=%s\n",
		fixture_name, realdata ? "original-game-pak-read-only" : "synthetic-non-game");
}

static void finish_audio_proof(void)
{
	h2dos_sound_stats_t stats;
	if (!SNDSDL3_GetDOSStats(&stats))
	{
		log_append("sound-stats-read=FAIL\n");
		Sys_Error("Could not read DOS production sound counters");
	}
	log_append(
		"sound-callback-calls=%lu\nsound-submitted-bytes=%lu\n"
		"sound-submitted-nonzero-samples=%lu\nsound-mixed-nonzero-frames=%lu\n"
		"sound-cached-sounds=%lu\nsound-cached-source-samples=%lu\n",
		stats.callback_calls, stats.submitted_bytes, stats.submitted_nonzero_samples,
		stats.mixed_nonzero_frames, stats.cached_sounds, stats.cached_source_samples);
	if (!stats.backend_initialized || !stats.soundblaster_driver || stats.cached_sounds < 1 ||
		stats.cached_source_samples < 1 || stats.callback_calls < 1 || stats.submitted_bytes < 1 ||
		stats.mixed_nonzero_frames < 1 || stats.submitted_nonzero_samples < 1)
		Sys_Error("Production mixer-to-Sound-Blaster counters did not cross proof boundary");
	log_append(
		"sound-wav-load=PASS\nsound-software-mix=PASS\nsound-sdl-submit=PASS\n"
		"sound-proof-counters=PASS\n");
}

static void verify_loopback(void)
{
	static const byte payload[] = "H2DOS production loopback proof";
	byte received[sizeof(payload) + 8];
	sizebuf_t message;
	netadr_t destination;
	netadr_t source;

	memset(&message, 0, sizeof(message));
	message.data = received;
	message.maxsize = sizeof(received);
	if (!NET_StringToAdr("localhost", &destination) || !NET_IsLocalAddress(&destination))
		Sys_Error("DOS loopback address conversion failed");

	const h2dos_net_stats_t before_gamespy = NET_GetDOSStats();
	NET_SendPacket(NS_GAMESPY, sizeof(payload), payload, &destination);
	const h2dos_net_stats_t after_gamespy = NET_GetDOSStats();
	if (memcmp(&before_gamespy, &after_gamespy, sizeof(before_gamespy)) != 0 ||
		NET_GetPacket(NS_GAMESPY, &source, &message))
		Sys_Error("DOS loopback backend did not reject NS_GAMESPY");

	NET_SendPacket(NS_CLIENT, sizeof(payload), payload, &destination);
	if (!NET_GetPacket(NS_SERVER, &source, &message))
		Sys_Error("DOS loopback packet was not received");
	if (!NET_CompareAdr(&destination, &source) || message.cursize != sizeof(payload) ||
		memcmp(message.data, payload, sizeof(payload)) != 0)
		Sys_Error("DOS loopback packet was corrupted");

	log_append("loopback-gamespy-rejected=PASS\nloopback-address=loopback\nloopback-send=1\nloopback-receive=1\nloopback-payload=PASS\n");
}

int main(const int argc, char** argv)
{
	int frame_limit = argument_value(argc, argv, "-dosframes", "H2DOS_FRAMES", 180);
	const char* run_id = argument_string(argc, argv, "-dosrun", "H2DOS_RUN_ID", "UNSPECIFIED");
	const qboolean renderer_proof = argument_flag(argc, argv, "-dosrenderer", "H2DOS_RENDERER");
	const qboolean audio_proof = argument_flag(argc, argv, "-dosaudio", "H2DOS_AUDIO");
	const qboolean realdata = argument_flag(argc, argv, "-dosrealdata", "H2DOS_REALDATA");
	const qboolean bench_requested = argument_flag(argc, argv, "-dosbench", "H2DOS_BENCH");
	const qboolean integrated_proof = renderer_proof && audio_proof;

	if (argument_flag(argc, argv, "-dosplay", "H2DOS_PLAY"))
	{
		// Interactive play mode: production engine loop without proof pacing,
		// asserts, logging, or frame caps. Exits through Sys_Quit.
		Qcommon_Init(argc, argv);
		long long play_previous = Sys_Microseconds();
		for (;;)
		{
			const long long now = Sys_Microseconds();
			long long elapsed = now - play_previous;
			if (elapsed < H2DOS_MIN_FRAME_USEC)
			{
				Sys_Nanosleep((int)((H2DOS_MIN_FRAME_USEC - elapsed) * 1000));
				continue;
			}
			if (elapsed < 0 || elapsed > INT_MAX)
				elapsed = H2DOS_MIN_FRAME_USEC;
			curtime = (int)(now / 1000ll);
			Qcommon_Frame((int)elapsed);
			play_previous = now;
		}
	}
	qboolean audio_started = false;
	if (frame_limit < 1)
		frame_limit = 1;
	if (frame_limit > 10000)
		frame_limit = 10000;

	const long long started = Sys_Microseconds();
	long long previous = started;
	long long minimum_delta = LLONG_MAX;
	long long maximum_delta = 0;
	FILE* log;
	int frame;

	remove(H2DOS_MARKER);
	log = fopen(H2DOS_LOG, "w");
	if (log == NULL)
		return 2;
	fprintf(log,
		"artifact=SILVER.EXE\nrun-id=%s\npath=production-Qcommon\n"
		"fixture=%s\n"
		"qcommon-init-entry=PASS\nframes-requested=%d\n",
		run_id, realdata ? "original-paks-read-only-non-gameplay" : "synthetic-non-game", frame_limit);
	fclose(log);

	Qcommon_Init(argc, argv);
	log_append("qcommon-init-completion=PASS\n");
	if (GetfxAPI == NULL || fxe.client_string == NULL)
		Sys_Error("Production server initialization did not bind the client-effects API");
	log_append("client-effects-api-init=PASS\n");
	if (realdata)
		verify_realdata_mounts(argument_string(argc, argv, "-basedir", NULL, "."));
	else
	{
		verify_fixture();
		verify_mounted_paks(argument_string(argc, argv, "-basedir", NULL, "."));
	}
	verify_static_bindings();
	verify_zone_allocator();
	verify_loopback();
	if (renderer_proof)
	{
		if (realdata)
			verify_realdata_renderer_begin();
		else
			verify_renderer();
	}
	if (audio_proof && !integrated_proof)
	{
		begin_audio_proof(realdata);
		audio_started = true;
	}

	const int server_frame_start = sv.framenum;
	previous = Sys_Microseconds() - 100000;
	long long bench_time_us = 0;
	qboolean bench_seeded = false;
	for (frame = 0; frame < frame_limit; ++frame)
	{
		long long now = Sys_Microseconds();
		long long elapsed = now - previous;
		if (bench_requested)
		{
			elapsed = H2DOS_MIN_FRAME_USEC; // decouple sim pacing from render cost
			bench_time_us += elapsed;      // deterministic sim clock (curtime below)
		}
		else if (elapsed < H2DOS_MIN_FRAME_USEC)
		{
			Sys_Nanosleep((int)((H2DOS_MIN_FRAME_USEC - elapsed) * 1000));
			now = Sys_Microseconds();
			elapsed = now - previous;
		}
		if (elapsed < 0 || elapsed > INT_MAX)
			Sys_Error("Invalid frame delta %lld", elapsed);
		if (elapsed < minimum_delta)
			minimum_delta = elapsed;
		if (elapsed > maximum_delta)
			maximum_delta = elapsed;
		curtime = bench_requested ? (int)(bench_time_us / 1000ll) : (int)(now / 1000ll);
		Qcommon_Frame((int)elapsed);
		previous = now;
		if (bench_requested && realdata && !bench_seeded && cls.state == ca_active && cl.refresh_prepped)
		{
			// The sim and client share one libc PRNG; frames rendered before the
			// map is active (loading screens) vary in count with wall time, so the
			// rand state at activation would otherwise fork between runs.
			srand(H2DOS_BENCH_RNG_SEED);
			bench_seeded = true;
			log_append("bench-rng-seeded=%u\n", (unsigned)H2DOS_BENCH_RNG_SEED);
		}
		if (realdata)
			log_append("realdata-frame=%d cls-state=%d sv-state=%d initialized=%d\n",
				frame, cls.state, sv.state, svs.initialized);
		if (realdata && bench_requested && bench_seeded && getenv("H2DOS_SWEEP") != NULL)
		{
			// Deterministic camera orbit: each frame yaws by a fixed step so span
			// and naive runs sweep identical angle sequences. Anything that differs
			// per angle (coverage, objects) lands in the dumped frames.
			cl.viewangles[0] = -10.0f;
			cl.viewangles[1] = (float)(frame % 48) * (360.0f / 48.0f);
		}
		if (realdata && getenv("H2DOS_DUMPALL") != NULL)
		{
			// Name by renderer 3D frame (bf) so dumps align with bf= tagged
			// diagnostics; skip iterations where no 3D render happened.
			static unsigned last_rendered_frame = 0xFFFFFFFFu;
			char name[16];
			const h2dos_soft_stats_t *swst = H2DOS_SoftGetStats();
			const unsigned bf = swst != NULL ? swst->bench_frames : 0;
			if (bf != last_rendered_frame)
			{
				last_rendered_frame = bf;
				snprintf(name, sizeof(name), "F%02d.PPM", (int)(bf > 0 ? bf - 1 : 0));
				if (!H2DOS_SoftSavePPM(name))
					Sys_Error("Could not save indexed client framebuffer %s", name);
			}
		}
		if (integrated_proof && !audio_started && svs.initialized && svs.clients != NULL &&
			svs.clients[0].state == cs_spawned && cls.state == ca_active && cl.refresh_prepped)
		{
			begin_audio_proof(realdata);
			audio_started = true;
		}
	}

	if (svs.initialized)
	{
		if (ge == NULL || playerExport.PlayerSeqData == NULL || playerExport.p_itemlist == NULL)
			Sys_Error("Active synthetic server did not bind game/player APIs");
		if (sv.framenum <= server_frame_start)
			Sys_Error("Active synthetic server did not advance a game frame");
		log_append("game-api-init=PASS\nplayer-api-init=PASS\nserver-map=%s\nserver-state=%d\n", sv.name, sv.state);
	}
	if (audio_proof)
	{
		if (!audio_started)
			Sys_Error("Sound proof could not start after client/server initialization");
		finish_audio_proof();
	}
	if (realdata)
		log_realdata_stats();
	if (bench_requested && realdata)
		log_bench_summary();
	if (integrated_proof)
	{
		const h2dos_net_stats_t net_stats = NET_GetDOSStats();
		if (!svs.initialized || svs.clients == NULL || svs.clients[0].state != cs_spawned ||
			cls.state != ca_active || !cl.refresh_prepped)
			Sys_Error("Synthetic local client/server handshake did not reach the active state");
		if (net_stats.packets_sent <= 1 || net_stats.packets_received <= 1 || net_stats.packets_dropped != 0)
			Sys_Error("Synthetic local client/server handshake did not cross DOS loopback");
		if (!H2DOS_SoftSavePPM("CLIENT.PPM"))
			Sys_Error("Could not save active synthetic client framebuffer");
		log_append(
			"local-client-server-handshake=PASS\nclient-state=active\n"
			"server-client-state=spawned\nclient-refresh-prepped=PASS\n"
			"loopback-total-sent=%u\nloopback-total-received=%u\nloopback-total-dropped=%u\n"
			"renderer-active-client-screenshot=CLIENT.PPM\n",
			net_stats.packets_sent, net_stats.packets_received, net_stats.packets_dropped);
	}
	log_append(
		"qcommon-frame-calls=%d\nsv-frame-calls=%u\ncl-frame-calls=%u\n"
		"server-frame-start=%d\nserver-frame-end=%d\n"
		"frame-delta-min-us=%lld\nframe-delta-max-us=%lld\n"
		"curtime-unit=milliseconds\nruntime-ms=%lld\n"
		"boundary=%s\n"
		"gameplay=UNVERIFIED\nstatus=PASS\n",
		frame, h2dos_sv_frame_calls, h2dos_cl_frame_calls,
		server_frame_start, sv.framenum,
		minimum_delta, maximum_delta, (Sys_Microseconds() - started) / 1000ll,
		realdata ? "original-paks-read-only-production-path-smoke-non-gameplay" :
		integrated_proof ? "synthetic-local-client-server-renderer-mixer-soundblaster" :
		(renderer_proof ? "synthetic-client-software-renderer" :
		(audio_proof ? "synthetic-client-software-mixer-soundblaster" : "synthetic-dedicated-server-client-null-renderer")));

	log = fopen(H2DOS_MARKER, "w");
	if (log == NULL)
		return 4;
	fprintf(log, "SILVER DOS QCOMMON LIFECYCLE PASS\nrun-id=%s\nframes=%d\n", run_id, frame);
	fclose(log);

	if (svs.initialized)
		SV_Shutdown("Synthetic DOS proof complete\n", false);
	CL_Shutdown();
	SDL_Quit();
	return 0;
}
