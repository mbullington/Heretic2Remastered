#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR="$ROOT/.dos-build/guest"
PLAY_DIR="$ROOT/.dos-build/play"
DATA_DIR=${H2DOS_ORIGINAL_DATA:-"$ROOT/.dos-data"}
DOSBOX=${DOSBOX:-dosbox-staging}
# DOSBox Staging suggests 100000 for a Pentium MMX 166. Scaling by
# 233/166 gives about 140000 as a starting point for a Pentium MMX 233.
CYCLES=${H2DOS_CYCLES:-140000}
MAP=${H2DOS_MAP-ssdocks}
FULLSCREEN=${H2DOS_FULLSCREEN:-0}
TILE=${H2DOS_TILE:-0}
IPXSERVER=${H2DOS_IPXSERVER:-}
IPXCONNECT=${H2DOS_IPXCONNECT:-}

usage() {
	cat <<EOF
Usage: scripts/run-dos-play.sh

Stage the DOS build and original PAKs, then play Heretic II in DOSBox Staging.
Missing DOS binaries are built automatically.

First run: you need a legal copy of Heretic II (Steam, GOG, or CD). This
launcher searches the usual install locations for its two data archives
(Htic2-0.pak and Htic2-1.pak) and uses them automatically. If they are not
found, it prints where to look and where to copy them.

Environment variables:
  DOSBOX                 DOSBox Staging executable (default: dosbox-staging)
  H2DOS_ORIGINAL_DATA    Directory containing base/Htic2-{0,1}.pak (auto-detected from
                         Steam/GOG/CD installs when unset)
  H2DOS_CYCLES           Fixed emulated CPU speed (default: 140000)
  H2DOS_MAP              Initial map (default: ssdocks; empty opens the menu)
  H2DOS_FULLSCREEN       Start fullscreen, 0 or 1 (default: 0)
  H2DOS_TILE             Tile round-robin world bands, 0=off, 2-16 (default: 0)
  H2DOS_IPXSERVER        Host an IPX tunnel on this UDP port (DOSBox ipxnet startserver)
  H2DOS_IPXCONNECT       Join an IPX tunnel, host:port (DOSBox ipxnet connect)

Examples:
  ./scripts/run-dos-play.sh
  H2DOS_ORIGINAL_DATA="$HOME/GOG Games/Heretic II" ./scripts/run-dos-play.sh
  H2DOS_CYCLES=120000 ./scripts/run-dos-play.sh
  H2DOS_MAP= H2DOS_FULLSCREEN=1 ./scripts/run-dos-play.sh
EOF
}

case "${1-}" in
	-h|--help)
		usage
		exit 0
		;;
esac
if [ "$#" -ne 0 ]; then
	echo "Unexpected argument: $1" >&2
	echo "Run scripts/run-dos-play.sh --help for usage." >&2
	exit 2
fi

case "$CYCLES" in
	''|*[!0-9]*)
		echo "H2DOS_CYCLES must be a positive integer" >&2
		exit 2
		;;
	0)
		echo "H2DOS_CYCLES must be a positive integer" >&2
		exit 2
		;;
esac
case "$TILE" in
	''|*[!0-9]*)
		echo "H2DOS_TILE must be an integer from 0 to 16" >&2
		exit 2
		;;
esac
if [ "$TILE" -gt 16 ]; then
	echo "H2DOS_TILE must be an integer from 0 to 16" >&2
	exit 2
fi
case "$FULLSCREEN" in
	0|1) ;;
	*)
		echo "H2DOS_FULLSCREEN must be 0 or 1" >&2
		exit 2
		;;
esac
case "$MAP" in
	*[!A-Za-z0-9_./-]*)
		echo "H2DOS_MAP contains unsupported characters: $MAP" >&2
		exit 2
		;;
esac


# --- Original game data discovery -----------------------------------------
# The DOS port needs the two data archives from a legal Heretic II copy:
# Htic2-0.pak (~206 MB) and Htic2-1.pak (~42 MB). Look in the locations a
# Steam, GOG, or CD install would put them on each host OS before giving up.
PAK0_SRC=""
PAK1_SRC=""
FOUND_AT=""

is_pak() {
	[ -f "$1" ] && [ "$(head -c 4 "$1" 2>/dev/null)" = "PACK" ]
}

