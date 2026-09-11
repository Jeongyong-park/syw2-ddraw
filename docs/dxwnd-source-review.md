# DxWnd 소스 검토 — 커서 지연과 성능 계측

2026-09-11에 공식 `v2_06_15_src.rar`를 내려받아 정적으로 검토했다.
[공식 소스 배포](https://sourceforge.net/projects/dxwnd/files/Sources/v2_06_15_src.rar/download)
SHA-256: `c0f7632332c5389a1876b0c561286b82594729c192d72e5b9d815f1828f70d38`.
검토 소스는 로컬 `output/v2_06_15_src`에 보관했다. DxWnd 실행·주입이나 게임 설정 변경은 수행하지 않았다.
아래 줄 번호는 이 배포본 기준이다. 권고는 현재 HQCDD 소스와 비교한 판단이며 성능 개선 실측 결과가 아니다.

## 가장 먼저 참고할 부분

| 우선순위 | DxWnd 근거 | HQCDD에 적용할 판단 |
| --- | --- | --- |
| 1 | `dll/sblit.cpp:443`의 `QUARTERBLT`, `LIMITFLIPONLY` | 작은 Blt와 큰 화면 갱신, Flip을 구분해 출력 원인을 계측한다. 모든 갱신에 같은 제한을 걸면 커서 갱신도 늦어질 수 있다. |
| 1 | `dll/user32.cpp:2341`, `dll/dxwcore.cpp:920`의 입력 좌표 처리 | 좌표 계산을 렌더링 대기와 분리할 여지가 있다. 우선 현재 `mouse_lock_wait`, `cursor_lock_wait`를 실제 이동 중 측정한다. |
| 2 | `dll/user32.cpp:2401`, `dll/dinput.cpp:1537` | 좌표 조회·설정, 메시지·DirectInput, 절대·상대 입력을 구분한다. ESL이 실제 사용하는 경로를 확인한 뒤 누락된 훅만 보완한다. |
| 2 | `dll/ddraw.cpp:4820`, `dll/d3d9blit.cpp:93` | 게임이 요청하는 수직 공백 대기와 최종 출력 VSync를 별도 정책으로 다룬다. |
| 3 | `dll/d3d9blit.cpp:596`, `dll/asyncdc.cpp:13` | 비동기 출력은 실험 후보지만, 스레드 추가만으로 입력 지연이 해결되지는 않는다. |

## 1. 출력 원인을 먼저 분리한다

DxWnd는 큰 Blt에만 FPS 처리를 적용하거나 `Flip`만 제한하는 선택지를 제공한다.
`dll/dxwcore.cpp:1453`의 대기 기반 제한과 `:1521`의 출력 생략도 별개다.

현재 HQCDD는 `Flip`, primary `Unlock`, `ReleaseDC`, palette 변경, primary `Blt` 등이
`Draw::present()`로 이어진다(`src/ddraw.cpp:298`, `:350`, `:371`, `:1164`, `:1206`).
작은 갱신도 GPU 경로에서 원본 이미지 전체 업로드와 출력으로 연결될 수 있다.
현재 계측에는 `request_flip`만 있어 나머지 출력 원인을 분리하기 어렵다.

권고: `request_blt`, `request_unlock`, `request_release_dc`, `request_palette`, `request_repaint`와
갱신 면적·surface 종류를 추가한다. 원본 surface를 수정하는 연산은 유지하고,
중복 업로드/출력만 줄일 수 있는지 판단한다. 면적만으로 커서 그림이라고 단정하지 않는다.
팔레트 변경은 작은 API 호출이어도 화면 전체 색에 영향을 줄 수 있다.

`dll/hdxgi.cpp:875`는 생략 요청에도 실제 DXGI `Present`를 호출하며 필요하면 SyncInterval을 0으로 바꾼다.
이미 GPU 명령을 제출한 뒤 Present만 성공한 것처럼 생략하는 방식과,
래퍼 내부에서 업로드 이전에 갱신을 합치는 방식을 구분해야 한다.

## 2. 입력 경로와 렌더링 잠금

현재 `Surface::Flip()`은 전역 재귀 mutex를 잡은 채 `Draw::present()`를 호출한다.
`game_cursor()`와 마우스 메시지 처리도 같은 mutex를 획득한다(`src/ddraw.cpp:294`, `:394`, `:926`).
따라서 업로드나 Present의 대기가 입력 처리를 막을 수 있는 구조다. 실제 병목인지는 측정이 필요하다.

DxWnd의 `extGetCursorPos()`는 실제 커서 조회 → 가상 좌표 변환 → 클리핑을 수행한다.
이 함수와 좌표 보정 함수에는 D3D9 렌더러의 critical section 획득이 보이지 않는다.
이 관찰을 DxWnd 전체가 잠금 없이 동작한다는 주장으로 확대하지 않는다.

권고: 입력에 필요한 창 원점·viewport·게임 크기를 일관된 스냅샷으로 제공하는 설계를 검토한다.
기존 Draw 객체의 수명이나 resize 동기화를 무시하고 mutex만 제거해서는 안 된다.
우선 움직임/정지/반전 중 잠금 대기 p95/p99와 Present 구간의 중첩을 확인한다.

## 3. 좌표 조회만 고쳐서는 충분하지 않을 수 있다

DxWnd는 `GetCursorPos`의 역변환과 `SetCursorPos`의 정변환을 함께 다룬다.
비활성 상태에서는 커서 위치 설정을 억제하고, DirectInput의 절대 좌표와 상대 이동도 구분한다.
메시지 경로에서는 휠의 화면 좌표와 일반 마우스의 클라이언트 좌표를 구별하는 코드가 있다
(`dll/winproc.cpp:194`).

현재 HQCDD의 게임 IAT 패치는 `GetCursorPos` 대상이며 일반 마우스 메시지를 별도로 변환한다.
ESL의 실제 `SetCursorPos`/DirectInput 사용 여부를 먼저 조사해야 한다.
사용하지 않는 입력 API를 포괄적으로 가로채는 것은 이번 문제의 근거 있는 해결책이 아니다.
이미 변환한 좌표를 메시지·조회 경로에서 다시 변환하는 실수도 피해야 한다.

테스트 후보: 음수 모니터 원점, 화면 경계, letterbox, DPI/크기 변경,
Alt-Tab 이후 복귀, 설정창·채팅 EDIT 포커스, 설정→조회 좌표 왕복.
HW 커서 강제 표시 옵션은 소프트웨어 커서 그림을 자동으로 분리해 주지 않는다.

## 4. VSync와 게임 대기를 구별한다

DxWnd의 `extWaitForVerticalBlank()`는 원본 API 호출, 에뮬레이션 대기, 강제 무대기를 플래그로 구분한다.
D3D9 출력의 PresentationInterval도 별도 설정이다.
현재 HQCDD의 `WaitForVerticalBlank()`는 유효한 BLOCKBEGIN/BLOCKEND 요청에 `Sleep(1)`을 수행한다.
출력 VSync를 꺼도 이 경로는 남는다(`src/ddraw.cpp:189`).

권고: 우선 `vertical_blank_wait`의 호출률·누적 시간·호출 스레드를 확인한다.
이 대기를 무조건 없애면 게임 진행 속도나 CPU 사용량에 영향을 줄 수 있으므로,
현재 기본값인 GPU + Sharp Bilinear + VSync OFF와 별개인 호환성 실험으로 다룬다.
DxWnd의 시간 늘이기나 cursor API 내부의 선택적 지연도 커서 지연 개선 목적으로 채택하지 않는다.

## 5. 비동기 출력은 두 번째 단계

DxWnd D3D9 비동기 스레드는 60Hz 제한 후 critical section 안에서 `D3D9FrameBlit()`를 호출하며,
그 함수에는 Present까지 포함된다(`dll/d3d9blit.cpp:588`, `:626`).
surface 갱신 쪽도 같은 critical section을 사용한다(`:674`).
따라서 비동기라는 이름만으로 생산자 대기나 화면 지연이 없어지는 구조는 아니다.

HQCDD에서 시도한다면 렌더 스레드의 D3D 객체 소유권, 최신 프레임 스냅샷,
짧은 surface 잠금, resize·종료 동기화를 함께 설계해야 한다.
프레임을 무제한으로 쌓는 큐는 피하고, 입력과 화면의 신선도를 별도 계측해야 한다.
우선순위는 새 스레드 도입보다 출력 원인과 잠금 경합의 측정이다.

## PresentMon 문제에 대한 한계

검토한 DxWnd의 FPS 카운터는 `HandleFPS`/출력 경로 호출 횟수와 GetTickCount 기반이다
(`dll/dxwcore.cpp:1433`, `:1531`). 화면에 실제 표시된 프레임이나 input-to-photon 검증 수단이 아니다.
`extDXGIPresent`에서도 현재 ESL의 표시 이벤트 연결 실패를 직접 해결하는 로직은 찾지 못했다.

`DisableDWM`이라는 이름의 함수는 비클라이언트 렌더링 정책과 전환 효과 속성을 바꾼다
(`dll/dxwcore.cpp:1167`). 이것을 Windows 합성기를 끄거나 ETW 문제를 해결하는 코드로 해석하면 안 된다.
D3D9의 DISCARD/BackBufferCount=1 설정도 현재 D3D11 경로를 전환할 근거로 충분하지 않다.

현재의 QPC·ETW 제출 이벤트·표시 이벤트 구분은 유지한다.
DxWnd에서 가져올 것은 호환성 조건의 분리와 실험 설계이며,
표시 FPS가 없는 상태를 자체 FPS 숫자로 대체하지 않는다.

## 적용 순서

후속 진행: 출력 원인과 요청 면적 계측을 구현했다. 첫 ESL 10초 표본은 전체 화면 BltFast
1,429회였으며 작은 부분 갱신의 과도한 출력 가설은 확인되지 않았다.
상세 결과와 계측의 한계는 [성능 테스트 문서](performance-testing.md)에 기록한다.

1. 기존 동작을 유지하면서 출력 원인/면적과 입력 API 사용 여부를 계측한다.
2. 움직임·정지·방향 반전 조건에서 VSync ON/OFF의 입력 잠금 대기를 비교한다.
3. 병목이 확인되면 입력용 좌표 스냅샷과 중복 출력 감소를 각각 독립적으로 실험한다.
4. 여전히 렌더링 대기가 게임 스레드를 지배할 때 비동기 출력 설계를 진행한다.

각 실험은 CPU·호출 간격뿐 아니라 클릭 위치, 채팅/로그인 입력, Alt-Tab, resize, 종료를 검증한다.
OS별 수동 검증을 1.0 이후로 미루기로 한 기존 결정은 유지한다.

소스 표기상 DxWnd 핵심은 GPL-3.0-or-later(`dll/dxwnd.cpp:6`), 현재 저장소는 MIT다.
이번 검토에서는 코드를 복사하지 않았으며, 후속 구현은 관찰한 동작과 요구사항을 바탕으로 독자 설계한다.
