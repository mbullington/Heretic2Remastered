//
// DllMain.c
//
// Copyright 1998 Raven Software
//

#include <windows.h>
#include "ResourceManager.h"
#include "SinglyLinkedList.h"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	switch (fdwReason)
	{
		case DLL_PROCESS_ATTACH:
			SLList_Init();
			break;

		case DLL_PROCESS_DETACH:
			SLList_Shutdown();
			break;

		default:
			break;
	}

	return TRUE;
}