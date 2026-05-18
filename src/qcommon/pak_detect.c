//
// pak_detect.c -- Automatic PAK file detection from Steam/GOG/Program Files
//
// Searches common Heretic II installation locations and copies PAK files if found.
//

#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "qcommon.h"

#pragma region ========================== Registry Utilities ==========================

// Query Windows Registry for a value
static qboolean Reg_QueryString(HKEY hive, const char* path, const char* value, char* buffer, int buffer_size)
{
	HKEY hkey;
	if (RegOpenKeyExA(hive, path, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
		return false;

	DWORD size = buffer_size;
	qboolean success = (RegQueryValueExA(hkey, value, NULL, NULL, (LPBYTE)buffer, &size) == ERROR_SUCCESS);
	RegCloseKey(hkey);

	return success;
}

#pragma endregion

#pragma region ========================== Search Paths ==========================

// Possible Heretic II installation paths to search
static struct
{
	const char* registry_path;
	const char* registry_value;
	const char* fallback_path;
	qboolean check_registry;
} search_paths[] = {
	// Steam
	{ "SOFTWARE\\Valve\\Steam", "SteamPath", NULL, true },
	
	// GOG
	{ "SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1444149254", "path", NULL, true },
	{ "SOFTWARE\\GOG.com\\Games\\1444149254", "path", NULL, true },
	
	// Program Files (check common locations)
	{ NULL, NULL, "C:\\Program Files\\Heretic II", false },
	{ NULL, NULL, "C:\\Program Files (x86)\\Heretic II", false },
	{ NULL, NULL, "C:\\Games\\Heretic II", false },
	{ NULL, NULL, "D:\\Games\\Heretic II", false },
};

#pragma endregion

#pragma region ========================== PAK File Operations ==========================

// Check if a file exists
static qboolean FileExists(const char* path)
{
	return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

// Copy a file, creating directories if needed
static qboolean CopyPakFile(const char* src, const char* dst)
{
	if (!FileExists(src))
		return false;

	// Create destination directory if needed
	char dir[MAX_OSPATH];
	strcpy_s(dir, sizeof(dir), dst);
	char* slash = strrchr(dir, '\\');
	if (slash != NULL)
	{
		*slash = '\0';
		CreateDirectoryA(dir, NULL); // Returns false if already exists, that's ok
	}

	// Copy the file
	if (!CopyFileA(src, dst, FALSE)) // FALSE = overwrite if exists
	{
		Com_Printf("Warning: Failed to copy PAK file from %s to %s\n", src, dst);
		return false;
	}

	Com_Printf("Copied PAK file from %s to %s\n", src, dst);
	return true;
}

// Search for PAK files in a directory and copy them
static qboolean SearchAndCopyPaksFromDir(const char* game_dir, const char* base_dir)
{
	if (!FileExists(game_dir))
		return false;

	char search_path[MAX_OSPATH];
	Com_sprintf(search_path, sizeof(search_path), "%s\\*", game_dir);

	WIN32_FIND_DATAA find_data;
	HANDLE find_handle = FindFirstFileA(search_path, &find_data);
	if (find_handle == INVALID_HANDLE_VALUE)
		return false;

	qboolean found_pak0 = false;
	qboolean found_pak1 = false;
	qboolean copied_any = false;

	do
	{
		const char* filename = find_data.cFileName;
		
		// Check for Htic2-0.pak and Htic2-1.pak
		if (_stricmp(filename, "Htic2-0.pak") == 0)
		{
			char src[MAX_OSPATH], dst[MAX_OSPATH];
			Com_sprintf(src, sizeof(src), "%s\\%s", game_dir, filename);
			Com_sprintf(dst, sizeof(dst), "%s\\%s", base_dir, filename);

			if (!FileExists(dst)) // Only copy if not already present
			{
				if (CopyPakFile(src, dst))
					copied_any = true;
			}
			found_pak0 = true;
		}
		else if (_stricmp(filename, "Htic2-1.pak") == 0)
		{
			char src[MAX_OSPATH], dst[MAX_OSPATH];
			Com_sprintf(src, sizeof(src), "%s\\%s", game_dir, filename);
			Com_sprintf(dst, sizeof(dst), "%s\\%s", base_dir, filename);

			if (!FileExists(dst)) // Only copy if not already present
			{
				if (CopyPakFile(src, dst))
					copied_any = true;
			}
			found_pak1 = true;
		}

	} while (FindNextFileA(find_handle, &find_data));

	FindClose(find_handle);

	// Return true if we found both files (even if we didn't need to copy)
	return found_pak0 && found_pak1;
}

#pragma endregion

#pragma region ========================== Main PAK Detection ==========================

qboolean CD_SearchAndCopyPaks(const char* base_dir)
{
	Com_Printf("Searching for Heretic II PAK files...\n");

	// Search through all possible paths
	for (size_t i = 0; i < sizeof(search_paths) / sizeof(search_paths[0]); i++)
	{
		const char* registry_path = search_paths[i].registry_path;
		const char* registry_value = search_paths[i].registry_value;
		const char* fallback_path = search_paths[i].fallback_path;

		char install_dir[MAX_OSPATH];
		qboolean found = false;

		if (search_paths[i].check_registry && registry_path != NULL)
		{
			// Try to get path from Registry
			if (Reg_QueryString(HKEY_LOCAL_MACHINE, registry_path, registry_value, install_dir, sizeof(install_dir)))
			{
				found = true;
				Com_Printf("  Found Heretic II installation via Registry: %s\n", install_dir);
			}
		}
		else if (fallback_path != NULL)
		{
			// Try fallback path
			strcpy_s(install_dir, sizeof(install_dir), fallback_path);
			Com_Printf("  Checking common location: %s\n", install_dir);
			found = FileExists(install_dir);
		}

		if (found)
		{
			// Check for PAK files in Steam directory
			if (search_paths[i].check_registry && registry_path != NULL)
			{
				// Steam: Htic2 is usually in SteamPath\steamapps\common\Heretic 2
				char steam_h2_path[MAX_OSPATH];
				Com_sprintf(steam_h2_path, sizeof(steam_h2_path), "%s\\steamapps\\common\\Heretic 2", install_dir);
				if (SearchAndCopyPaksFromDir(steam_h2_path, base_dir))
					return true;

				// Also check just the install dir
				if (SearchAndCopyPaksFromDir(install_dir, base_dir))
					return true;
			}
			else
			{
				// GOG or Program Files: check directly
				if (SearchAndCopyPaksFromDir(install_dir, base_dir))
					return true;
			}
		}
	}

	Com_Printf("No Heretic II PAK files found in common locations.\n");
	return false;
}

#pragma endregion

#else

#include "qcommon.h"

#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#pragma region ========================== POSIX/macOS Helpers ==========================

static const char* data_files[] =
{
	"base.pak",
	"Htic2-0.pak",
	"Htic2-1.pak",
	"Htic2-2.pak",
	"Htic2-3.pak",
	"Htic2-4.pak",
	"Htic2-5.pak",
	"Htic2-6.pak",
	"Htic2-7.pak",
	"Htic2-8.pak",
	"Htic2-9.pak",
	"Art/WidescreenBook.png",
	NULL
};

static qboolean FileExists(const char* path)
{
	return Sys_IsFile(path);
}

static qboolean DirectoryExists(const char* path)
{
	return Sys_IsDir(path);
}

static void EnsureDirectory(const char* dir)
{
	char path[MAX_OSPATH];
	Com_sprintf(path, sizeof(path), "%s/.keep", dir);
	FS_CreatePath(path);
	Sys_Mkdir(dir);
}

static qboolean SamePath(const char* a, const char* b)
{
	char real_a[MAX_OSPATH];
	char real_b[MAX_OSPATH];

	if (realpath(a, real_a) == NULL || realpath(b, real_b) == NULL)
		return false;

	return strcmp(real_a, real_b) == 0;
}

static qboolean FindFileCaseInsensitive(const char* dir, const char* filename, char* out, const size_t out_size)
{
	const char* slash = strchr(filename, '/');
	char component[MAX_OSPATH];

	if (slash != NULL)
	{
		const size_t component_len = slash - filename;
		if (component_len == 0 || component_len >= sizeof(component))
			return false;

		memcpy(component, filename, component_len);
		component[component_len] = '\0';
	}
	else
	{
		strcpy_s(component, sizeof(component), filename);
	}

	DIR* d = opendir(dir);
	if (d == NULL)
		return false;

	struct dirent* ent;
	while ((ent = readdir(d)) != NULL)
	{
		if (_stricmp(ent->d_name, component) != 0)
			continue;

		char found[MAX_OSPATH];
		Com_sprintf(found, sizeof(found), "%s/%s", dir, ent->d_name);
		closedir(d);

		if (slash != NULL)
			return DirectoryExists(found) && FindFileCaseInsensitive(found, slash + 1, out, out_size);

		strcpy_s(out, out_size, found);
		return FileExists(out);
	}

	closedir(d);
	return false;
}

static qboolean CopyDataFile(const char* src_dir, const char* base_dir, const char* filename)
{
	char src[MAX_OSPATH];
	if (!FindFileCaseInsensitive(src_dir, filename, src, sizeof(src)))
		return false;

	char dst[MAX_OSPATH];
	Com_sprintf(dst, sizeof(dst), "%s/%s", base_dir, filename);

	if (FileExists(dst))
		return true;

	if (SamePath(src, dst))
		return true;

	EnsureDirectory(base_dir);
	FS_CreatePath(dst);

	if (!FS_CopyFile(src, dst))
	{
		Com_Printf("Warning: Failed to copy game data from %s to %s\n", src, dst);
		return false;
	}

	Com_Printf("Copied game data from %s to %s\n", src, dst);
	return true;
}

static qboolean SearchAndCopyDataFromDir(const char* game_dir, const char* base_dir)
{
	if (!DirectoryExists(game_dir))
		return false;

	qboolean copied_any = false;
	qboolean found_pak0 = false;
	qboolean found_pak1 = false;

	for (int i = 0; data_files[i] != NULL; i++)
	{
		const char* filename = data_files[i];
		const qboolean copied = CopyDataFile(game_dir, base_dir, filename);
		copied_any = copied_any || copied;

		if (_stricmp(filename, "Htic2-0.pak") == 0 && copied)
			found_pak0 = true;
		else if (_stricmp(filename, "Htic2-1.pak") == 0 && copied)
			found_pak1 = true;
	}

	if (copied_any)
		Com_Printf("  Found Heretic II game data in: %s\n", game_dir);

	return found_pak0 && found_pak1;
}

static qboolean SearchCandidateRoot(const char* root, const char* base_dir)
{
	if (root == NULL || root[0] == '\0')
		return false;

	if (SearchAndCopyDataFromDir(root, base_dir))
		return true;

	char path[MAX_OSPATH];
	Com_sprintf(path, sizeof(path), "%s/base", root);
	if (SearchAndCopyDataFromDir(path, base_dir))
		return true;

	Com_sprintf(path, sizeof(path), "%s/Base", root);
	if (SearchAndCopyDataFromDir(path, base_dir))
		return true;

	return false;
}

static qboolean SearchHomeLocations(const char* base_dir)
{
	const char* home = getenv("HOME");
	if (home == NULL || home[0] == '\0')
		return false;

	static const char* home_suffixes[] =
	{
		"Library/Application Support/Heretic2R",
		"Library/Application Support/Heretic 2 Remastered",
		"Library/Application Support/Heretic II",
		"Library/Application Support/Steam/steamapps/common/Heretic 2",
		"Library/Application Support/Steam/steamapps/common/Heretic II",
		"GOG Games/Heretic II",
		"Games/Heretic II",
		"Games/Heretic 2",
		NULL
	};

	for (int i = 0; home_suffixes[i] != NULL; i++)
	{
		char path[MAX_OSPATH];
		Com_sprintf(path, sizeof(path), "%s/%s", home, home_suffixes[i]);
		if (SearchCandidateRoot(path, base_dir))
			return true;
	}

	return false;
}

#ifdef __APPLE__
static qboolean SearchAppBundleResources(const char* base_dir)
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

	if (SearchCandidateRoot(resources, base_dir))
		return true;

	char path[MAX_OSPATH];
	Com_sprintf(path, sizeof(path), "%s/Heretic2R", resources);
	if (SearchCandidateRoot(path, base_dir))
		return true;

	Com_sprintf(path, sizeof(path), "%s/Heretic II", resources);
	return SearchCandidateRoot(path, base_dir);
}
#endif

static qboolean SearchApplications(const char* base_dir)
{
	static const char* app_paths[] =
	{
		"/Applications/Heretic2R",
		"/Applications/Heretic 2 Remastered",
		"/Applications/Heretic II",
		"/Applications/Heretic II.app/Contents/Resources",
		"/Applications/Heretic 2.app/Contents/Resources",
		"/Applications/Heretic2R.app/Contents/Resources",
		NULL
	};

	for (int i = 0; app_paths[i] != NULL; i++)
		if (SearchCandidateRoot(app_paths[i], base_dir))
			return true;

	return false;
}

static qboolean StringContainsNoCase(const char* haystack, const char* needle)
{
	const size_t needle_len = strlen(needle);
	if (needle_len == 0)
		return true;

	for (const char* p = haystack; *p != '\0'; p++)
		if (_strnicmp(p, needle, needle_len) == 0)
			return true;

	return false;
}

static qboolean DirNameLooksPromising(const char* name)
{
	return (_stricmp(name, "base") == 0 ||
		_stricmp(name, "Data") == 0 ||
		_stricmp(name, "GameData") == 0 ||
		StringContainsNoCase(name, "Heretic") ||
		StringContainsNoCase(name, "Htic"));
}

static qboolean SearchMountedVolumeDir(const char* dir, const char* base_dir, const int depth)
{
	if (SearchCandidateRoot(dir, base_dir))
		return true;

	if (depth <= 0)
		return false;

	DIR* d = opendir(dir);
	if (d == NULL)
		return false;

	struct dirent* ent;
	while ((ent = readdir(d)) != NULL)
	{
		if (ent->d_name[0] == '.')
			continue;
		if (!DirNameLooksPromising(ent->d_name) && depth < 3)
			continue;

		char child[MAX_OSPATH];
		Com_sprintf(child, sizeof(child), "%s/%s", dir, ent->d_name);
		if (!DirectoryExists(child))
			continue;

		if (SearchMountedVolumeDir(child, base_dir, depth - 1))
		{
			closedir(d);
			return true;
		}
	}

	closedir(d);
	return false;
}

static qboolean SearchMountedVolumes(const char* base_dir)
{
	DIR* volumes = opendir("/Volumes");
	if (volumes == NULL)
		return false;

	qboolean found = false;
	struct dirent* ent;
	while ((ent = readdir(volumes)) != NULL)
	{
		if (ent->d_name[0] == '.')
			continue;

		char volume[MAX_OSPATH];
		Com_sprintf(volume, sizeof(volume), "/Volumes/%s", ent->d_name);
		if (!DirectoryExists(volume))
			continue;

		if (SearchMountedVolumeDir(volume, base_dir, 3))
		{
			found = true;
			break;
		}
	}

	closedir(volumes);
	return found;
}

#pragma endregion

qboolean CD_SearchAndCopyPaks(const char* base_dir)
{
	Com_Printf("Searching for Heretic II game data...\n");

	char base_pak_path[MAX_OSPATH];
	Com_sprintf(base_pak_path, sizeof(base_pak_path), "%s/base.pak", base_dir);

	char widescreen_book_path[MAX_OSPATH];
	Com_sprintf(widescreen_book_path, sizeof(widescreen_book_path), "%s/Art/WidescreenBook.png", base_dir);

#ifdef __APPLE__
	if (SearchAppBundleResources(base_dir) && FileExists(base_pak_path) && FileExists(widescreen_book_path))
		return true;
#endif

	const qboolean found_required_in_base = SearchCandidateRoot(base_dir, base_dir);
	if (found_required_in_base && FileExists(base_pak_path) && FileExists(widescreen_book_path))
		return true;

	// The original PAKs are enough to start the game, but the remastered assets
	// live in base.pak and a few loose support files. Don't stop at a partial
	// base dir or macOS/Linux builds run with missing player skins, HUD art,
	// menu art, music, etc.
	if (SearchHomeLocations(base_dir) && FileExists(base_pak_path) && FileExists(widescreen_book_path))
		return true;

	if (SearchApplications(base_dir) && FileExists(base_pak_path) && FileExists(widescreen_book_path))
		return true;

	if (SearchMountedVolumes(base_dir) && FileExists(base_pak_path) && FileExists(widescreen_book_path))
		return true;

	if (found_required_in_base)
		return true;

	Com_Printf("No Heretic II game data found in common locations.\n");
	return false;
}

#endif // _WIN32
