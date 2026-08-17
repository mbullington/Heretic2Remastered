#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NIXPKGS_REVISION=f2b17eb1b90f6a359082035b4538d4c63017aec3
NIXPKGS="github:NixOS/nixpkgs/$NIXPKGS_REVISION"
NIX_DJGPP="$NIXPKGS#djgpp_i586"
NIX_CMAKE="$NIXPKGS#cmake"
NIX_NINJA="$NIXPKGS#ninja"
DOS_BUILD_JOBS=${CMAKE_BUILD_PARALLEL_LEVEL:-1}
SDL_REVISION=b669ac244e10fbab4734c7389151f7a4e8ac725f
SDL_SHA256=9171e2ecec13d0cbe90d49a5faf9dee56c387ff161f6467b42b39c381d0eb724
SDL_ARCHIVE="$ROOT/.dos-deps/downloads/SDL-$SDL_REVISION.tar.gz"
CWSDPMI_SHA256=deacda0488e1cdd7c4a9f32fab45662b34c0ed6b2d7d4d13bc07041b62004a8c
CWSDPMI_ARCHIVE="$ROOT/.dos-deps/downloads/csdpmi7b.zip"
SDL_SOURCE="$ROOT/.dos-deps/SDL"
BUILD_ROOT="$ROOT/.dos-build"
SDL_BUILD="$BUILD_ROOT/sdl"
SDL_PREFIX="$BUILD_ROOT/prefix"
APP_BUILD="$BUILD_ROOT/heretic2-dos"
GUEST_DIR="$BUILD_ROOT/guest"
TOOLCHAIN="$SDL_SOURCE/build-scripts/i586-pc-msdosdjgpp.cmake"
FIXTURE_SHA256_FILE="$ROOT/scripts/dos-fixture.sha256"

case "$DOS_BUILD_JOBS" in
	''|*[!0-9]*|0)
		echo "CMAKE_BUILD_PARALLEL_LEVEL must be a positive integer" >&2
		exit 2
		;;
esac

verify_archive() {
	archive=$1
	expected=$2
	[ -f "$archive" ] && [ "$(shasum -a 256 "$archive" | awk '{print $1}')" = "$expected" ]
}

fetch_archive() {
	archive=$1
	expected=$2
	url=$3
	if verify_archive "$archive" "$expected"; then
		return
	fi

	rm -f "$archive.tmp"
	curl --fail --location --retry 3 --output "$archive.tmp" "$url"
	actual_sha=$(shasum -a 256 "$archive.tmp" | awk '{print $1}')
	if [ "$actual_sha" != "$expected" ]; then
		rm -f "$archive.tmp"
		echo "Archive checksum mismatch: expected $expected, got $actual_sha" >&2
		exit 1
	fi
	mv "$archive.tmp" "$archive"
}

mkdir -p "$(dirname "$SDL_ARCHIVE")" "$BUILD_ROOT" "$GUEST_DIR"
"$ROOT/scripts/make-dos-test-pak.py" "$GUEST_DIR/H2TEST/base/Htic2-9.pak"
expected_fixture_sha=$(awk 'NF == 2 { print $1 }' "$FIXTURE_SHA256_FILE")
actual_fixture_sha=$(shasum -a 256 "$GUEST_DIR/H2TEST/base/Htic2-9.pak" | awk '{print $1}')
if [ -z "$expected_fixture_sha" ] || [ "$actual_fixture_sha" != "$expected_fixture_sha" ]; then
	echo "Synthetic fixture checksum mismatch: expected $expected_fixture_sha, got $actual_fixture_sha" >&2
	exit 1
fi
fetch_archive \
	"$SDL_ARCHIVE" \
	"$SDL_SHA256" \
	"https://github.com/libsdl-org/SDL/archive/$SDL_REVISION.tar.gz"
fetch_archive \
	"$CWSDPMI_ARCHIVE" \
	"$CWSDPMI_SHA256" \
	"https://www.delorie.com/pub/djgpp/current/v2misc/csdpmi7b.zip"

if [ ! -f "$SDL_SOURCE/.h2-dos-revision" ] || [ "$(cat "$SDL_SOURCE/.h2-dos-revision")" != "$SDL_REVISION" ]; then
	rm -rf "$SDL_SOURCE"
	mkdir -p "$SDL_SOURCE"
	tar -xzf "$SDL_ARCHIVE" --strip-components=1 -C "$SDL_SOURCE"
	printf '%s\n' "$SDL_REVISION" > "$SDL_SOURCE/.h2-dos-revision"
fi

nix shell "$NIX_DJGPP" "$NIX_CMAKE" "$NIX_NINJA" -c cmake \
	-S "$SDL_SOURCE" \
	-B "$SDL_BUILD" \
	-G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" \
	-DSDL_TESTS=OFF \
	-DSDL_EXAMPLES=OFF
nix shell "$NIX_DJGPP" "$NIX_CMAKE" "$NIX_NINJA" -c cmake --build "$SDL_BUILD" --parallel "$DOS_BUILD_JOBS"
nix shell "$NIX_DJGPP" "$NIX_CMAKE" "$NIX_NINJA" -c cmake --install "$SDL_BUILD"

nix shell "$NIX_DJGPP" "$NIX_CMAKE" "$NIX_NINJA" -c cmake \
	-S "$ROOT" \
	-B "$APP_BUILD" \
	-G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
	-DCMAKE_BUILD_TYPE=Release \
	-DSDL3_DIR="$SDL_PREFIX/lib/cmake/SDL3" \
	-DH2_DOS_OUTPUT_DIR="$GUEST_DIR"
nix shell "$NIX_DJGPP" "$NIX_CMAKE" "$NIX_NINJA" -c cmake --build "$APP_BUILD" --parallel "$DOS_BUILD_JOBS" --target HELLODOS H2DOS SILVER
unzip -p "$CWSDPMI_ARCHIVE" bin/CWSDPMI.EXE > "$GUEST_DIR/CWSDPMI.EXE"
unzip -p "$CWSDPMI_ARCHIVE" bin/cwsdpmi.doc > "$GUEST_DIR/CWSDPMI.DOC"

{
	printf 'artifacts=HELLODOS.EXE,H2DOS.EXE,SILVER.EXE\n'
	printf 'milestone=HERETIC2-DOS-PRODUCTION-SLICES-3\n'
	printf 'test-fixture=H2TEST/base/Htic2-9.pak (synthetic, non-game)\n'
	shasum -a 256 "$GUEST_DIR/H2TEST/base/Htic2-9.pak" | awk '{print "test-fixture-sha256=" $1}'
	printf 'nixpkgs-revision=%s\n' "$NIXPKGS_REVISION"
	printf 'build-parallel-jobs=%s\n' "$DOS_BUILD_JOBS"
	printf 'sdl-revision=%s\n' "$SDL_REVISION"
	printf 'sdl-archive-sha256=%s\n' "$SDL_SHA256"
	printf 'cwsdpmi-archive=csdpmi7b.zip\n'
	printf 'cwsdpmi-archive-sha256=%s\n' "$CWSDPMI_SHA256"
	nix shell "$NIX_DJGPP" -c i586-pc-msdosdjgpp-gcc --version | head -n 1 | sed 's/^/compiler=/'
} > "$GUEST_DIR/BUILD-INFO.TXT"

printf 'Built %s, %s, and %s\n' "$GUEST_DIR/HELLODOS.EXE" "$GUEST_DIR/H2DOS.EXE" "$GUEST_DIR/SILVER.EXE"
