#include "qcommon.h"
#include "client.h"

#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int curtime;

static char find_base[MAX_OSPATH];
static char find_path[MAX_OSPATH];
static intptr_t find_handle;

long long Sys_Microseconds(void)
{
	return (long long)(SDL_GetTicksNS() / 1000ULL);
}

void Sys_Nanosleep(const int nanosec)
{
	if (nanosec > 0)
		SDL_DelayNS((Uint64)nanosec);
}

void Sys_Mkdir(const char *path)
{
	_mkdir(path);
}

qboolean Sys_IsDir(const char *path)
{
	struct stat info;
	return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

qboolean Sys_IsFile(const char *path)
{
	struct stat info;
	return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

qboolean Sys_GetWorkingDir(char *buffer, const size_t length)
{
	if (getcwd(buffer, length))
		return true;
	if (length)
		buffer[0] = '\0';
	return false;
}

qboolean Sys_GetOSUserDir(char *buffer, const size_t length)
{
	if (length)
		buffer[0] = '\0';
	return false;
}

static qboolean attributes_match(const uint found, const uint must_have, const uint cannot_have)
{
	const uint attributes[] = {_A_RDONLY, _A_HIDDEN, _A_SYSTEM, _A_SUBDIR, _A_ARCH};
	const uint flags[] = {SFF_RDONLY, SFF_HIDDEN, SFF_SYSTEM, SFF_SUBDIR, SFF_ARCH};
	int i;

	for (i = 0; i < (int)(sizeof(attributes) / sizeof(attributes[0])); ++i) {
		if ((found & attributes[i]) && (cannot_have & flags[i]))
			return false;
		if ((must_have & flags[i]) && !(found & attributes[i]))
			return false;
	}
	return true;
}

char *Sys_FindNext(const uint must_have, const uint cannot_have)
{
	struct _finddata_t info;
	if (find_handle == 0 || find_handle == -1)
		return NULL;

	while (_findnext(find_handle, &info) != -1) {
		if (!attributes_match(info.attrib, must_have, cannot_have))
			continue;
		if ((info.attrib & _A_SUBDIR) && (!strcmp(info.name, ".") || !strcmp(info.name, "..")))
			continue;
		Com_sprintf(find_path, sizeof(find_path), "%s/%s", find_base, info.name);
		return find_path;
	}
	return NULL;
}

char *Sys_FindFirst(const char *path, const uint must_have, const uint cannot_have)
{
	struct _finddata_t info;
	if (find_handle != 0)
		Sys_Error("Sys_FindFirst called without Sys_FindClose");

	COM_FilePath(path, find_base);
	if (!find_base[0])
		strcpy(find_base, ".");
	find_handle = _findfirst(path, &info);
	if (find_handle == -1)
		return NULL;
	if (!attributes_match(info.attrib, must_have, cannot_have) ||
		((info.attrib & _A_SUBDIR) && (!strcmp(info.name, ".") || !strcmp(info.name, ".."))))
		return Sys_FindNext(must_have, cannot_have);
	Com_sprintf(find_path, sizeof(find_path), "%s/%s", find_base, info.name);
	return find_path;
}

void Sys_FindClose(void)
{
	if (find_handle != 0 && find_handle != -1)
		_findclose(find_handle);
	find_handle = 0;
}

H2R_NORETURN void Sys_Error(const char *error, ...)
{
	va_list args;
	char message[2048];
	fputs("System error: ", stderr);
	va_start(args, error);
	vsnprintf(message, sizeof(message), error, args);
	va_end(args);
	fputs(message, stderr);
	fputc('\n', stderr);
	FILE* proof_log = fopen("SILVER.LOG", "a");
	if (proof_log != NULL)
	{
		fprintf(proof_log, "sys-error=%s\n", message);
		fclose(proof_log);
	}
	SDL_Quit();
	exit(1);
}

H2R_NORETURN void Sys_Quit(void)
{
	CL_Shutdown();
	SDL_Quit();
	exit(0);
}

void Sys_LoadGameDll(const char *dll_name, HINSTANCE *handle, DWORD *checksum)
{
	if (handle == NULL)
		Sys_Error("Sys_LoadGameDll called without a handle destination");
	if (checksum)
		*checksum = 0;
	*handle = (HINSTANCE)H2R_LoadLibrary(dll_name);
	if (!*handle)
		Sys_Error("Static DOS module '%s' is unavailable", dll_name != NULL ? dll_name : "<null>");
}

void Sys_UnloadGameDll(const char *name, HINSTANCE *handle)
{
	(void)name;
	if (handle && *handle) {
		H2R_FreeLibrary((void *)*handle);
		*handle = NULL;
	}
}

void Sys_Init(void)
{
	Set_Com_Printf(Com_Printf);
}

char *Sys_ConsoleInput(void)
{
	return NULL;
}

void Sys_ConsoleOutput(const char *string)
{
	fputs(string, stdout);
	fflush(stdout);
}
