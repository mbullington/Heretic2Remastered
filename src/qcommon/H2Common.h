//
// H2Common.h
//
// Copyright 1998 Raven Software
//

#pragma once

#ifndef _WIN32
	#include "platform_compat.h"
#endif

#if defined(H2_STATIC_GAME)
	#define H2COMMON_API
#elif defined(_WIN32)
	#ifdef H2COMMON
		#define H2COMMON_API __declspec(dllexport)
	#else
		#define H2COMMON_API __declspec(dllimport)
	#endif
#else
	#define H2COMMON_API __attribute__((visibility("default")))
#endif