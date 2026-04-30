// dlopen_unix.c -- dynamic library helper functions shared by macOS dylibs.

#include "qcommon.h"

#include <dlfcn.h>
#include <libgen.h>
#include <mach-o/dyld.h>

static void H2R_NormalizeLibraryName(const char* name, char* out, size_t out_size)
{
	strcpy_s(out, out_size, name);

	char* ext = strrchr(out, '.');
	if (ext != NULL && Q_stricmp(ext, ".dll") == 0)
		strcpy_s(ext, out_size - (size_t)(ext - out), ".dylib");
}

static void* H2R_TryLoadLibraryPath(const char* path)
{
	void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (handle != NULL)
		return handle;

	char normalized[MAX_OSPATH];
	H2R_NormalizeLibraryName(path, normalized, sizeof(normalized));
	if (strcmp(normalized, path) != 0)
	{
		handle = dlopen(normalized, RTLD_NOW | RTLD_LOCAL);
		if (handle != NULL)
			return handle;
	}

	const char* filename = strrchr(normalized, '/');
	filename = (filename != NULL) ? filename + 1 : normalized;
	if (strchr(filename, '.') == NULL)
	{
		char with_ext[MAX_OSPATH];
		Com_sprintf(with_ext, sizeof(with_ext), "%s.dylib", normalized);
		handle = dlopen(with_ext, RTLD_NOW | RTLD_LOCAL);
		if (handle != NULL)
			return handle;
	}

	return NULL;
}

void* H2R_LoadLibrary(const char* name)
{
	if (name == NULL || name[0] == '\0')
		return NULL;

	void* handle = H2R_TryLoadLibraryPath(name);
	if (handle != NULL)
		return handle;

	char local[MAX_OSPATH];
	Com_sprintf(local, sizeof(local), "./%s", name);
	handle = H2R_TryLoadLibraryPath(local);
	if (handle != NULL)
		return handle;

	char exe_path[MAX_OSPATH];
	uint32_t size = sizeof(exe_path);
	if (_NSGetExecutablePath(exe_path, &size) == 0)
	{
		char exe_dir[MAX_OSPATH];
		strcpy_s(exe_dir, sizeof(exe_dir), exe_path);
		char* dir = dirname(exe_dir);
		char candidate[MAX_OSPATH];
		Com_sprintf(candidate, sizeof(candidate), "%s/%s", dir, name);
		handle = H2R_TryLoadLibraryPath(candidate);
		if (handle != NULL)
			return handle;
	}

	return NULL;
}

void* H2R_GetProcAddress(void* library, const char* symbol)
{
	return (library != NULL && symbol != NULL) ? dlsym(library, symbol) : NULL;
}

int H2R_FreeLibrary(void* library)
{
	return library == NULL || dlclose(library) == 0;
}
