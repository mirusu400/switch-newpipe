# Roadmap

## Phase 0: 앱 골격

- [x] 루트 프로젝트 문서 추가
- [x] Borealis 앱 엔트리포인트 추가
- [x] 탭 기반 UI 골격 추가
- [x] Docker 빌드 스크립트 추가
- [x] `make host` 검증 경로 추가
- [x] 로그 파일 저장 경로 추가

## Phase 1: 데이터 흐름

- [x] 공용 `CatalogService` 모델 정의
- [x] fixture 기반 홈 / 검색 / 상세 흐름
- [x] 실제 `YouTubeCatalogService` 추가
- [x] 홈 카테고리 4종 연결
- [x] 검색 결과 연결
- [x] 쿠키 세션 import / 저장
- [x] 인증된 구독 피드 호출 뼈대
- [x] 상세 설명 / 채널 ID 보강
- [x] 채널 업로드 피드
- [x] 연관 추천 fallback
- [x] watch-next 기반 연관 추천 파서
- [x] 재생목록 파서
- [x] 댓글 1페이지 파서
- [x] 로그인 기반 추천 홈 1차 연결
- [x] 썸네일 비동기 로딩
- [x] 채널 화면
- [x] 관련 영상 목록
- [ ] 상세 메타데이터 확장

## Phase 2: 재생

- [x] Borealis -> 플레이어 전환
- [x] SDL2 + OpenGL ES + mpv 플레이어 연결
- [x] B 버튼으로 재생 종료 후 UI 복귀
- [x] A 버튼 pause / resume
- [x] 위 / 아래 볼륨 조절
- [x] 로딩 오버레이 + 상태 문구
- [x] YouTube URL 해석기 추가
- [x] 스트림 브리지 캐시 기반 재생
- [x] 실기에서 실제 YouTube 영상 재생 확인
- [ ] 720p 경로 실기 안정화
- [ ] 좌우 seek 복구
- [ ] 재생 중 OSD
- [ ] 화질 수동 선택

## Phase 3: 앱다운 기능

- [x] 구독 화면 실제화
- [x] 최근 시청 저장
- [x] 즐겨찾기 저장
- [x] 라이브러리 화면 실제화
- [x] 기본 설정
- [ ] 검색/재생 캐시 정책 정리

## Phase 4: 콘텐츠 확장

- [ ] 라이브 전용 UX
- [ ] 채널 browse / 탭 구조
- [ ] 댓글 / 재생목록 pagination
- [ ] 개인화 홈 추천 고도화

## 후순위

- [ ] 앱 내부 Google OAuth / WebView 로그인
- [ ] 멀티뷰
- [ ] 다운로드
