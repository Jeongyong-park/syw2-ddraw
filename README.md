# SYW2Plus 전용 그래픽 래퍼 — HQCDD 0.5.2

조선의반격의 **DirectDraw 7 / 8비트 팔레트** 화면을 메모리에 유지하고,
기본 D3D11 하드웨어 출력에서 팔레트 인덱스를 셰이더로 변환하여 Windows 창에 출력한다.
GPU 초기화·출력 실패 시 CPU RGB 변환과 GDI 출력으로 전환한다.
DxWnd나 dxwrapper 코드를 포함하지 않은 전용 구현이다. 시스템의 화면 해상도와
색상 모드를 바꾸지 않는다. 네트워크/DirectPlay/WireGuard 기능은 포함하지 않는다.

현재는 **초기 시험 버전**이다. 모든 DirectDraw 게임용 호환 라이브러리가 아니다.
창 크기 변경·최대화와 테두리 없는 전체화면을 지원한다. 게임 비율을 유지하고 남는 공간은 검게 표시한다.
Direct3D 11은 최종 출력에 사용하며 게임에 Direct3D COM API를 제공하지는 않는다.

## 게임 내부 디스플레이 오버레이 (v0.5)

**Ctrl+Alt+D**로 연다. 제목 표시줄 시스템 메뉴에 설정 항목이
표시되는 환경에서는 **디스플레이 설정** 메뉴로도 열 수 있다.
게임 화면에서 창 모드/전체화면, GPU/GDI 출력, 확대 필터, 수직동기화를 바꿀 수 있다.
**적용**을 누르면 재시작 없이 변경된다. **다음 실행에도 저장**을 체크하면
실행본 옆 `hqcdd.ini`에도 저장하며, 해제하면 현재 실행에만 적용한다.
**닫기/Esc**는 적용하지 않은 선택을 버리고 이전 게임 입력창으로 포커스를 돌린다.
설정은 게임 화면 안의 테두리 없는 오버레이로 표시한다. 짙은 갈색 패널과 금색 선택 표시,
크기에 맞춰 조절되는 한글, 키보드 포커스·마우스 강조 효과를 제공한다.
배경에는 설정을 열 때의 게임 화면을 어둡게 표시하며 게임 자체는 계속 진행된다.
오버레이가 열린 동안 게임으로 향하는 키보드·마우스 메시지와 게임 EXE의
GetAsyncKeyState/GetKeyState/GetKeyboardState 조회를 차단한다. 닫을 때 누르고 있던
키는 놓을 때까지 차단한다. 다른 프로세스나 Windows 전체 입력은 변경하지 않는다.
Tab/Shift+Tab으로 항목을 이동하고 Space/Enter로 선택할 수 있다.
게임의 팔레트·논리 해상도·진행 속도 값은 변경하지 않는다.
GDI 선택 시 GPU 전용 옵션은 비활성화된다. GPU 실패와 파일 저장 실패는 화면에 표시한다.

## GPU 출력 설정

`hqcdd.ini`의 `[Display]`를 직접 편집한 경우 게임을 다시 실행한다.
실행 중 변경하려면 Ctrl+Alt+D 설정창을 사용한다.

| 설정 | 기본값 | 동작 |
| --- | --- | --- |
| `Renderer` | `auto` | D3D11 하드웨어 사용, 실패 시 GDI. `gdi`는 GDI 강제 사용 |
| `VSync` | `0` | `1`이면 GPU Present 수직동기화. 게임이 한 프레임에 여러 번 출력하면 속도에 영향을 줄 수 있음 |
| `LinearFilter` | `0` | GPU 확대 시 `0`은 선명하게, `1`은 부드럽게 |