# $1 = directory that directly contains both PAK files.
try_dir() {
	if is_pak "$1/Htic2-0.pak" && is_pak "$1/Htic2-1.pak"; then
		PAK0_SRC="$1/Htic2-0.pak"
		PAK1_SRC="$1/Htic2-1.pak"
		FOUND_AT="$1"
		return 0
	fi
	return 1
}

# $1 = an installation root; the PAKs may sit in it or in its base/ folder.
try_root() {
	try_dir "$1" || try_dir "$1/base" || try_dir "$1/Base"
}

try_steam_library() {
	for name in "Heretic 2" "Heretic II" "Heretic 2 Remastered" "Heretic II Remastered"; do
		try_root "$1/steamapps/common/$name" && return 0
	done
	return 1
}

# Print additional Steam library roots recorded in libraryfolders.vdf.
steam_extra_libraries() {
	vdf="$1/steamapps/libraryfolders.vdf"
	[ -f "$vdf" ] || return 0
	grep -o '"path"[[:space:]]*"[^"]*"' "$vdf" 2>/dev/null |
		sed -e 's/.*"path"[[:space:]]*"//' -e 's/"$//' -e 's/\\\\/\//g' || true
}

try_steam_root() {
	try_steam_library "$1" && return 0
	for lib in $(steam_extra_libraries "$1"); do
		try_steam_library "$lib" && return 0
	done
	return 1
}

try_cd_zip() {
	# Original CD: the PAKs sit in Setup/zip/h2.zip on the disc.
	[ -f "$1" ] || return 1
	command -v unzip >/dev/null 2>&1 || return 1
	echo "Found a Heretic II CD image at $1; extracting its PAK files..."
	mkdir -p "$ROOT/.dos-data/base"
	unzip -o -j "$1" "*Htic2-0.pak" "*Htic2-1.pak" -d "$ROOT/.dos-data/base" >/dev/null 2>&1 || true
	try_dir "$ROOT/.dos-data/base"
}

