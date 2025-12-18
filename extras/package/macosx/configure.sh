#!/bin/sh

SCRIPTDIR=$(dirname "$0")
. "$SCRIPTDIR/env.build.sh" "none"

# Get contrib directory path
VLC_ROOT_DIR="$(cd "$(dirname "$0")/../../.." && pwd)"
# HOST_TRIPLET should be set by the build script, but compute it if not
if [ -z "$HOST_TRIPLET" ]; then
    HOST_TRIPLET=$(vlcGetHostTriplet)
fi
# Convert aarch64 to arm64 for directory name
CONTRIB_TRIPLET=$(echo "$HOST_TRIPLET" | sed 's/aarch64/arm64/')
CONTRIB_DIR="${VLC_ROOT_DIR}/contrib/${CONTRIB_TRIPLET}"

# Set PKG_CONFIG_PATH to find contrib packages
export PKG_CONFIG_PATH="${CONTRIB_DIR}/lib/pkgconfig:${PKG_CONFIG_PATH}"
export PKG_CONFIG_LIBDIR="${CONTRIB_DIR}/lib/pkgconfig"

CFLAGS="${CFLAGS} -I${CONTRIB_DIR}/include"
LDFLAGS="${LDFLAGS} -L${CONTRIB_DIR}/lib"

OPTIONS="
        --prefix=/
        --enable-macosx
        --enable-merge-ffmpeg
        --enable-osx-notifications
        --enable-flac
        --enable-theora
        --enable-shout
        --enable-ncurses
        --enable-twolame
        --enable-libass
        --enable-macosx-avfoundation
        --disable-skins2
        --disable-xcb
        --disable-caca
        --disable-pulse
        --disable-vnc
        --disable-lua
        --disable-macosx-ui-shaders
        --disable-sparkle
        --disable-opus
        --disable-archive
        --with-macosx-version-min=10.11
        --with-contrib=${CONTRIB_DIR}
        --without-x
"

export CFLAGS
export LDFLAGS

vlcSetSymbolEnvironment

sh "$(dirname $0)"/../../../configure ${OPTIONS} "$@"
