#!/bin/sh
# Build the pinned SDL3_mixer release against the installed SDL3 and system
# music decoder libraries.
set -eu
prefix=${1:?usage: build-sdl3-mixer.sh absolute-prefix absolute-build-directory}
build_directory=${2:?usage: build-sdl3-mixer.sh absolute-prefix absolute-build-directory}
case "$prefix:$build_directory" in
    /*:/*) ;;
    *) printf '%s\n' 'Both paths must be absolute.' >&2; exit 1 ;;
esac
mkdir -p "$build_directory"
archive="$build_directory/SDL3_mixer-3.2.4.tar.gz"
curl --fail --silent --show-error --location --retry 3 \
    https://github.com/libsdl-org/SDL_mixer/releases/download/release-3.2.4/SDL3_mixer-3.2.4.tar.gz \
    --output "$archive"
printf '%s  %s\n' \
    182a07c745375e113dc740d43964ff21b0be29f29f59876c4dbc4db3d32f6901 "$archive" | sha256sum --check
tar -xf "$archive" -C "$build_directory"
cmake -S "$build_directory/SDL3_mixer-3.2.4" -B "$build_directory/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib -DSDLMIXER_VENDORED=OFF \
    -DSDLMIXER_EXAMPLES=OFF -DSDLMIXER_TESTS=OFF
cmake --build "$build_directory/build" --parallel
cmake --install "$build_directory/build"
