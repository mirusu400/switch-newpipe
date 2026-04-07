# Switch-NewPipe

![preview2](./docs/preview2.jpg)
![preview1](./docs/preview1.jpg)

Switch-NewPipe는 NewPipe 스타일을 참고한 닌텐도 스위치 CFW 홈브류 YouTube 앱이다.  
이 프로젝트의 목표는 Android NewPipe를 그대로 포팅하는 것이 아니라, 스위치에서 실제로 안정적으로 동작하는 네이티브 MVP를 먼저 만드는 것이다.

[English README](./README.md)

## 현재 상태

Switch-NewPipe는 단순한 UI 스캐폴드 단계를 넘겼다. 현재 Borealis 기반 스위치 UI, 실제 YouTube 데이터 조회, 실기 재생, 로그 저장, 설정 persistence, 쿠키 import 기반 로그인 흐름까지 연결되어 있다.

현재 구현된 것:

- `홈`, `검색`, `구독`, `라이브러리`, `설정` 탭을 가진 Borealis 네이티브 UI
- `YouTubeCatalogService` 기반 실제 YouTube 홈 / 검색 피드
- 로그인 세션이 있을 때 `FEwhat_to_watch` 기반 개인화 홈 추천 우선 시도
- 쿠키 import 기반 로그인 세션 저장
- 인증된 `FEsubscriptions` browse 요청 기반 구독 피드
- 설명 / 채널 / 연관 영상 / 재생목록 / 댓글로 들어갈 수 있는 상세 화면
- `watch-next` 기반 연관 영상 파서
- `youtubei/v1/browse` 기반 재생목록 파서
- watch 페이지 continuation + `youtubei/v1/next` 기반 댓글 1페이지 파서
- 최근 시청 / 즐겨찾기 저장
- 시작 탭, 기본 홈 카테고리, 재생 정책, 짧은 영상 필터, 언어 저장
- 비동기 썸네일 로딩
- SDL2 + OpenGL ES + mpv 기반 스위치 전용 재생 루프
- 로딩 스피너와 상태 문구
- 제목 / 상태 / 화질 / 진행 바 / 볼륨을 보여주는 플레이어 OSD
- `sdmc:/switch/switch_newpipe.log` 로그 저장
- `make host` 기반 호스트 검증 경로
- 영어 / 한국어 i18n 및 앱 내 언어 설정

아직 없거나 미완성인 것:

- 다운로드
- seek
- 플레이어 내 수동 화질 선택 UI
- 앱 내부 Google OAuth / WebView 로그인
- browse 기반 채널 홈 / 탭 구조
- 댓글 / 재생목록 전체 pagination

## 기능

### 홈 / 검색

- placeholder fixture가 아니라 실제 YouTube 데이터 사용
- `추천`, `라이브`, `음악`, `게임` 홈 카테고리 제공
- 실제 YouTube 응답 기반 검색
- 목록 화면에서 `A`로 즉시 재생
- `Y`로 상세 화면 진입

### 로그인 / 구독

현재 로그인은 앱 내부 OAuth가 아니라 외부에서 가져온 YouTube / Google 쿠키를 import하는 방식이다.

- 기본 인증 import 파일:
  - `sdmc:/switch/switch_newpipe_auth.txt`
- 지원 형식:
  - raw `Cookie` header
  - JSON `{"cookie_header":"..."}`
  - Netscape `cookies.txt`
- 정규화된 저장 세션:
  - `sdmc:/switch/switch_newpipe_session.json`
- `구독` 탭에서 `RB`로 세션 관리
- `X`로 구독 피드 새로고침
- 로그인 세션이 있으면 홈 `추천`은 개인화 피드를 먼저 시도

### 라이브러리

- 저장 파일:
  - `sdmc:/switch/switch_newpipe_library.json`
- 현재 지원:
  - 최근 시청
  - 즐겨찾기
  - 라이브러리 탭 내 섹션 전환
  - 현재 섹션 비우기

### 설정

- 저장 파일:
  - `sdmc:/switch/switch_newpipe_settings.json`
- 현재 지원:
  - 시작 탭
  - 기본 홈 카테고리
  - 재생 품질 정책
  - 짧은 영상 숨기기
  - 앱 언어
- 지원 언어:
  - 시스템 기본값
  - 한국어
  - 영어
- 설정 탭에서 `X`로 기본값 복원 가능

### 재생

플레이어 입력:

- `A`: 일시정지 / 재개
- `B`: 플레이어 종료
- `위 / 아래`: 볼륨
- `X / Y`: OSD 고정 표시 토글

재생 정책:

- `표준 720p`
  - 가능하면 720p 경로 우선
  - 필요 시 fallback
- `호환성 우선`
  - progressive MP4 경로 우선
- `데이터 절약`
  - 480p 부근의 낮은 progressive 포맷 우선

현재 제한:

- seek는 아직 비활성화 상태

## 로그 / 저장 파일

- 로그:
  - `sdmc:/switch/switch_newpipe.log`
- 설정:
  - `sdmc:/switch/switch_newpipe_settings.json`
- 라이브러리:
  - `sdmc:/switch/switch_newpipe_library.json`
- 인증 import 파일:
  - `sdmc:/switch/switch_newpipe_auth.txt`
- 저장된 로그인 세션:
  - `sdmc:/switch/switch_newpipe_session.json`

## 레퍼런스

- `reference/NewPipe`
  - Android NewPipe 원본 레퍼런스
- `reference/` 아래의 무시된 Switch 재생 레퍼런스 앱
  - Borealis + mpv + ffmpeg 재생 구조 참고용
- `reference/wiliwili`
  - Switch / 멀티플랫폼 UI 및 재생 구조 레퍼런스

무시된 `reference/` 트리는 탐색/참고용이다.  
실제 빌드 의존성은 `vendor/borealis`, `vendor/lunasvg`, `vendor/third_party`, `vendor/switch-portlibs`에 직접 vendoring 되어 있다.

## 빌드

Switch 빌드:

```bash
./build.sh
```

호스트 검증:

```bash
make host
./build/host/switch_newpipe_host
./build/host/switch_newpipe_host --search Zelda
./build/host/switch_newpipe_host --subscriptions --auth-file ./switch_newpipe_auth.txt
./build/host/switch_newpipe_host --related 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'
./build/host/switch_newpipe_host --channel 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'
./build/host/switch_newpipe_host --playlist 'https://www.youtube.com/watch?v=ZZcuSBouhVA&list=PL8F6B0753B2CCA128'
./build/host/switch_newpipe_host --comments 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'
./build/host/switch_newpipe_host --resolve 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'
```

## 문서

- 구조:
  - `docs/architecture.md`
- 재생 메모:
  - `docs/playback.md`
- 툴체인 / 빌드:
  - `docs/toolchain.md`
- 현재 handoff:
  - `docs/handoff.md`
- 실기 테스트 체크리스트:
  - `docs/testing.md`
- 로드맵:
  - `docs/roadmap.md`
