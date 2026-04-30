//
// q_Typedef.h
//
// Copyright 1998 Raven Software
//

#pragma once

#ifndef _WIN32
	#include "platform_compat.h"
#endif

typedef float vec3_t[3];
typedef float matrix3_t[3][3];

typedef unsigned short ushort;	//mxd. Way shorter than "unsigned short"...
typedef unsigned uint;			//mxd. Shorter than "size_t", way shorter than "unsigned int"...
typedef unsigned long ulong;	//mxd. Way shorter than "unsigned long"...
typedef unsigned char byte;

#if !defined(__cplusplus) && !defined(__OBJC__) //mxd. Needed, so C++/Objective-C translation units can build...
	typedef enum { false, true } qboolean;
#else
	typedef int qboolean;
#endif

typedef struct edict_s edict_t;

typedef struct paletteRGBA_s
{
	union
	{
		struct
		{
			byte r;
			byte g;
			byte b;
			byte a;
		};
		uint c;
		byte c_array[4];
	};
} paletteRGBA_t;