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

Environment variables:
  DOSBOX                 DOSBox Staging executable (default: dosbox-staging)
  H2DOS_ORIGINAL_DATA    Directory containing base/Htic2-{0,1}.pak
  H2DOS_CYCLES           Fixed emulated CPU speed (default: 140000)
  H2DOS_MAP              Initial map (default: ssdocks; empty opens the menu)
  H2DOS_FULLSCREEN       Start fullscreen, 0 or 1 (default: 0)
  H2DOS_TILE             Tile round-robin world bands, 0=off, 2-16 (default: 0)
  H2DOS_IPXSERVER        Host an IPX tunnel on this UDP port (DOSBox ipxnet startserver)
  H2DOS_IPXCONNECT       Join an IPX tunnel, host:port (DOSBox ipxnet connect)

Examples:
  ./scripts/run-dos-play.sh
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

if ! command -v "$DOSBOX" >/dev/null 2>&1; then
	echo "DOSBox Staging not found: $DOSBOX" >&2
	exit 1
fi

if [ ! -f "$BUILD_DIR/SILVER.EXE" ] || [ ! -f "$BUILD_DIR/CWSDPMI.EXE" ]; then
	echo "DOS build artifacts are missing; building them now."
	"$ROOT/scripts/build-dos-bootstrap.sh"
fi

for pak in Htic2-0.pak Htic2-1.pak; do
	if [ ! -f "$DATA_DIR/base/$pak" ]; then
		echo "Missing original game data: $DATA_DIR/base/$pak" >&2
		echo "Set H2DOS_ORIGINAL_DATA to the directory containing base/$pak." >&2
		exit 1
	fi
done

mkdir -p "$PLAY_DIR/H2REAL/base"
cp "$BUILD_DIR/SILVER.EXE" "$BUILD_DIR/CWSDPMI.EXE" "$PLAY_DIR/"
cp "$ROOT/dos/h2dos_default.cfg" "$PLAY_DIR/H2REAL/base/default.cfg"
for pak in Htic2-0.pak Htic2-1.pak; do
	source_pak="$DATA_DIR/base/$pak"
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
