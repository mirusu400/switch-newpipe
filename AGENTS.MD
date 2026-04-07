# AGENTS.MD

## 목적

**Switch-NewPipe** — 닌텐도 스위치 CFW 홈브류로 구동되는 YouTube 앱. 기준 레퍼런스는 `reference/NewPipe`이며, 최우선 목표는 `실제 스위치 실기에서 안정적으로 굴러가는 MVP`다.

## 현재 상태

- **UI**: Borealis 기반 Switch 네이티브 UI, 홈/검색/구독/라이브러리/설정 탭
- **데이터**: `YouTubeCatalogService`로 실제 YouTube 홈/검색/구독 목록 조회
- **홈 추천**: 로그인 세션이 있으면 `FEwhat_to_watch` 기반 개인화 추천 우선 시도, 실패 시 익명 피드 fallback
- **연관/재생목록/댓글**: `watch-next`, `browse`, `next` 기반 파서 연결
- **인증**: `AuthStore`로 쿠키 import + 세션 저장 + `SAPISIDHASH`
- **라이브러리**: `LibraryStore`로 최근 시청 / 즐겨찾기 저장
- **설정**: `SettingsStore`로 시작 탭 / 기본 홈 카테고리 / 재생 품질 / 짧은 영상 필터 저장
- **재생**: Borealis 종료 -> SDL2/OpenGL ES/mpv 플레이어 -> UI 복귀 루프 연결
- **로깅**: `sdmc:/switch/switch_newpipe.log`
- **빌드**: `./build.sh`로 Switch `.nro`, `make host`로 공용 파서/해석 검증

로그인은 앱 내부 OAuth가 아니라 외부 쿠키 import 방식이다. 홈 개인화 추천은 1차 연결만 된 상태라 실기 검증이 아직 필요하다.

## 핵심 원칙

1. 최우선 타깃은 항상 실제 닌텐도 스위치 홈브류다.
2. 호스트 경로는 개발 보조용이지 최종 제품이 아니다.
3. 로그인 WebView, 멀티뷰, 복잡한 채팅 UI는 우선순위가 낮다.
4. 응답 구조를 임의로 상상하지 말고 실제 네트워크와 로그로 확인한다.
5. 스위치 전용 변경을 해도 공용 파서와 호스트 검증 경로는 같이 유지한다.
6. 실기 이슈는 반드시 `switch_newpipe.log` 기준으로 판단한다.
7. 절대 임의로 `git commit`, `git push` 하지 말 것

## 작업 전 확인

- 프로젝트 개요: `README.md`
- 구조 설명: `docs/architecture.md`
- 빌드/툴체인: `docs/toolchain.md`
- 현재 handoff: `docs/handoff.md`
- 재생 백엔드: `docs/playback.md`
- 실기 테스트 체크리스트: `docs/testing.md`
- 우선순위: `docs/roadmap.md`

## 에이전트가 먼저 해야 할 일

1. 수정 범위가 공용인지, 스위치 전용인지 먼저 구분
2. 재생/네트워크 이슈면 최신 로그부터 확인
3. 스위치 변경이면 실기 검증 포인트와 문서도 같이 갱신
4. 호스트 경로는 공용 로직 빠른 검증용으로만 사용

## 빌드 주의사항

- Switch 빌드는 Docker 경로를 기준으로 유지
- Borealis와 SDL 윈도우는 동시에 살리지 않는다
- `mpv log-file`에 `sdmc:/` 경로 직접 사용 금지
- 재생 이슈는 host 성공만으로 닫지 말고 실기까지 확인

## 현재 입력 흐름

- 목록
  - `A`: 바로 재생
  - `Y`: 상세
- 플레이어
  - `A`: pause / resume
  - `B`: 종료
  - `위 / 아래`: volume

현재 seek는 비활성화 상태다.

## 현재 저장 파일

- 설정: `sdmc:/switch/switch_newpipe_settings.json`
- 로그인 세션: `sdmc:/switch/switch_newpipe_session.json`
- 인증 import 기본 경로: `sdmc:/switch/switch_newpipe_auth.txt`
- 라이브러리: `sdmc:/switch/switch_newpipe_library.json`
- 로그: `sdmc:/switch/switch_newpipe.log`

## 우선순위 높은 다음 작업

1. 설정 탭과 설정 persistence 실기 검증
2. 재생 품질 3종 실기 검증
3. 로그인 import + 구독 피드 실기 검증
4. 채널 / 재생목록 / 댓글 흐름 추가
5. 플레이어 OSD / quality 선택 / seek 복구
6. 홈 개인화 추천 실기 검증
7. 다운로드 / 백그라운드 재생 검토

## 당장 하지 말 것

- WebView 로그인 구현
- 멀티뷰 동시 재생
- 검증 없이 외부 서드파티 스위치 SDK/포크 도입
- Android NewPipe 구조를 그대로 복붙하는 대형 리라이트

## 완료 기준

- `.nro` 빌드 + 실기 동작까지 확인
- 공용 변경 후 `make host` 보조 검증 유지
- 스위치 타깃 변경이면 문서도 같이 최신화
