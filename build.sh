#!/bin/bash

set -euo pipefail

BUILD_DIR="cmake-build-switch"
BOREALIS_VENDOR_DIR="vendor/borealis"
SWITCH_PORTLIBS_DIR="vendor/switch-portlibs"
LIBROMFS_GENERATOR="${BOREALIS_VENDOR_DIR}/libromfs-generator"
LIBROMFS_BUILD_DIR=".build-libromfs-generator"
PORTLIBS_VOLUME="switch-newpipe-portlibs"
PORTLIBS_DIR="${PORTLIBS_DIR:-}"
DOCKER_IMAGE="${DOCKER_IMAGE:-devkitpro/devkita64}"
APP_ONLY=0

DOCKER_PORTLIBS_ARGS=()
if [ -n "$PORTLIBS_DIR" ]; then
    mkdir -p "$PORTLIBS_DIR"
    DOCKER_PORTLIBS_ARGS=(-v "$PORTLIBS_DIR:/portlibs-cache")
else
    DOCKER_PORTLIBS_ARGS=(-v "$PORTLIBS_VOLUME:/portlibs-cache")
fi

if [ ! -d "$BOREALIS_VENDOR_DIR" ] || [ ! -d "$SWITCH_PORTLIBS_DIR" ]; then
    echo "Missing vendored build dependencies."
    echo "Expected directories:"
    echo "  - $BOREALIS_VENDOR_DIR"
    echo "  - $SWITCH_PORTLIBS_DIR"
    exit 1
fi

ensure_cmake_build_dir_matches_source() {
    local build_dir="$1"
    local source_dir="$2"
    local cache_file="$build_dir/CMakeCache.txt"

    if [ ! -d "$build_dir" ]; then
        return
    fi

    if [ ! -d "$source_dir" ]; then
        echo "Missing CMake source directory: $source_dir"
        exit 1
    fi

    if grep -Rqs "reference/switchzzk" "$build_dir"; then
        rm -rf "$build_dir"
        return
    fi

    if [ ! -f "$cache_file" ]; then
        return
    fi

    local configured_source
    configured_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache_file" | head -n1)"
    if [ -n "$configured_source" ] && [ "$configured_source" != "$source_dir" ]; then
        rm -rf "$build_dir"
    fi
}

for arg in "$@"; do
    case "$arg" in
        --clean)
            if [ -n "$PORTLIBS_DIR" ]; then
                rm -rf "$PORTLIBS_DIR"
            else
                docker volume rm -f "$PORTLIBS_VOLUME" >/dev/null 2>&1 || true
            fi
            docker run --rm \
              -v "$PWD:/work" \
              -w /work \
              "$DOCKER_IMAGE" \
              bash -lc "rm -rf '$BUILD_DIR'"
            ;;
        --app-only)
            APP_ONLY=1
            ;;
        *)
            ;;
    esac
done

ensure_cmake_build_dir_matches_source \
  "$LIBROMFS_BUILD_DIR" \
  "$PWD/${BOREALIS_VENDOR_DIR}/library/lib/extern/libromfs/generator"

if [ ! -x "$LIBROMFS_GENERATOR" ]; then
    rm -rf "$LIBROMFS_BUILD_DIR"
    cmake -S "${BOREALIS_VENDOR_DIR}/library/lib/extern/libromfs/generator" \
      -B "$LIBROMFS_BUILD_DIR"
    cmake --build "$LIBROMFS_BUILD_DIR" -j"$(nproc)"
    cp "$LIBROMFS_BUILD_DIR/libromfs-generator" "$LIBROMFS_GENERATOR"
fi

if [ "$APP_ONLY" -eq 0 ]; then
    docker run --rm \
      "${DOCKER_PORTLIBS_ARGS[@]}" \
      -v "$PWD:/work" \
      -w /work \
      "$DOCKER_IMAGE" \
      bash -lc '
        set -euo pipefail
        if [ -n "$(find /portlibs-cache -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
          cp -a /portlibs-cache/. /opt/devkitpro/portlibs/switch/
        fi
        ./vendor/switch-portlibs/build_custom_portlibs.sh --skip-mpv --skip-app
        MPV_PC=/opt/devkitpro/portlibs/switch/lib/pkgconfig/mpv.pc
        if [ -f "$MPV_PC" ] && ! grep -q "mbedtls" "$MPV_PC"; then
          sed -i "s#^Libs\\.private: .*#& -lmbedtls -lmbedx509 -lmbedcrypto#" "$MPV_PC"
        fi
        find /portlibs-cache -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true
        cp -a /opt/devkitpro/portlibs/switch/. /portlibs-cache/
      '
fi

ensure_cmake_build_dir_matches_source "$BUILD_DIR" "$PWD"

docker run --rm \
  "${DOCKER_PORTLIBS_ARGS[@]}" \
  -v "$PWD:/work" \
  -w /work \
  "$DOCKER_IMAGE" \
  bash -lc '
    set -euo pipefail
    if [ -n "$(find /portlibs-cache -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
      cp -a /portlibs-cache/. /opt/devkitpro/portlibs/switch/
    fi
    source /opt/devkitpro/switchvars.sh
    export PATH=/opt/devkitpro/devkitA64/bin:$PATH
    cmake -B '"$BUILD_DIR"' \
      -DCMAKE_BUILD_TYPE=Release \
      -DPLATFORM_SWITCH=ON \
      -DUSE_DEKO3D=OFF
    cmake --build '"$BUILD_DIR"' -j$(nproc) --target switch_newpipe.nro
    chown -R '"$(id -u):$(id -g)"' /work/'"$BUILD_DIR"'
  '
