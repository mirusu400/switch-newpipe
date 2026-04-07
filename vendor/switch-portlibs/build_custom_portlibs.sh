#!/bin/bash
# build_custom_portlibs.sh
# 커스텀 Switch ffmpeg / libmpv 빌드 스크립트
# Docker 컨테이너 내에서 실행됨 (devkitpro/devkita64)
#
# 사용법 (프로젝트 루트에서):
#   sg docker -c 'docker run --rm -v "$PWD:/work" -w /work devkitpro/devkita64 \
#     bash -lc "./scripts/switch/build_custom_portlibs.sh"'
#
# 옵션:
#   --with-nvtegra   ffmpeg.patch(하드웨어 가속)도 적용
#   --skip-ffmpeg    ffmpeg 빌드 건너뛰기
#   --skip-mpv       mpv 빌드 건너뛰기
#   --skip-app       앱(.nro) 빌드 건너뛰기

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK_DIR="/tmp/custom-portlibs-build"
FFMPEG_COMMIT="e1094ac45d3bc7942043e72a23b6ab30faaddb8a"
FFMPEG_VER="7.1"
MPV_VER="0.36.0"

WITH_NVTEGRA=0
SKIP_FFMPEG=0
SKIP_MPV=0
SKIP_APP=0

for arg in "$@"; do
  case "$arg" in
    --with-nvtegra) WITH_NVTEGRA=1 ;;
    --skip-ffmpeg)  SKIP_FFMPEG=1 ;;
    --skip-mpv)     SKIP_MPV=1 ;;
    --skip-app)     SKIP_APP=1 ;;
    *) echo "Unknown option: $arg"; exit 1 ;;
  esac
done

# devkitPro 환경 로드
source /opt/devkitpro/switchvars.sh
export PATH="/opt/devkitpro/devkitA64/bin:$PATH"

echo "=== Custom portlibs build ==="
echo "  DEVKITPRO=$DEVKITPRO"
echo "  PORTLIBS_PREFIX=$PORTLIBS_PREFIX"
echo "  WITH_NVTEGRA=$WITH_NVTEGRA"

install_required_packages() {
  dkp-pacman -S --noconfirm --needed "$@"
}

install_optional_package() {
  local package="$1"
  if ! dkp-pacman -S --noconfirm --needed "$package" 2>/dev/null; then
    echo "WARN: $package not available, continuing without"
  fi
}

# ─── 의존성 설치 ───
echo ""
echo "=== Installing build dependencies ==="
dkp-pacman -Syu --noconfirm 2>/dev/null || true
install_required_packages \
  switch-pkg-config dkp-toolchain-vars \
  switch-zlib switch-bzip2 switch-mbedtls \
  switch-mesa switch-sdl2 switch-curl

install_optional_package switch-freetype
install_optional_package switch-harfbuzz
install_optional_package switch-libfribidi
install_optional_package switch-libass
install_optional_package switch-dav1d
install_optional_package switch-liblua51

if ! command -v aarch64-none-elf-pkg-config >/dev/null 2>&1; then
  echo "ERROR: switch-pkg-config did not install aarch64-none-elf-pkg-config"
  exit 1
fi

# meson 빌드에 필요
install_optional_package dkp-meson-scripts
if ! command -v meson &>/dev/null; then
  echo "Installing meson via apt..."
  apt-get update -qq 2>/dev/null
  apt-get install -y -qq meson 2>/dev/null
fi

mkdir -p "$WORK_DIR"

# ─── FFmpeg 빌드 ───
# 이미 커스텀 ffmpeg가 설치되어 있으면 건너뛰기
FFMPEG_MARKER="$PORTLIBS_PREFIX/lib/.custom_ffmpeg_installed"
if [ "$SKIP_FFMPEG" -eq 0 ] && [ -f "$FFMPEG_MARKER" ]; then
  echo "=== Custom ffmpeg already installed (cached), skipping ==="
  SKIP_FFMPEG=1
fi

