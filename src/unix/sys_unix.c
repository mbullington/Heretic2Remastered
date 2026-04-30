// sys_unix.c -- macOS/POSIX system services.

#include "qcommon.h"

#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <errno.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <sys/select.h>
#include <sys/time.h>

int curtime;

static char findbase[MAX_OSPATH];
static char findpath[MAX_OSPATH];
static intptr_t findhandle;

static char console_text[256];

long long Sys_Microseconds(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (long long)tv.tv_sec * 1000000ll + (long long)tv.tv_usec;
}

void Sys_Nanosleep(const int nanosec)
{
	if (nanosec <= 0)
		return;

	struct timespec ts;
	ts.tv_sec = nanosec / 1000000000;
	ts.tv_nsec = nanosec % 1000000000;

	while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
	{
	}
}

void Sys_Mkdir(const char* path)
{
	_mkdir(path);
}

qboolean Sys_IsDir(const char* path)
{
	struct stat st;
	return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

qboolean Sys_IsFile(const char* path)
{
	struct stat st;
	return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static qboolean CompareAttributes(const uint found, const uint musthave, const uint canthave)
{
	if ((found & _A_RDONLY) && (canthave & SFF_RDONLY))
		return false;
	if ((found & _A_HIDDEN) && (canthave & SFF_HIDDEN))
		return false;
	if ((found & _A_SYSTEM) && (canthave & SFF_SYSTEM))
		return false;
	if ((found & _A_SUBDIR) && (canthave & SFF_SUBDIR))
		return false;
	if ((found & _A_ARCH) && (canthave & SFF_ARCH))
		return false;

	if ((musthave & SFF_RDONLY) && !(found & _A_RDONLY))
		return false;
	if ((musthave & SFF_HIDDEN) && !(found & _A_HIDDEN))
		return false;
	if ((musthave & SFF_SYSTEM) && !(found & _A_SYSTEM))
		return false;
	if ((musthave & SFF_SUBDIR) && !(found & _A_SUBDIR))
		return false;
	if ((musthave & SFF_ARCH) && !(found & _A_ARCH))
		return false;

	return true;
}

char* Sys_FindFirst(const char* path, const uint musthave, const uint canthave)
{
	struct _finddata_t findinfo;

	if (findhandle != 0)
		Sys_Error("Sys_FindFirst called without close");

	COM_FilePath((char*)path, findbase);
	findhandle = _findfirst(path, &findinfo);

	if (findhandle == -1)
		return NULL;

	if (!CompareAttributes(findinfo.attrib, musthave, canthave))
		return Sys_FindNext(musthave, canthave);

	if ((findinfo.attrib & _A_SUBDIR) && (strcmp(findinfo.name, ".") == 0 || strcmp(findinfo.name, "..") == 0))
		return Sys_FindNext(musthave, canthave);

	Com_sprintf(findpath, sizeof(findpath), "%s/%s", findbase, findinfo.name);
	return findpath;
}

char* Sys_FindNext(const uint musthave, const uint canthave)
{
	struct _finddata_t findinfo;

	if (findhandle == -1)
		return NULL;

	while (_findnext(findhandle, &findinfo) != -1)
	{
		if (!CompareAttributes(findinfo.attrib, musthave, canthave))
			continue;

		if ((findinfo.attrib & _A_SUBDIR) && (strcmp(findinfo.name, ".") == 0 || strcmp(findinfo.name, "..") == 0))
			continue;

		Com_sprintf(findpath, sizeof(findpath), "%s/%s", findbase, findinfo.name);
		return findpath;
	}

	return NULL;
}

void Sys_FindClose(void)
{
	if (findhandle != -1 && findhandle != 0)
		_findclose(findhandle);

	findhandle = 0;
}

#ifdef __APPLE__
static qboolean Sys_GetAppBundleResourcesDir(char* buffer, const size_t len)
{
	char exe_path[MAX_OSPATH];
	uint32_t size = sizeof(exe_path);
	if (_NSGetExecutablePath(exe_path, &size) != 0)
		return false;

	char resolved[MAX_OSPATH];
	if (realpath(exe_path, resolved) == NULL)
		strcpy_s(resolved, sizeof(resolved), exe_path);

	char* marker = strstr(resolved, ".app/Contents/MacOS/");
	if (marker == NULL)
		return false;

	marker += strlen(".app");
	*marker = '\0';

	char resources[MAX_OSPATH];
	Com_sprintf(resources, sizeof(resources), "%s/Contents/Resources", resolved);

	char data_file[MAX_OSPATH];
	Com_sprintf(data_file, sizeof(data_file), "%s/base/Htic2-0.pak", resources);
	if (!Sys_IsFile(data_file))
	{
		Com_sprintf(data_file, sizeof(data_file), "%s/base/base.pak", resources);
		if (!Sys_IsFile(data_file))
			return false;
	}

	strcpy_s(buffer, len, resources);
	return true;
}
#endif

qboolean Sys_GetWorkingDir(char* buffer, const size_t len)
{
#ifdef __APPLE__
	if (Sys_GetAppBundleResourcesDir(buffer, len))
		return true;
#endif

	if (getcwd(buffer, len) != NULL)
		return true;

	buffer[0] = '\0';
	return false;
}

qboolean Sys_GetOSUserDir(char* buffer, const size_t len)
{
	const char* home = getenv("HOME");
	if (home == NULL || home[0] == '\0')
	{
		buffer[0] = '\0';
		return false;
	}

	Com_sprintf(buffer, len, "%s/Library/Application Support", home);
	return true;
}

H2R_NORETURN void Sys_Error(const char* error, ...)
{
	va_list argptr;
	char text[1024];

	va_start(argptr, error);
	vsprintf_s(text, sizeof(text), error, argptr);
	va_end(argptr);

	CL_Shutdown();
	fprintf(stderr, "Error: %s\n", text);
	exit(1);
}

H2R_NORETURN void Sys_Quit(void)
{
	CL_Shutdown();
	exit(0);
}

void Sys_LoadGameDll(const char* dll_name, HINSTANCE* hinst, DWORD* checksum)
{
	char name[MAX_OSPATH];

	*hinst = NULL;
	if (checksum != NULL)
		*checksum = 0;

	char* path = NULL;
	while (true)
	{
		path = FS_NextPath(path);
		if (path == NULL)
			break;

		Com_sprintf(name, sizeof(name), "%s/%s", path, dll_name);
		*hinst = H2R_LoadLibrary(name);
		if (*hinst != NULL)
		{
			Com_DDPrintf(2, "dlopen (%s)\n", name);
			return;
		}
	}

	*hinst = H2R_LoadLibrary(dll_name);
	if (*hinst != NULL)
	{
		Com_DDPrintf(2, "dlopen (%s)\n", dll_name);
		return;
	}

	Sys_Error("Failed to load %s (%s)", dll_name, dlerror() != NULL ? dlerror() : "unknown dlopen error");
}

void Sys_UnloadGameDll(const char* name, HINSTANCE* hinst)
{
	if (hinst == NULL || *hinst == NULL)
		return;

	if (!H2R_FreeLibrary(*hinst))
		Sys_Error("Failed to unload %s", name);

	*hinst = NULL;
}

void Sys_Init(void)
{
	Set_Com_Printf(Com_Printf);
}

char* Sys_ConsoleInput(void)
{
	if (dedicated == NULL || !(int)dedicated->value)
		return NULL;

	fd_set fdset;
	struct timeval timeout = { 0, 0 };
	FD_ZERO(&fdset);
	FD_SET(STDIN_FILENO, &fdset);

	if (select(STDIN_FILENO + 1, &fdset, NULL, NULL, &timeout) <= 0)
		return NULL;

	if (fgets(console_text, sizeof(console_text), stdin) == NULL)
		return NULL;

	const size_t len = strlen(console_text);
	if (len > 0 && console_text[len - 1] == '\n')
		console_text[len - 1] = '\0';

	return console_text;
}

void Sys_ConsoleOutput(const char* string)
{
	if (dedicated != NULL && (int)dedicated->value)
		fputs(string, stdout);
}

int main(int argc, char** argv)
{
	Qcommon_Init(argc, argv);

	long long oldtime = Sys_Microseconds();
	while (true)
	{
		const long long spintime = Sys_Microseconds();
		while (Sys_Microseconds() - spintime < 5)
			SDL_CPUPauseInstruction();

		const long long newtime = Sys_Microseconds();
		curtime = (int)(newtime / 1000ll);
		Qcommon_Frame((int)(newtime - oldtime));
		oldtime = newtime;
	}
}