8비트 팔레트, 16비트 RGB565, 32비트 색상 변환과 확대를 GPU 셰이더에서 수행한다.
게임의 Lock/Blt용 메모리 표면은 유지하며 화면 데이터를 GPU 텍스처에 업로드한다.
GDI 입력창과의 호환성을 위해 windowed blt-model swap chain을 사용한다.
Alt+Enter는 래퍼가 관리하며 DXGI 자체 전환은 끈다. GPU 오류가 나면 해당 실행에서는
GDI를 유지하고 원인 HRESULT를 `hqcdd.log`에 남긴다.
현재 Windows에서 D3D11 feature level 11.0 하드웨어 경로를 검증했다.
래퍼는 D3D11/DXGI/D3DCompiler DLL을 로드 시점에 필요로 한다. GDI 자동 전환은
이 DLL들이 로드된 뒤 GPU 초기화·출력이 실패한 경우에 적용된다. 런타임 DLL 자체가
없는 구형 Windows에서는 GDI 설정만으로 실행할 수 없으며, 해당 환경은 지원을 검증하지 않았다.

GPU readback 테스트로 8/16/32비트 색상, 팔레트 변경, 이미지 방향, 행 간격,
여백, 보간 필터, 창 크기 변경 후 출력 픽셀을 검증한다.
CPU 사용량과 실제 게임 FPS의 비교 측정은 아직 수행하지 않았으며 성능 향상 폭은 미확정이다.

## 화면 조작

- **Alt+Enter**: 창 모드 ↔ 현재 모니터를 채우는 전체화면. 입력칸에 포커스가 있어도 전환한다.
- 창 테두리 드래그 또는 최대화 버튼: 창 크기 변경.
- 제목 표시줄의 시스템 메뉴에서도 창 모드/전체화면을 선택할 수 있다.
- 창 모드로 복귀하면 이전 창 위치·크기·최대화 상태를 복원한다.
- 전체화면에서는 마우스를 게임 영역 안에 유지하고 다른 앱으로 전환하면 해제한다.
- `hqcdd.ini`의 `[Display] Fullscreen=1`이면 전체화면으로 시작한다. `0`은 창 모드다.
  Alt+Enter 전환은 현재 실행에만 적용되며 시작 설정을 덮어쓰지 않는다.
- HQNET 자식 입력창의 위치·크기·폰트와 게임 마우스 좌표도 화면 배율에 맞춰 보정한다.

전체화면은 모니터 해상도를 변경하지 않는 borderless 방식이다.

## v0.2 자동 검증

DLL 통합 테스트에서 Alt+Enter와 키 반복 방지, 자식 입력칸 포커스 상태의 전환,
모니터 크기의 전체화면, 기존 창 위치 복원, 크기 변경 후 입력칸 배치,
전환 중 글자 보존, 최소화/복원, 논리 해상도 유지를 검증했다.
4:3 화면의 가로·세로 여백과 마우스 좌표 역변환도 검사한다.

생성기는 수정 가능한 `hqcdd.ini`도 함께 설치하며 이 설정 파일은 실행 시 해시 검사에서 제외한다.

## 구현

- `IDirectDraw7`, `IDirectDrawSurface7`, `IDirectDrawPalette` COM 객체 및 참조 수명.
- 8비트 팔레트, RGB565 16비트, XRGB8888 32비트 표면.
- 팔레트 변경만으로도 즉시 재출력: 페이드와 색상 순환 지원.
- CPU Lock/Unlock, 1개 백버퍼의 Flip, Blt/BltFast, 색 채우기,
  투명색/대상 색키, 확대·축소 복사, 좌우·상하 반전, 겹치는 자기 복사.
- 표면 GetDC/ReleaseDC를 DIB로 제공하며 변경 픽셀을 표면으로 되돌린다.
- HWND 클리퍼가 있는 주 표면의 화면 좌표를 클라이언트 좌표로 변환한다.
- 주 화면 출력에서 자식 창을 제외한다. HQNET 로그인 EDIT 입력창·글자·캐럿을
  게임의 반복 출력이 덮어쓰지 않도록 `WS_CLIPCHILDREN`과 DC 클리핑을 함께 쓴다.
