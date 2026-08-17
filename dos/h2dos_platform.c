#include "h2dos_platform.h"
#include "qcommon.h"

#include <stdarg.h>

#define H2DOS_MAX_ARGS 50

static int h2dos_argc;
static char **h2dos_argv;
static FILE *h2dos_log;
static char find_base[MAX_OSPATH];
static char find_path[MAX_OSPATH];
static intptr_t find_handle;

cvar_t *dedicated;

void H2DOS_InitArgs(int argc, char **argv)
{
	h2dos_argc = argc > H2DOS_MAX_ARGS ? H2DOS_MAX_ARGS : argc;
	h2dos_argv = argv;
}

void H2DOS_SetLog(FILE *file)
{
	h2dos_log = file;
}

int COM_Argc(void)
{
	return h2dos_argc;
}

char *COM_Argv(const int arg)
{
	static char empty[] = "";

	if (arg < 0 || arg >= h2dos_argc)
		return empty;

	return h2dos_argv[arg];
}

void COM_ClearArgv(const int arg)
{
	if (arg >= 0 && arg < h2dos_argc)
		h2dos_argv[arg] = "";
}

int COM_CheckParm(const char *parm)
{
	int i;

	for (i = 1; i < h2dos_argc; ++i)
		if (strcmp(parm, h2dos_argv[i]) == 0)
			return i;

	return 0;
}

void Com_Printf(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	if (h2dos_log) {
		va_start(args, fmt);
		vfprintf(h2dos_log, fmt, args);
		va_end(args);
		fflush(h2dos_log);
	}
}

void Com_ColourPrintf(const PalIdx_t colour, const char *fmt, ...)
{
	va_list args;

	(void)colour;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	if (h2dos_log) {
		va_start(args, fmt);
		vfprintf(h2dos_log, fmt, args);
		va_end(args);
		fflush(h2dos_log);
	}
}

void Com_DPrintf(const char *fmt, ...)
{
	(void)fmt;
}

void Com_DDPrintf(const int level, const char *fmt, ...)
{
	(void)level;
	(void)fmt;
}

void Com_Error(const int code, const char *fmt, ...)
{
	va_list args;

	fprintf(stderr, "Engine error %d: ", code);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}

void Sys_Error(const char *fmt, ...)
{
	va_list args;

	fputs("System error: ", stderr);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}

void *Z_Malloc(const int size)
{
	void *memory;

	if (size <= 0)
		Com_Error(ERR_FATAL, "Z_Malloc: invalid size %d", size);

	memory = calloc(1, (size_t)size);
	if (!memory)
		Com_Error(ERR_FATAL, "Z_Malloc: failed on %d bytes", size);

	return memory;
}

void Z_Free(void *ptr)
{
	free(ptr);
}

void *Z_TagMalloc(const int size, const int tag)
{
	(void)tag;
	return Z_Malloc(size);
}

void Z_FreeTags(const int tag)
{
	(void)tag;
}

char *CopyString(const char *in)
{
	const size_t length = strlen(in) + 1;
	char *copy = Z_Malloc((int)length);
	memcpy(copy, in, length);
	return copy;
}

void SZ_Init(sizebuf_t *buf, byte *data, const int length)
{
	memset(buf, 0, sizeof(*buf));
	buf->data = data;
	buf->maxsize = length;
}

void SZ_Clear(sizebuf_t *buf)
{
	buf->cursize = 0;
	buf->overflowed = false;
}

void *SZ_GetSpace(sizebuf_t *buf, const int length)
{
	void *space;

	if (length < 0 || buf->cursize + length > buf->maxsize)
		Com_Error(ERR_FATAL, "SZ_GetSpace: %d-byte overflow", length);

	space = buf->data + buf->cursize;
	buf->cursize += length;
	return space;
}

void SZ_Write(sizebuf_t *buf, const void *data, const int length)
{
	memcpy(SZ_GetSpace(buf, length), data, (size_t)length);
}

void SZ_Print(sizebuf_t *buf, const char *data)
{
	const int length = (int)strlen(data) + 1;

	if (buf->cursize == 0 || buf->data[buf->cursize - 1])
		memcpy(SZ_GetSpace(buf, length), data, (size_t)length);
	else
		memcpy((byte *)SZ_GetSpace(buf, length - 1) - 1, data, (size_t)length);
}

int Com_ServerState(void)
{
	return 0;
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

	buffer[0] = '\0';
	return false;
}

qboolean Sys_GetOSUserDir(char *buffer, const size_t length)
{
	if (length > 0)
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

void Cmd_ForwardToServer(void)
{
	Com_Printf("Unknown command: %s\n", Cmd_Argv(0));
}
