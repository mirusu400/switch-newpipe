# Architecture

## 목표 범위

- 스위치 홈브류에서 실제로 돌아가는 YouTube MVP 유지
- Android NewPipe 전체 복제보다, 실기 안정성과 입력 흐름 단순화를 우선
- 공용 로직은 호스트에서 빠르게 검증하고, 최종 기준은 항상 Switch 실기

## 현재 레이어

### 1. Switch UI

위치: `src/`, `include/`, `resources/`

- Borealis 기반 앱 프레임
- 좌측 사이드바 + 탭 구조
- `Home`, `Search`, `Subscriptions`, `Library`
- 카드 그리드와 상세 다이얼로그
- 목록 입력
  - `A`: 바로 재생
  - `Y`: 상세

### 2. 공용 데이터 계층

위치: `src/common/`, `include/newpipe/`

- `CatalogService`
- `FixtureCatalogService`
- `YouTubeCatalogService`
- `AuthStore`
- `LibraryStore`
- `StreamItem`, `StreamDetail`, `HomeFeed`, `SearchResults`

현재 기본 경로는 `YouTubeCatalogService`다.

- 홈은 `추천 / 라이브 / 음악 / 게임` 프리셋 검색으로 구성된다
- 검색과 홈 카드 모두 실제 YouTube 응답에서 파싱한다
- 구독 탭은 저장된 로그인 세션이 있으면 `FEsubscriptions` browse feed를 사용한다
- 상세는 `youtubei/v1/player`로 설명/채널 ID를 보강한다
- 채널 목록은 YouTube RSS feed를 사용한다
- 연관 추천은 현재 제목+채널 기반 검색 fallback이다

### 3. 네트워크 / 해석 계층

위치: `src/common/http_client.cpp`, `src/common/auth_store.cpp`, `src/common/youtube_resolver.cpp`

- `HttpClient`
- `AuthStore`
- `YouTubeResolver`
- `LibraryStore`

현재 인증 흐름:

1. 외부에서 가져온 쿠키를 `switch_newpipe_auth.txt`로 import
2. `switch_newpipe_session.json`에 정규화 저장
3. `SAPISID` 또는 `__Secure-3PAPISID`로 `SAPISIDHASH` 생성
4. 인증된 `youtubei/v1/browse` 호출에 `Cookie` + `Authorization` 헤더 부착

현재 재생 URL 해석 흐름:

1. Android `youtubei/v1/player`로 기본 playable format 조회
2. 720p adaptive가 있으면 iOS player API로 HLS manifest 확보 시도
3. 720p HLS direct 재생 우선
4. 실패 시 progressive 스트림 fallback 메타 유지

### 4. 썸네일 / 로그 / 런타임 보조

위치: `src/common/image_loader.cpp`, `src/common/log.cpp`, `src/common/runtime.cpp`

- 비동기 썸네일 로더
- `sdmc:/switch/switch_newpipe.log` 파일 로그
- UI -> 플레이어 재생 요청 큐
- 재생 실패 메시지 전달

### 5. Switch 전용 재생 계층

위치: `src/switch/switch_player.cpp`

- SDL2 윈도우
- OpenGL ES 기반 `libmpv` 렌더링
- 로딩 화면과 상태 문구
- 입력 처리
  - `A`: pause / resume
  - `B`: exit
  - `위 / 아래`: volume
- progressive 스트림용 캐시 브리지

현재 seek는 꺼져 있다.

### 6. 공용 호스트 검증

위치: `src/host/`, `Makefile`

- `make host`
- 검색 결과 확인
- resolver 출력 확인

## 현재 실행 흐름

```text
main()
  -> Borealis init
  -> MainActivity push
  -> Home/Search UI render
  -> 카드에서 재생 요청 큐 등록
  -> Borealis 종료
  -> SwitchPlayer 실행
  -> YouTubeResolver로 URL 해석
  -> mpv 재생
  -> B 종료 시 UI 재시작
```

## 현재 데이터 흐름

```text
Home/Search tab
  -> YouTubeCatalogService
  -> youtubei/v1/search
  -> StreamItem 목록
  -> StreamCard + ImageLoader

Subscriptions tab
  -> AuthStore
  -> YouTubeCatalogService
  -> youtubei/v1/browse (FEsubscriptions)
  -> StreamItem 목록
  -> StreamCard + ImageLoader
```

## 현재 재생 흐름

```text
PlaybackRequest
  -> YouTubeResolver
  -> 720p HLS direct 또는 progressive URL
  -> SwitchPlayer
  -> 필요 시 switchcache 브리지
  -> mpv render loop

PlaybackRequest enqueue
  -> LibraryStore history update
  -> Borealis 종료
  -> SwitchPlayer
```

## 아직 비어 있는 부분

- 채널 / 관련 영상 / 재생목록 계층
- 영구 저장소
- 플레이어 OSD / 화질 선택 / seek
- 개인화 추천 홈
- 앱 내부 OAuth / WebView 로그인
- 댓글 / 재생목록 / 다운로드
