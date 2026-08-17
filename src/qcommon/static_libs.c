// static_libs.c -- built-in module loader for H2_STATIC_GAME builds.

#include "qcommon.h"
#include "client.h"
#include "server.h"

#include <stddef.h>

extern game_export_t* GetGameAPI(const game_import_t* import);
extern client_fx_export_t H2R_ClientEffects_GetfxAPI(client_fx_import_t import);
extern refexport_t GetRefAPI(refimport_t import);
extern snd_export_t GetSoundAPI(snd_import_t import);

static const int h2_static_game_handle = 1;
static const int h2_static_clfx_handle = 2;
static const int h2_static_renderer_handle = 3;
static const int h2_static_snd_sdl3_handle = 4;

static const char* H2R_BaseName(const char* path)
{
	const char* base = path;

	for (const char* p = path; p != NULL && *p != '\0'; p++)
	{
		if (*p == '/' || *p == '\\')
			base = p + 1;
	}

	return base;
}

static qboolean H2R_StaticNameMatches(const char* name, const char* module_name)
{
	if (name == NULL || module_name == NULL)
		return false;

	const char* base = H2R_BaseName(name);
	const size_t module_len = strlen(module_name);

	if (Q_strncasecmp(base, module_name, (int)module_len) != 0)
		return false;

	const char next = base[module_len];
	return (next == '\0' || next == '.' || next == '/' || next == '\\');
}

void* H2R_LoadLibrary(const char* name)
{
	if (H2R_StaticNameMatches(name, "gamex86"))
		return (void*)&h2_static_game_handle;

	if (H2R_StaticNameMatches(name, "Client Effects"))
		return (void*)&h2_static_clfx_handle;

#ifdef H2_DOS
	if (H2R_StaticNameMatches(name, "ref_soft"))
		return (void*)&h2_static_renderer_handle;
#else
	if (H2R_StaticNameMatches(name, "ref_gl3"))
		return (void*)&h2_static_renderer_handle;
#endif

	if (H2R_StaticNameMatches(name, "snd_sdl3"))
		return (void*)&h2_static_snd_sdl3_handle;

	return NULL;
}

void* H2R_GetProcAddress(void* library, const char* symbol)
{
	if (library == NULL || symbol == NULL)
		return NULL;

	if (library == (void*)&h2_static_game_handle)
		return (Q_stricmp(symbol, "GetGameAPI") == 0 ? (void*)GetGameAPI : NULL);

	if (library == (void*)&h2_static_clfx_handle)
		return (Q_stricmp(symbol, "GetfxAPI") == 0 ? (void*)H2R_ClientEffects_GetfxAPI : NULL);

	if (library == (void*)&h2_static_renderer_handle)
		return (Q_stricmp(symbol, "GetRefAPI") == 0 ? (void*)GetRefAPI : NULL);

	if (library == (void*)&h2_static_snd_sdl3_handle)
		return (Q_stricmp(symbol, "GetSoundAPI") == 0 ? (void*)GetSoundAPI : NULL);

	return NULL;
}

int H2R_FreeLibrary(void* library)
{
	return library == NULL ||
		library == (void*)&h2_static_game_handle ||
		library == (void*)&h2_static_clfx_handle ||
		library == (void*)&h2_static_renderer_handle ||
		library == (void*)&h2_static_snd_sdl3_handle;
}
