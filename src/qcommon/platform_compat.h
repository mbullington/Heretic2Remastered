// platform_compat.h -- small Windows CRT compatibility layer for non-Windows builds.
//
// The original Heretic II sources were modernized around MSVC's "secure" CRT
// helpers and Win32-style typedefs.  Keep that source readable by providing the
// tiny subset we need on POSIX/macOS.

#pragma once

#ifndef _WIN32

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t DWORD;
typedef long LONG;
typedef unsigned long DWORD_PTR;
#ifndef __OBJC__
typedef int BOOL;
#endif
typedef void* HINSTANCE;
typedef void* HMODULE;
typedef void* HWND;
typedef void* HANDLE;
typedef char* LPSTR;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef WINAPI
#define WINAPI
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef _In_
#define _In_
#define _In_opt_
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif

#define MB_OK 0
#define MB_ICONWARNING 0
static inline int MessageBox(void* hwnd, const char* text, const char* caption, unsigned int type)
{
	(void)hwnd;
	(void)type;
	fprintf(stderr, "%s: %s\n", caption != NULL ? caption : "Message", text != NULL ? text : "");
	return 0;
}

static inline void OutputDebugString(const char* text)
{
	if (text != NULL)
		fputs(text, stderr);
}

#ifndef __cplusplus
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#endif

static inline size_t h2_min_size(const size_t a, const size_t b)
{
	return a < b ? a : b;
}

#ifndef _inline
#define _inline static inline
#endif

#define _strdup strdup
#define _stricmp strcasecmp
#define stricmp strcasecmp
#define _strnicmp strncasecmp
#define strnicmp strncasecmp
#define _strcmpi strcasecmp
#define _getcwd getcwd

static inline int h2_fopen_s(FILE** file, const char* filename, const char* mode)
{
	if (file == NULL)
		return EINVAL;

	*file = fopen(filename, mode);
	return (*file != NULL) ? 0 : (errno != 0 ? errno : EIO);
}

static inline int h2_strcpy_s(char* dst, size_t dst_size, const char* src)
{
	if (dst == NULL || dst_size == 0)
		return EINVAL;

	if (src == NULL)
	{
		dst[0] = '\0';
		return EINVAL;
	}

	const size_t len = strlen(src);
	const size_t copy = h2_min_size(len, dst_size - 1);
	memcpy(dst, src, copy);
	dst[copy] = '\0';

	return (copy == len) ? 0 : ERANGE;
}

static inline int h2_strncpy_s(char* dst, size_t dst_size, const char* src, size_t count)
{
	if (dst == NULL || dst_size == 0)
		return EINVAL;

	if (src == NULL)
	{
		dst[0] = '\0';
		return EINVAL;
	}

	if (count == _TRUNCATE)
		count = dst_size - 1;

	const size_t available = h2_min_size(count, dst_size - 1);
	const size_t copy = strnlen(src, available);
	memcpy(dst, src, copy);
	dst[copy] = '\0';

	return (src[copy] == '\0' || copy == count) ? 0 : ERANGE;
}

static inline int h2_strcat_s(char* dst, size_t dst_size, const char* src)
{
	if (dst == NULL || dst_size == 0)
		return EINVAL;

	const size_t len = strnlen(dst, dst_size);
	if (len >= dst_size)
	{
		dst[dst_size - 1] = '\0';
		return EINVAL;
	}

	return h2_strcpy_s(dst + len, dst_size - len, src);
}

static inline int h2_strncat_s(char* dst, size_t dst_size, const char* src, size_t count)
{
	if (dst == NULL || dst_size == 0)
		return EINVAL;

	const size_t len = strnlen(dst, dst_size);
	if (len >= dst_size)
	{
		dst[dst_size - 1] = '\0';
		return EINVAL;
	}

	return h2_strncpy_s(dst + len, dst_size - len, src, count);
}

static inline int h2_localtime_s(struct tm* out, const time_t* timer)
{
	return (out != NULL && timer != NULL && localtime_r(timer, out) != NULL) ? 0 : EINVAL;
}

static inline int h2_memmove_s(void* dst, size_t dst_size, const void* src, size_t count)
{
	if (dst == NULL || src == NULL)
		return EINVAL;
	if (count > dst_size)
		return ERANGE;
	memmove(dst, src, count);
	return 0;
}

static inline int h2_memcpy_s(void* dst, size_t dst_size, const void* src, size_t count)
{
	if (dst == NULL || src == NULL)
		return EINVAL;
	if (count > dst_size)
		return ERANGE;
	memcpy(dst, src, count);
	return 0;
}

static inline size_t h2_fread_s(void* dst, size_t dst_size, size_t elem_size, size_t elem_count, FILE* file)
{
	if (dst == NULL || file == NULL)
		return 0;
	if (elem_size != 0 && elem_count > dst_size / elem_size)
		elem_count = dst_size / elem_size;
	return fread(dst, elem_size, elem_count, file);
}

