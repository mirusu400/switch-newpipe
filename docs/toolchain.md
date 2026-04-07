# Toolchain

## 현재 확인된 환경

- `DEVKITPRO=/opt/devkitpro`
- `/opt/devkitpro/devkitA64` 존재
- Docker 사용 가능
- 최종 Switch 빌드는 Docker 경로를 기준으로 유지

## 왜 Docker를 우선 쓰는가

- 호스트의 `portlibs` 상태가 일정하지 않을 수 있다
- mpv / ffmpeg / SDL2 같은 의존성이 Switch 쪽에서 민감하다
- 지금 프로젝트는 `devkitpro/devkita64` Docker 경로에서 빌드하는 쪽이 가장 재현성이 높다

## Switch 빌드

```bash
./build.sh
```

결과물:

- `cmake-build-switch/switch_newpipe.nro`

## 호스트 검증

```bash
make host
./build/host/switch_newpipe_host
./build/host/switch_newpipe_host --search Zelda
./build/host/switch_newpipe_host --resolve 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'
```

호스트 경로는 UI 없이 공용 파서/해석 로직을 빠르게 확인하는 용도다.

## 실기 배포 / 로그 회수

예시 FTP 서버가 열려 있으면:

```bash
curl -T cmake-build-switch/switch_newpipe.nro ftp://192.168.1.16:5000/switch/switch_newpipe.nro
curl --silent ftp://192.168.1.16:5000/switch/switch_newpipe.log
```

## 주의점

- `mpv log-file`에 `sdmc:/` 경로를 직접 주는 방식은 쓰지 않는다
- 앱 로그는 자체 `log.cpp` 경로로 남긴다
- Borealis와 SDL 플레이어는 동시에 유지하지 않고, UI 종료 -> 플레이어 -> UI 재시작 루프를 쓴다
- 재생 이슈는 호스트보다 실기 로그 기준으로 판단해야 한다
