#!/bin/bash

set -euo pipefail

BUILD_DIR="cmake-build-switch"
LIBROMFS_GENERATOR="reference/switchzzk/library/borealis/libromfs-generator"
LIBROMFS_BUILD_DIR=".build-libromfs-generator"
PORTLIBS_VOLUME="switch-newpipe-portlibs"
PORTLIBS_DIR="${PORTLIBS_DIR:-}"
DOCKER_IMAGE="${DOCKER_IMAGE:-devkitpro/devkita64}"
APP_ONLY=0

DOCKER_PORTLIBS_ARGS=()
if [ -n "$PORTLIBS_DIR" ]; then
    mkdir -p "$PORTLIBS_DIR"
    DOCKER_PORTLIBS_ARGS=(-v "$PORTLIBS_DIR:/opt/devkitpro/portlibs/switch")
else
    DOCKER_PORTLIBS_ARGS=(-v "$PORTLIBS_VOLUME:/opt/devkitpro/portlibs/switch")
fi

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

if [ ! -x "$LIBROMFS_GENERATOR" ]; then
    cmake -S reference/switchzzk/library/borealis/library/lib/extern/libromfs/generator \
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
        ./reference/switchzzk/scripts/switch/build_custom_portlibs.sh --skip-mpv --skip-app
        MPV_PC=/opt/devkitpro/portlibs/switch/lib/pkgconfig/mpv.pc
        if [ -f "$MPV_PC" ] && ! grep -q "mbedtls" "$MPV_PC"; then
          sed -i "s#^Libs\\.private: .*#& -lmbedtls -lmbedx509 -lmbedcrypto#" "$MPV_PC"
        fi
      '
fi

docker run --rm \
  --user "$(id -u):$(id -g)" \
  "${DOCKER_PORTLIBS_ARGS[@]}" \
  -v "$PWD:/work" \
  -w /work \
  "$DOCKER_IMAGE" \
  bash -lc '
    source /opt/devkitpro/switchvars.sh
    export PATH=/opt/devkitpro/devkitA64/bin:$PATH
    cmake -B '"$BUILD_DIR"' \
      -DCMAKE_BUILD_TYPE=Release \
      -DPLATFORM_SWITCH=ON \
      -DUSE_DEKO3D=OFF
    cmake --build '"$BUILD_DIR"' -j$(nproc) --target switch_newpipe.nro
  '
