#include "ref_soft_local.h"
#include "Vector.h"

#include <math.h>
#include <string.h>

#define SW_TITLE "DOS SDL3 software"

refimport_t sw_ri;
qboolean sw_synthetic_fixture;
int sw_registration_sequence;
sw_image_t *sw_sky_images[6];
float sw_sky_rotate;
vec3_t sw_sky_axis;
unsigned sw_sky_generation;

static qboolean sw_initialized;

static void screenshot_command(void)
{
	if (SW_SavePPM("SWSHOT.PPM"))
		sw_ri.Con_Printf(PRINT_ALL, "Wrote SWSHOT.PPM from DOS software framebuffer\n");
	else
		sw_ri.Con_Printf(PRINT_ALL, "Could not write SWSHOT.PPM\n");
}

static qboolean detect_synthetic_fixture(void)
{
	static const char identity[] = "Heretic2Remastered DOS synthetic engine-path fixture.";
	void *data = NULL;
	const int length = sw_ri.FS_LoadFile("SYNTHETIC-NOT-GAME.txt", &data);
	const qboolean found = data != NULL && length >= (int)sizeof(identity) - 1 &&
		memcmp(data, identity, sizeof(identity) - 1) == 0;
	if (data != NULL)
		sw_ri.FS_FreeFile(data);
	return found;
}

static qboolean SW_Init(void)
{
	int width = 0;
	int height = 0;
	cvar_t *mode = sw_ri.Cvar_Get("vid_mode", "0", CVAR_ARCHIVE);
	sw_synthetic_fixture = detect_synthetic_fixture();
	memset(&sw_stats, 0, sizeof(sw_stats));
	memset(sw_sky_images, 0, sizeof(sw_sky_images));
	sw_sky_rotate = 0.0f;
	VectorSet(sw_sky_axis, 0.0f, 0.0f, 1.0f);
	sw_sky_generation = 1;
	sw_registration_sequence = 1;
	sw_ri.Con_Printf(PRINT_ALL, "Refresh: %s\n", SW_TITLE);
	sw_ri.Con_Printf(PRINT_ALL, "ref_soft: synthetic fallback policy=%s\n",
		sw_synthetic_fixture ? "enabled-for-labeled-fixture" : "disabled");
	if (!sw_ri.Vid_GetModeInfo(&width, &height, (int)mode->value) ||
		!sw_ri.GLimp_InitGraphics(width, height))
	{
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: could not initialize video mode %d\n", (int)mode->value);
		return false;
	}
	SW_ImageInit();
	SW_ModelInit();
	SW_ParticleTexturesInit();
	sw_ri.Cmd_AddCommand("screenshot", screenshot_command);
	sw_initialized = true;
	return true;
}

static void SW_Shutdown(void)
{
	if (!sw_initialized)
		return;
	sw_ri.Cmd_RemoveCommand("screenshot");
	SW_ParticleTexturesShutdown();
	SW_ModelShutdown();
	SW_SkyCacheShutdown();
	SW_ImageShutdown();
	SW_FrameBufferShutdown();
	sw_initialized = false;
}

static void SW_SetSky(const char *name, const float rotate, const vec3_t axis)
{
	static const char *suffixes[6] = { "rt", "bk", "lf", "ft", "up", "dn" };
	++sw_sky_generation;
	memset(sw_sky_images, 0, sizeof(sw_sky_images));
	sw_sky_rotate = 0.0f;
	VectorSet(sw_sky_axis, 0.0f, 0.0f, 1.0f);
	if (name == NULL || name[0] == '\0' || strlen(name) > MAX_QPATH - 22)
	{
		sw_ri.Con_Printf(PRINT_ALL, "ref_soft: rejected invalid sky name\n");
		return;
	}
	if (isfinite(rotate))
		sw_sky_rotate = rotate;
	if (axis != NULL && isfinite(axis[0]) && isfinite(axis[1]) && isfinite(axis[2]))
		VectorCopy(axis, sw_sky_axis);
	for (int face = 0; face < 6; ++face)
	{
		char path[MAX_QPATH];
		Com_sprintf(path, sizeof(path), "pics/skies/%s%s.m8", name, suffixes[face]);
		sw_sky_images[face] = SW_FindImage(path, SW_IMAGE_WALL);
	}
}