find_game_data() {
	# Explicit location (and a developer's repo-local base/) first.
	try_root "$DATA_DIR" && return 0
	try_dir "$ROOT/base" && return 0

	case "$(uname -s 2>/dev/null)" in
	Darwin)
		try_steam_root "$HOME/Library/Application Support/Steam" && return 0
		try_root "$HOME/Library/Application Support/Heretic2R" && return 0
		try_root "$HOME/GOG Games/Heretic II" && return 0
		try_root "/Applications/Heretic II.app/Contents/Resources" && return 0
		try_root "/Applications/Heretic 2.app/Contents/Resources" && return 0
		for zip in /Volumes/*/Setup/zip/h2.zip; do
			try_cd_zip "$zip" && return 0
		done
		;;
	Linux)
		for steam in "$HOME/.local/share/Steam" "$HOME/.steam/steam" \
			"$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
			try_steam_root "$steam" && return 0
		done
		try_root "$HOME/GOG Games/Heretic II" && return 0
		try_root "$HOME/Games/Heretic II" && return 0
		try_root "$HOME/Games/Heroic/Heretic II" && return 0
		for zip in /media/"$USER"/*/Setup/zip/h2.zip /run/media/"$USER"/*/Setup/zip/h2.zip; do
			try_cd_zip "$zip" && return 0
		done
		;;
	MINGW*|MSYS*|CYGWIN*)
		for drive in c d e; do
			try_steam_library "/$drive/Program Files (x86)/Steam" && return 0
			try_steam_library "/$drive/Program Files/Steam" && return 0
			try_steam_library "/$drive/SteamLibrary" && return 0
			try_root "/$drive/GOG Games/Heretic II" && return 0
			try_root "/$drive/Program Files (x86)/GOG Galaxy/Games/Heretic II" && return 0
			try_root "/$drive/Heretic II" && return 0
			try_root "/$drive/Games/Heretic II" && return 0
		done
		;;
	esac
	return 1
}

print_data_help() {
	cat >&2 <<EOF

run-dos-play: Heretic II game data not found.

This DOS port is only an engine; it does not include the game itself. It
needs the two data archives ("PAK files") from a legal copy of Heretic II
(Steam, GOG, or the original CD):

    Htic2-0.pak   (about 206 MB)
    Htic2-1.pak   (about 42 MB)

This script already searched the usual Steam, GOG, and CD locations on
this machine and found neither file. If you own the game, look here:

  Steam (Windows)  C:\\Program Files (x86)\\Steam\\steamapps\\common\\Heretic 2\\base\\
  Steam (macOS)    ~/Library/Application Support/Steam/steamapps/common/Heretic 2/base/
  Steam (Linux)    ~/.local/share/Steam/steamapps/common/Heretic 2/base/
                     (a custom Steam library folder works too; it is read
                      from Steam's libraryfolders.vdf automatically)
  GOG (Windows)    C:\\GOG Games\\Heretic II\\base\\
                     (GOG Galaxy: C:\\Program Files (x86)\\GOG Galaxy\\Games\\Heretic II\\base\\)
  Original CD      inside Setup\\zip\\h2.zip on the disc (auto-extracted
                     when the mounted CD is found on macOS/Linux)

Then either copy both files into:

    $DATA_DIR/base/

or point the launcher at the folder that contains the base/ folder:

    H2DOS_ORIGINAL_DATA="/path/to/install" ./scripts/run-dos-play.sh

See docs/dos-build.md ("Getting the game data") for details.
EOF
}

if ! command -v "$DOSBOX" >/dev/null 2>&1; then
	echo "DOSBox Staging not found: $DOSBOX" >&2
	exit 1
fi

if [ ! -f "$BUILD_DIR/SILVER.EXE" ] || [ ! -f "$BUILD_DIR/CWSDPMI.EXE" ]; then
	echo "DOS build artifacts are missing; building them now."
	"$ROOT/scripts/build-dos-bootstrap.sh"
fi

if ! find_game_data; then
	print_data_help
	exit 1
fi
if [ "$FOUND_AT" != "$DATA_DIR/base" ]; then
	echo "Using Heretic II game data found in: $FOUND_AT"
fi

mkdir -p "$PLAY_DIR/H2REAL/base"
cp "$BUILD_DIR/SILVER.EXE" "$BUILD_DIR/CWSDPMI.EXE" "$PLAY_DIR/"
cp "$ROOT/dos/h2dos_default.cfg" "$PLAY_DIR/H2REAL/base/default.cfg"
for pak in Htic2-0.pak Htic2-1.pak; do
	if [ "$pak" = "Htic2-0.pak" ]; then
		source_pak="$PAK0_SRC"
	else
		source_pak="$PAK1_SRC"
	fi
	staged_pak="$PLAY_DIR/H2REAL/base/$pak"
	if [ ! -f "$staged_pak" ] ||
		[ "$source_pak" -nt "$staged_pak" ] ||
		[ "$(wc -c < "$source_pak")" -ne "$(wc -c < "$staged_pak")" ]; then
		cp "$source_pak" "$staged_pak"
	fi
done

if [ -n "$MAP" ]; then
	START_COMMAND=" +map $MAP"
else
	START_COMMAND=" +menu_main"
fi
if [ "$TILE" != "0" ]; then
	START_COMMAND="$START_COMMAND +set r_swtile $TILE"
fi

{
	printf '@echo off\r\n'
	printf 'set H2DOS_SW_WIDTH=320\r\n'
	printf 'set H2DOS_SW_HEIGHT=240\r\n'
	printf 'silver.exe -dosplay -basedir H2REAL +set s_khz 22%s\r\n' "$START_COMMAND"
} > "$PLAY_DIR/PLAY.BAT"

set -- \
	--noprimaryconf \
	--nolocalconf \
	--noautoexec \
	--set "memsize=64" \
	--set "machine=svga_s3" \
	--set "cputype=pentium_mmx" \
	--set "cpu_cycles=$CYCLES" \
	--set "cpu_cycles_protected=$CYCLES" \
	--set "ipx=true" \
	--exit \
	-c "mount c \"$PLAY_DIR\"" \
	-c "c:"
if [ -n "$IPXSERVER" ]; then
	set -- "$@" -c "ipxnet startserver $IPXSERVER"
fi
if [ -n "$IPXCONNECT" ]; then
	set -- "$@" -c "ipxnet connect $IPXCONNECT"
fi
set -- "$@" \
	-c "play.bat" \
	-c "exit"
if [ "$FULLSCREEN" = 1 ]; then
	set -- --fullscreen "$@"
fi

printf 'Starting Heretic II DOS at fixed %s DOSBox cycles' "$CYCLES"
if [ -n "$MAP" ]; then
	printf ' on map %s' "$MAP"
else
	printf ' at the main menu'
fi
printf '.\n'
printf 'The opening SSDocks camera locks controls; press Escape once to skip it.\n'
printf 'Press Ctrl-F11/Ctrl-F12 to adjust cycles, or Alt-Enter for fullscreen.\n'
exec "$DOSBOX" "$@"