- 창 이동 후 마우스 위치 보정: **실행 파일의 GetCursorPos IAT 한 항목만**
  창 내부 좌표로 변환하고 객체 종료 때 복원한다. 다른 프로세스는 수정하지 않는다.
- 클리퍼 객체만 Windows 시스템 경로의 DirectDraw에 위임한다.
- 지원하지 않는 API/일부 플래그는 성공으로 위장하지 않고 오류를 반환하며
  `hqcdd.log`에 기록한다. 객체는 게임의 주 스레드에서 초기화해야 한다.

## 빌드 및 자동 테스트

Windows Visual Studio C++와 Windows SDK, CMake가 필요하다.

```powershell
./build.ps1
python -m unittest discover -s tests -p test_prepare.py -v
```

산출물: `build/Release/hqcdd.dll` (x86, MSVC 런타임 정적 링크).
네이티브 테스트는 실제 DLL을 로딩하여 COM ABI, 팔레트, GDI DC, Flip,
클리핑, 입력 컨트롤 보호 및 참조 수명을 검사한다. 테스트 창이 잠깐 나타날 수 있다.
Python 테스트는 독립적인 PE 픽스처로 원본 보존과 잘못된 파일 거부를 검사한다.
GitHub Actions는 Windows x86 Release 빌드와 Python 설치 도구 테스트를 실행한다.
네이티브 실행 테스트는 대화형 데스크톱과 D3D11 하드웨어가 필요하므로 로컬에서
`build.ps1`로 실행한다. CI의 빌드 성공만으로 GPU 실행 검증을 대신하지 않는다.

## 시험용 실행 파일 생성

소스 폴더의 `launch.ps1`을 바로 실행하지 않는다. `prepare.py`가 생성한 출력 폴더의
`launch.ps1`을 실행해야 한다. `hqcdd-install.json`은 해당 실행본의 경로와 해시를 담으므로
소스 폴더로 복사하지 않는다.

```powershell
python prepare.py "D:\syw2plus\조선의반격 오리지날 실행 충무공넷.exe"
```

원본 옆에 다음 **새 파일만** 생성한다. 이미 있으면 덮어쓰지 않고 중단한다.

1. `조선의반격 오리지날 실행 충무공넷 HQ그래픽.exe`
2. `hqcdd.dll`
3. `hqcdd-install.json` — 원본·생성 파일 해시와 수정 위치
4. `launch.ps1` — 원본 게임 폴더를 작업 디렉터리로 지정하는 실행 스크립트

게임이 종료된 상태에서 위의 `HQ그래픽.exe`를 실행한다.
`--output-dir <시험폴더>`도 가능하지만 그 경우 작업 디렉터리를 원본 게임 폴더로
설정해야 한다. 생성된 `launch.ps1`로 실행하면 자동 지정된다.
게임이 실행 파일 상대경로로 읽는 자원은 추가 확인이 필요하다.

EXE의 PE import descriptor에서 `DDRAW.dll`이라는 이름만 동일 길이의
`hqcdd.dll`로 바꾼다. 기계 코드, HQNET 주소, DirectPlay 패치는 변경하지 않는다.
원본 EXE, 기존 `ddraw.dll`, DxWnd/dxwrapper 설정은 손대지 않는다.
따라서 기존 `ddraw.dll`이 불러오던 **syw2x 플러그인은 시험본에서 자동 로딩되지 않는다.**
게임 기능 모드와의 결합은 별도 검증 대상이다.

복구는 원래 EXE로 실행하면 된다. 시험본을 제거하려면 게임 종료 후 위 네 파일과
시험본이 남긴 `hqcdd.log`만 삭제한다. 기존 DLL이나 설정 파일을 삭제하지 않는다.
업데이트 때도 설치 기록의 파일과 해시를 먼저 확인한다.

## 분석 근거 및 범위