static inline int h2_sprintf_s(char* dst, size_t dst_size, const char* fmt, ...)
{
	if (dst == NULL || dst_size == 0)
		return EINVAL;

	va_list args;
	va_start(args, fmt);
	const int result = vsnprintf(dst, dst_size, fmt, args);
	va_end(args);

	if (result < 0 || (size_t)result >= dst_size)
	{
		dst[dst_size - 1] = '\0';
		return ERANGE;
	}

	return result;
}

static inline int h2_vsprintf_s(char* dst, size_t dst_size, const char* fmt, va_list args)
{
	if (dst == NULL || dst_size == 0)
		return EINVAL;

	const int result = vsnprintf(dst, dst_size, fmt, args);
	if (result < 0 || (size_t)result >= dst_size)
	{
		dst[dst_size - 1] = '\0';
		return ERANGE;
	}

	return result;
}

#ifndef __cplusplus
#define fopen_s(file, filename, mode) h2_fopen_s((file), (filename), (mode))
#define strcpy_s(dst, dst_size, src) h2_strcpy_s((dst), (dst_size), (src))
#define strncpy_s(dst, dst_size, src, count) h2_strncpy_s((dst), (dst_size), (src), (count))
#define strcat_s(dst, dst_size, src) h2_strcat_s((dst), (dst_size), (src))
#define strncat_s(dst, dst_size, src, count) h2_strncat_s((dst), (dst_size), (src), (count))
#define localtime_s(out, timer) h2_localtime_s((out), (timer))
#define memmove_s(dst, dst_size, src, count) h2_memmove_s((dst), (dst_size), (src), (count))
#define memcpy_s(dst, dst_size, src, count) h2_memcpy_s((dst), (dst_size), (src), (count))
#define fread_s(dst, dst_size, elem_size, elem_count, file) h2_fread_s((dst), (dst_size), (elem_size), (elem_count), (file))
#define sprintf_s(dst, dst_size, fmt, ...) h2_sprintf_s((dst), (dst_size), (fmt), ##__VA_ARGS__)
#define vsprintf_s(dst, dst_size, fmt, args) h2_vsprintf_s((dst), (dst_size), (fmt), (args))
#define sscanf_s sscanf
#define strtok_s(str, delim, ctx) strtok_r((str), (delim), (ctx))
#endif

static inline int _mkdir(const char* path)
{
	return mkdir(path, 0755);
}

#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_SUBDIR 0x10
#define _A_ARCH   0x20

struct _finddata_t
{
	unsigned attrib;
	char name[MAX_PATH];
};

typedef struct h2_find_s
{
	DIR* dir;
	char base[PATH_MAX];
	char pattern[PATH_MAX];
} h2_find_t;

static inline void h2_split_find_pattern(const char* path, char* base, size_t base_size, char* pattern, size_t pattern_size)
{
	const char* slash = strrchr(path, '/');
	const char* backslash = strrchr(path, '\\');
	if (backslash != NULL && (slash == NULL || backslash > slash))
		slash = backslash;

	if (slash != NULL)
	{
		const size_t len = h2_min_size((size_t)(slash - path), base_size - 1);
		memcpy(base, path, len);
		base[len] = '\0';
		h2_strcpy_s(pattern, pattern_size, slash + 1);
	}
	else
	{
		h2_strcpy_s(base, base_size, ".");
		h2_strcpy_s(pattern, pattern_size, path);
	}

	if (base[0] == '\0')
		h2_strcpy_s(base, base_size, "/");
}

static inline int h2_find_next_match(h2_find_t* find, struct _finddata_t* data)
{
	struct dirent* ent;
	while ((ent = readdir(find->dir)) != NULL)
	{
		if (fnmatch(find->pattern, ent->d_name, 0) != 0)
			continue;

		h2_strncpy_s(data->name, sizeof(data->name), ent->d_name, sizeof(ent->d_name) - 1);
		data->attrib = _A_ARCH;

		char full[PATH_MAX];
		h2_sprintf_s(full, sizeof(full), "%s/%s", find->base, ent->d_name);

		struct stat st;
		if (stat(full, &st) == 0)
		{
			if (S_ISDIR(st.st_mode))
				data->attrib |= _A_SUBDIR;
			if (!(st.st_mode & S_IWUSR))
				data->attrib |= _A_RDONLY;
		}

		if (ent->d_name[0] == '.')
			data->attrib |= _A_HIDDEN;

		return 0;
	}

	return -1;
}

