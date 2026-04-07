# Playback

## 현재 상태

재생 코드는 이미 연결되어 있다. 현재 앱은 Borealis UI에서 재생 요청을 큐에 넣고, UI를 빠져나온 뒤 Switch 전용 SDL2/mpv 플레이어를 실행한다.

## 현재 재생 구조

1. 목록에서 `A`로 영상 선택
2. `PlaybackRequest` 생성
3. Borealis 종료
4. `SwitchPlayer` 실행
5. YouTube URL이면 `YouTubeResolver`로 playable stream 해석
6. mpv 재생
7. `B`로 종료 후 Borealis 재시작

## 입력

- 목록 화면
  - `A`: 바로 재생
  - `Y`: 상세 정보
- 플레이어 화면
  - `A`: 일시정지 / 재개
  - `B`: 종료
  - `위 / 아래`: 볼륨
  - `X / Y`: OSD 고정 표시 토글

현재 좌우 seek는 비활성화 상태다.

## 현재 해석 전략

### 1. 일반 direct URL

- 바로 재생
- 필요 시 캐시 브리지 사용

### 2. YouTube URL

- Android `youtubei/v1/player`로 기본 playable format 조회
- 720p adaptive가 있으면 iOS HLS manifest 확보 시도
- 현재 최신 코드 기준
  - 원본 720p HLS manifest direct 재생 우선
  - `hls-bitrate=max` 적용
  - 첫 프레임 전 실패 시 progressive stream으로 자동 fallback

### 3. Progressive stream

- `switchcache://` 커스텀 프로토콜 사용
- 백그라운드 다운로드 + mpv 읽기 브리지
- 긴 영상도 전체 다운로드 완료까지 기다리지 않고 재생 시작 가능

## 로딩 UI

재생 준비 중에는 검은 화면 대신 progress circle과 상태 문구를 표시한다.

예시 상태:

- `RESOLVING YOUTUBE STREAM`
- `REQUESTING 720P HLS STREAM`
- `OPENING MEDIA STREAM`
- `DOWNLOADING VIDEO DATA`
- `BUFFERING FIRST FRAME`

## 재생 OSD

재생이 시작된 뒤에는 입력 반응형 OSD를 영상 위에 직접 그린다.

- 자동 표시 시점
  - 첫 재생 시작 직후
  - `A` pause / resume
  - `위 / 아래` 볼륨 변경
- 표시 정보
  - 제목
  - 재생 상태
  - 현재 품질 라벨
  - 진행 바 / 경과 시간 / 총 길이
  - 볼륨 바
- `X / Y`를 누르면 OSD를 잠깐 띄우는 것이 아니라 고정 표시로 토글된다

## 로그

- Switch: `sdmc:/switch/switch_newpipe.log`
- FTP 예시: `ftp://192.168.1.16:5000/switch/switch_newpipe.log`

## 현재 제한

- seek 비활성화
- 화질 수동 선택 UI 없음
- 720p direct HLS 경로는 최신 코드 기준 실기 재검증이 필요
- 라이브/장시간 스트림의 예외 처리 강화가 더 필요
- 플레이어 OSD는 들어갔지만 실기 UI 튜닝은 아직 필요하다