확인한 대상 SHA-256:
`6c0597be236fc5c803b130bed6ae6ce19698b1b4b58af8b790c41b9560e5cea8`.
두 제공 EXE는 동일 바이너리였다 (1,032,192바이트).

- `0x464374`: `DirectDrawCreateEx` 호출, IID at `0x4e5928` =
  `{15E65EC0-3B9C-11D2-B92F-00609797EA5B}` (`IDirectDraw7`).
- `0x46457a`: SetDisplayMode, `0x4645fc`: 주 표면 생성.
- `0x4648e3`: GetAttachedSurface; `0x464f30`: Lock;
  `0x4650b0`: Unlock.
- 이 빌드의 import 이름 file offset: `0xebb7e`. 생성기는 하드코딩 오프셋 대신
  PE 섹션·import 테이블을 검증하며 DirectDrawCreateEx만 가져오는 파일을 허용한다.

공식 API 자료:

- [DirectDraw SetPalette](https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nf-ddraw-idirectdrawsurface7-setpalette)
- [Windows DIB와 RGB 출력](https://learn.microsoft.com/en-us/windows/win32/gdi/device-independent-bitmaps)

운영 배포 전에는 HQNET 채팅·한글 IME, 동영상, 관전 포함 장시간 멀티플레이,
다중 모니터/DPI, GPU·Windows 버전별 동작을 추가 확인해야 한다.

## 이번 검증 (2026-09-10)

- Windows x86 Release DLL 빌드 및 네이티브 통합 테스트 통과.
- PE 생성기 원본 보존·잘못된 import·x64 거부 테스트 통과.
- 실제 게임 메인 화면, 혼자하기 선택/설정 화면 출력 확인.
- 실행 중 800×600 8비트 ↔ 640×480 16비트 모드 전환 호출 확인.
- 사용자 보고: HQNET 로그인 입력칸/글자 사라짐·깜빡임.
  자식 창 영역을 주 화면 재출력에서 제외한 뒤 로그인 입력칸과 HQNET 로비의
  채팅 입력칸 표시를 확인했다. 실제 계정 로그인은 사용자가 진행했다.
- 장시간 인게임/다인전, 한글 IME 조합, 모든 GPU 환경은 아직 검증하지 않았다.

## v0.5.1 커서 수정

오버레이에서 Windows 화살표 커서를 표시한다. 게임의 반복 ShowCursor(FALSE)와
SetCursor(NULL) 호출을 오버레이 동안 보정하고, 닫을 때 게임의 커서 카운터와 모양을 복원한다.
숨김 카운터가 음수인 경우, 반복 숨김, 버튼 위 커서, 닫기 후 복원을 자동 검증했다.

## v0.5.2 단축키 충돌 수정

F10은 게임 생산 단축키이므로 래퍼에서 가로채지 않는다. 오버레이는 Ctrl+Alt+D로 열고
Esc 또는 닫기 버튼으로 닫는다. 오버레이가 열린 동안 게임 입력을 차단하는 동작은 유지한다.
F10의 누름·뗌 메시지가 원래 게임 창 프로시저까지 전달되는 것을 자동 검증했다.

## 라이선스

MIT — `LICENSE` 참조. Copyright (c) 2026 Park Jeongyong.

DxWnd, dxwrapper 등 기존 래퍼의 코드를 포함하지 않는다. 조선의반격 게임 파일은
포함하지 않으며, `prepare.py` 는 사용자가 가진 원본 EXE 를 **읽어서 복사본을
만들 뿐 원본을 수정하지 않는다**.

## 출신

HQnet 프로토콜 연구 저장소(`hqnet-research`)의 `native/syw2-ddraw` 에서
독립시켰다. 래퍼는 HQnet 서버·DirectPlay·네트워크 기능과 무관하며, 그래픽 출력만
담당한다. 타이밍 분석 문서와 도구(`docs/graphics_timing.md`, `tools/`)도 함께 옮겼다.