static inline intptr_t _findfirst(const char* path, struct _finddata_t* data)
{
	h2_find_t* find = (h2_find_t*)calloc(1, sizeof(*find));
	if (find == NULL)
		return -1;

	h2_split_find_pattern(path, find->base, sizeof(find->base), find->pattern, sizeof(find->pattern));
	find->dir = opendir(find->base);
	if (find->dir == NULL)
	{
		free(find);
		return -1;
	}

	if (h2_find_next_match(find, data) != 0)
	{
		closedir(find->dir);
		free(find);
		return -1;
	}

	return (intptr_t)find;
}

static inline int _findnext(intptr_t handle, struct _finddata_t* data)
{
	if (handle == -1 || handle == 0)
		return -1;

	return h2_find_next_match((h2_find_t*)handle, data);
}

static inline int _findclose(intptr_t handle)
{
	if (handle == -1 || handle == 0)
		return -1;

	h2_find_t* find = (h2_find_t*)handle;
	closedir(find->dir);
	free(find);
	return 0;
}

#ifndef H2_STATIC_GAME
// Dynamic-library helpers implemented in src/unix/dlopen_unix.c.
void* H2R_LoadLibrary(const char* name);
void* H2R_GetProcAddress(void* library, const char* symbol);
int H2R_FreeLibrary(void* library);

#define LoadLibrary(name) H2R_LoadLibrary((name))
#define GetProcAddress(library, symbol) H2R_GetProcAddress((library), (symbol))
#define FreeLibrary(library) H2R_FreeLibrary((library))
#endif

#ifdef __cplusplus
}

#include <cstdarg>
#include <cstdio>
#include <cstring>

static inline int fopen_s(FILE** file, const char* filename, const char* mode)
{
	return h2_fopen_s(file, filename, mode);
}

static inline int strcpy_s(char* dst, size_t dst_size, const char* src)
{
	return h2_strcpy_s(dst, dst_size, src);
}

template <size_t N>
static inline int strcpy_s(char (&dst)[N], const char* src)
{
	return h2_strcpy_s(dst, N, src);
}

static inline int strncpy_s(char* dst, size_t dst_size, const char* src, size_t count)
{
	return h2_strncpy_s(dst, dst_size, src, count);
}

template <size_t N>
static inline int strncpy_s(char (&dst)[N], const char* src, size_t count)
{
	return h2_strncpy_s(dst, N, src, count);
}

static inline int strcat_s(char* dst, size_t dst_size, const char* src)
{
	return h2_strcat_s(dst, dst_size, src);
}

template <size_t N>
static inline int strcat_s(char (&dst)[N], const char* src)
{
	return h2_strcat_s(dst, N, src);
}

static inline int strncat_s(char* dst, size_t dst_size, const char* src, size_t count)
{
	return h2_strncat_s(dst, dst_size, src, count);
}

static inline int localtime_s(struct tm* out, const time_t* timer)
{
	return h2_localtime_s(out, timer);
}

static inline int memmove_s(void* dst, size_t dst_size, const void* src, size_t count)
{
	return h2_memmove_s(dst, dst_size, src, count);
}

static inline int memcpy_s(void* dst, size_t dst_size, const void* src, size_t count)
{
	return h2_memcpy_s(dst, dst_size, src, count);
}

static inline size_t fread_s(void* dst, size_t dst_size, size_t elem_size, size_t elem_count, FILE* file)
{
	return h2_fread_s(dst, dst_size, elem_size, elem_count, file);
}

static inline int sprintf_s(char* dst, size_t dst_size, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	const int result = h2_vsprintf_s(dst, dst_size, fmt, args);
	va_end(args);
	return result;
}

template <size_t N>
static inline int sprintf_s(char (&dst)[N], const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	const int result = h2_vsprintf_s(dst, N, fmt, args);
	va_end(args);
	return result;
}

static inline int vsprintf_s(char* dst, size_t dst_size, const char* fmt, va_list args)
{
	return h2_vsprintf_s(dst, dst_size, fmt, args);
}

template <size_t N>
static inline int vsprintf_s(char (&dst)[N], const char* fmt, va_list args)
{
	return h2_vsprintf_s(dst, N, fmt, args);
}

#define sscanf_s sscanf
#define strtok_s(str, delim, ctx) strtok_r((str), (delim), (ctx))
#endif

#endif // _WIN32

#ifdef H2_STATIC_GAME
#ifdef __cplusplus
extern "C" {
#endif
void* H2R_LoadLibrary(const char* name);
void* H2R_GetProcAddress(void* library, const char* symbol);
int H2R_FreeLibrary(void* library);
#ifdef __cplusplus
}
#endif

#ifndef _WIN32
#undef LoadLibrary
#undef GetProcAddress
#undef FreeLibrary
#define LoadLibrary(name) H2R_LoadLibrary((name))
#define GetProcAddress(library, symbol) H2R_GetProcAddress((void*)(library), (symbol))
#define FreeLibrary(library) H2R_FreeLibrary((void*)(library))
#endif
#endif