static void SW_BeginFrame(const float camera_separation)
{
	(void)camera_separation;
	SW_Clear(SW_RGB565(2, 2, 8));
}

static void SW_EndFrame(void)
{
	SW_Present();
}

static int SW_PrepareForWindow(void)
{
	return 0;
}

static qboolean SW_InitContext(void *window)
{
	return SW_FrameBufferInit(window);
}

static void SW_ShutdownContext(void)
{
	SW_FrameBufferShutdown();
}

refexport_t GetRefAPI(const refimport_t import)
{
	refexport_t exports;
	memset(&exports, 0, sizeof(exports));
	sw_ri = import;
	exports.api_version = REF_API_VERSION;
	exports.title = SW_TITLE;
	exports.Init = SW_Init;
	exports.Shutdown = SW_Shutdown;
	exports.BeginRegistration = SW_BeginRegistration;
	exports.RegisterModel = SW_RegisterModel;
	exports.RegisterSkin = SW_RegisterSkin;
	exports.RegisterPic = SW_RegisterPic;
	exports.SetSky = SW_SetSky;
	exports.EndRegistration = SW_EndRegistration;
	exports.GetReferencedID = SW_GetReferencedID;
	exports.RenderFrame = SW_RenderFrame;
	exports.DrawGetPicSize = SW_DrawGetPicSize;
	exports.DrawPic = SW_DrawPic;
	exports.DrawStretchPic = SW_DrawStretchPic;
	exports.DrawChar = SW_DrawChar;
	exports.DrawTileClear = SW_DrawTileClear;
	exports.DrawFill = SW_DrawFill;
	exports.DrawFadeScreen = SW_DrawFadeScreen;
	exports.DrawBigFont = SW_DrawBigFont;
	exports.BF_Strlen = SW_BigFontLength;
	exports.BookDrawPic = SW_BookDrawPic;
	exports.DrawInitCinematic = SW_DrawInitCinematic;
	exports.DrawCloseCinematic = SW_DrawCloseCinematic;
	exports.DrawCinematic = SW_DrawCinematic;
	exports.Draw_Name = SW_DrawName;
	exports.BeginFrame = SW_BeginFrame;
	exports.EndFrame = SW_EndFrame;
	exports.FindSurface = SW_FindSurface;
	exports.PrepareForWindow = SW_PrepareForWindow;
	exports.InitContext = SW_InitContext;
	exports.ShutdownContext = SW_ShutdownContext;
	return exports;
}

const h2dos_soft_stats_t *H2DOS_SoftGetStats(void)
{
	return &sw_stats;
}

qboolean H2DOS_SoftSavePPM(const char *path)
{
	return SW_SavePPM(path);
}

qboolean H2DOS_SoftMalformedFixturesRejected(void)
{
	static const char *bsp_files[] = {
		"maps/malformed-area.bsp", "maps/malformed-texinfo-chain.bsp",
		"maps/malformed-area-reference.bsp", "maps/malformed-child.bsp",
		"maps/malformed-node-cycle.bsp", "maps/malformed-leaf-range.bsp",
		"maps/malformed-face-range.bsp", "maps/malformed-submodel-range.bsp",
		"maps/malformed-lightmap.bsp", "maps/malformed-visibility.bsp"
	};
	for (size_t i = 0; i < sizeof(bsp_files) / sizeof(bsp_files[0]); ++i)
	{
		if (SW_TestBSPFile(bsp_files[i]))
		{
			sw_ri.Con_Printf(PRINT_ALL, "ref_soft: malformed proof unexpectedly accepted '%s'\n", bsp_files[i]);
			return false;
		}
	}
	return !SW_TestBookFile("book/malformed-frame.bk") && !SW_TestFontFile("pics/misc/malformed.fnt");
}