if [ "$SKIP_FFMPEG" -eq 0 ]; then
  echo ""
  echo "=== Building custom ffmpeg ==="

  cd "$WORK_DIR"
  FFMPEG_TAR="ffmpeg-${FFMPEG_COMMIT}.tar.gz"
  FFMPEG_SRC="FFmpeg-${FFMPEG_COMMIT}"

  if [ ! -d "$FFMPEG_SRC" ]; then
    if [ ! -f "$FFMPEG_TAR" ]; then
      echo "Downloading ffmpeg ${FFMPEG_VER} (commit ${FFMPEG_COMMIT:0:10})..."
      curl -L -o "$FFMPEG_TAR" \
        "https://github.com/FFmpeg/FFmpeg/archive/${FFMPEG_COMMIT}.tar.gz"
    fi
    tar xf "$FFMPEG_TAR"
  fi

  cd "$FFMPEG_SRC"

  # ffmpeg.patch — horizon OS 지원 + nvtegra 하드웨어 가속 (항상 적용, configure에 horizon 정의 필요)
  echo "Applying ffmpeg patch (horizon OS + nvtegra)..."
  rm -f libavutil/hwcontext_nvtegra.c libavutil/hwcontext_nvtegra.h
  rm -f libavutil/nvdec_drv.h libavutil/nvhost_ioctl.h libavutil/nvjpg_drv.h
  rm -f libavutil/nvmap_ioctl.h libavutil/nvtegra.c libavutil/nvtegra.h
  rm -f libavutil/nvtegra_host1x.h libavutil/vic_drv.h
  rm -f libavutil/clb0b6.h libavutil/clc5b0.h libavutil/cle7d0.h
  rm -f libavcodec/nvtegra_*
  patch -Np1 --forward -i "$SCRIPT_DIR/ffmpeg/ffmpeg.patch" || true

  # network.patch (DNS 수정) — 항상 적용
  echo "Applying network patch (DNS fix for Switch)..."
  patch -Np1 --forward -i "$SCRIPT_DIR/ffmpeg/network.patch" || true

  # nvtegra 플래그 결정 (ffmpeg.patch가 항상 적용되므로 기본 활성화)
  NVTEGRA_FLAGS="--enable-nvtegra"
  if [ "$WITH_NVTEGRA" -eq 0 ]; then
    NVTEGRA_FLAGS=""
    echo "  nvtegra: disabled (--with-nvtegra 미지정)"
  else
    echo "  nvtegra: enabled"
  fi

  # dav1d 사용 가능 여부 확인
  DAV1D_FLAGS=""
  if pkg-config --exists dav1d 2>/dev/null; then
    DAV1D_FLAGS="--enable-libdav1d"
    echo "  dav1d: enabled"
  else
    echo "  dav1d: disabled (not found)"
  fi

  SUBTITLE_FLAGS=""
  if pkg-config --exists libass freetype2 fribidi 2>/dev/null; then
    SUBTITLE_FLAGS="--enable-libass --enable-libfreetype --enable-libfribidi"
    echo "  subtitles: enabled (libass/freetype/fribidi)"
  else
    echo "  subtitles: disabled (missing libass/freetype/fribidi)"
  fi

  echo "Configuring ffmpeg..."
  ./configure \
    --prefix="$PORTLIBS_PREFIX" \
    --enable-gpl --disable-shared --enable-static \
    --cross-prefix=aarch64-none-elf- --enable-cross-compile \
    --arch=aarch64 --cpu=cortex-a57 --target-os=horizon --enable-pic \
    --extra-cflags="-D__SWITCH__ -D_GNU_SOURCE -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec" \
    --extra-cxxflags="-D__SWITCH__ -D_GNU_SOURCE -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec" \
    --extra-ldflags="-fPIE -L${PORTLIBS_PREFIX}/lib -L${DEVKITPRO}/libnx/lib" \
    --disable-runtime-cpudetect --disable-programs --disable-debug --disable-doc --disable-autodetect \
    --enable-asm --enable-neon \
    --disable-postproc --disable-avdevice --disable-encoders --disable-muxers \
    --enable-swscale --enable-swresample --enable-network \
    --disable-protocols \
    --enable-protocol=file,http,tcp,udp,rtmp,hls,https,tls,ftp,rtp,crypto,httpproxy \
    --enable-zlib --enable-bzlib \
    $SUBTITLE_FLAGS \
    $DAV1D_FLAGS \
    $NVTEGRA_FLAGS \
    --enable-version3 --enable-mbedtls

  echo "Building ffmpeg (this takes a while)..."
  make -j$(nproc)

  echo "Installing ffmpeg to $PORTLIBS_PREFIX..."
  make install
  rm -rf "${PORTLIBS_PREFIX}/share/ffmpeg" 2>/dev/null || true

  touch "$FFMPEG_MARKER"
  echo "=== ffmpeg build complete ==="
fi

# ─── mpv 빌드 ───
if [ "$SKIP_MPV" -eq 0 ]; then
  echo ""
  echo "=== Building custom mpv ==="

  cd "$WORK_DIR"
  MPV_TAR="mpv-${MPV_VER}.tar.gz"
  MPV_SRC="mpv-${MPV_VER}"

  if [ ! -d "$MPV_SRC" ]; then
    if [ ! -f "$MPV_TAR" ]; then
      echo "Downloading mpv ${MPV_VER}..."
      curl -L -o "$MPV_TAR" \
        "https://github.com/mpv-player/mpv/archive/v${MPV_VER}.tar.gz"
    fi
    tar xf "$MPV_TAR"
  fi

  cd "$MPV_SRC"
  rm -rf build

  # mpv.patch 적용 — aos_hos, mman shim, VIC 정렬 등
  echo "Applying mpv patch (Horizon OS audio, VIC alignment, mmap shim)..."
  rm -f audio/out/ao_hos.c
  rm -rf osdep/switch/
  patch -Np1 --forward -i "$SCRIPT_DIR/mpv/mpv.patch" || true

  source /opt/devkitpro/switchvars.sh

  echo "Configuring mpv with meson..."

  # lua51 사용 가능 여부
  LUA_OPT="disabled"
  if pkg-config --exists lua51 2>/dev/null || pkg-config --exists lua5.1 2>/dev/null; then
    LUA_OPT="enabled"
    echo "  lua: enabled"
  else
    echo "  lua: disabled (not found)"
  fi

  /opt/devkitpro/meson-cross.sh switch crossfile.txt build \
    -Dlua=$LUA_OPT \
    -Dhos=enabled \
    -Dhos-audio=enabled \
    -Diconv=disabled \
    -Djpeg=disabled \
    -Dlibavdevice=disabled \
    -Dmanpage-build=disabled \
    -Dsdl2=disabled \
    -Dlibmpv=true \
    -Dcplayer=false

  meson compile -C build

  echo "Installing mpv to $PORTLIBS_PREFIX..."
  DESTDIR="" meson install -C build

  echo "=== mpv build complete ==="
fi

# ─── 앱 빌드 ───
if [ "$SKIP_APP" -eq 0 ]; then
  echo ""
  echo "=== Building switch_newpipe.nro ==="
  cd /work
  make -f Makefile.switch clean 2>/dev/null || true
  make -f Makefile.switch
  echo "=== App build complete ==="
  ls -la switch_newpipe.nro
fi

echo ""
echo "=== All done ==="
