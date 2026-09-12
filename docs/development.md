# 개발자 안내

일반 사용자 설치·설정 안내는 [README](../README.md)를 참고합니다.
이 문서는 저장소에서 ASI와 DLL 호환판을 빌드하고 테스트·패키징하는 개발자를 대상으로 합니다.
기본 배포 방식은 기존 ESL 로더를 유지하는 ASI입니다. Python과 빌드 도구는 개발·패키징·별도 시험본 생성에만 필요합니다.

## 환경과 빌드

Windows, Visual Studio의 C++ 데스크톱 개발 도구, Windows SDK, CMake 3.24 이상,
Python 3.10 이상이 필요합니다. 게임 프로세스에 맞춰 DLL은 x86으로 빌드합니다.

저장소 루트에서 실행합니다.

```powershell
.\build.ps1
python -m unittest discover -s tests -p "test_*.py" -v
```

산출물은 `build/Release/hqcdd.asi`와 `build/Release/hqcdd.dll`입니다.
기본 설치는 ASI를 기존 로더의 `plugins` 폴더에 두고 `hqcdd.ini`를 게임 EXE 옆에 둡니다.
기존 설정은 보존합니다. 원본 EXE 파일은 수정하지 않으며 MSVC 런타임을 정적 링크합니다.
Debug는 `.\build.ps1 -Configuration Debug`로 빌드합니다.

빌드만 수행하려면:

```powershell
cmake -S . -B build -A Win32
cmake --build build --config Release
```

D3D11 feature level 11.0 하드웨어에서 GPU 출력을 검증합니다.
GDI 설정에서도 D3D11/DXGI/D3DCompiler DLL은 로드 시점에 필요합니다.
GDI 자동 전환은 런타임 DLL 로드 이후 GPU 초기화·출력이 실패한 경우에 적용됩니다.

## 테스트와 CI

| 검증 | 명령·범위 |
| --- | --- |
| 네이티브 | `ctest --test-dir build -C Release --output-on-failure` |
| DLL 속성 | `.\tests\test_version.ps1` |
| Python | `python -m unittest discover -s tests -p "test_*.py" -v` |

`build.ps1`은 빌드, DLL 버전 검사, 네이티브 테스트를 순서대로 실행합니다.
네이티브 테스트에는 대화형 데스크톱과 D3D11 하드웨어가 필요하며 테스트 창이 나타납니다.
CTest의 `ci` 라벨은 호스팅 CI에서 실행하는 11개 테스트, `desktop` 라벨은
로컬 데스크톱에서 실행하는 `wrapper_test`, `asi_core_test`, `gpu_test`를 선택합니다.
모든 네이티브 테스트에는 `native` 라벨도 붙습니다. `ci` 테스트도 임시 Windows 창을
생성할 수 있으며, 이 분류는 CPU 전용 여부를 뜻하지 않습니다.

```powershell
ctest --test-dir build -C Release -L "^ci$" --no-tests=error --output-on-failure
ctest --test-dir build -C Release -L "^desktop$" --no-tests=error --output-on-failure
```

새 네이티브 테스트는 CMake의 `hqcdd_test(이름 ci|desktop 실행명 인수...)`로 등록합니다.
CI 대상은 이 라벨에서 선택하므로 워크플로에 테스트 이름을 추가할 필요가 없습니다.
`build.ps1`은 라벨 필터 없이 전체 네이티브 테스트를 실행합니다.
CI는 Windows x86 컴파일, DLL 메타데이터, Python 설치·패키징 테스트를 실행합니다.
호스팅 CI에서 GPU 실행 결과를 검증했다고 간주하지 않습니다.

- wrapper_test: COM 수명, 팔레트·표면 복사, GDI 입력칸, 창 전환, 좌표 변환, 오버레이·F10·커서 회귀.
- gpu_test: 8/16/32비트 readback, 팔레트 변경, 행 간격, 방향, 필터 경계, 정수·비정수·축소 배율.
- test_prepare: 독립 PE 픽스처로 원본 보존과 잘못된 입력 거부.
- test_package: 사용자·개발자 ZIP 구성, 내부·외부 SHA-256, 버전 형식, 반복 패키징과 실패 시 기존 ZIP 보존.
- test_distribution.ps1 / loader_test: 추출한 ZIP의 한글·공백 경로에서 일반 DDRAW.dll import, 시스템 AMStream 로딩 및 레거시 DirectDraw 위임 검증.
- test_version.ps1: 실제 DLL의 제작자·문자열 버전·숫자 버전 확인.

`wrapper_test.exe <DLL 경로> --save-test`는 DLL 옆 INI에 기록합니다.
반드시 임시 폴더에 복사한 DLL로 실행하고 사용자 설치에는 사용하지 않습니다.

## Unicorn CPU 검증

`python -m pip install unicorn==2.1.4` 후 `python tests/unicorn_viewport.py`를 실행합니다.
Release 빌드의 테스트 전용 unicorn_probe.dll에서 실제 src/viewport.h 코드를 x86으로 실행합니다.
화면 비율·정수 배율·축소·0 크기·고정 시드 무작위 입력에 이동·정지·반전과 경계 좌표를 더한
3,852개 사례를 유리수 기준값과 비교합니다. 물리 크기를 바꾸는 계산 검증이며 Windows DPI API 검증은 아닙니다.
Windows API나 DllMain을 실행하지 않으며 GDI·IME·GPU·게임 진행 속도·Windows 10/11 호환성 검증은 아닙니다.
테스트 DLL은 배포 ZIP에 포함하지 않습니다. CI에서도 같은 검사를 수행합니다.
참고: [Unicorn 문서](https://www.unicorn-engine.org/docs/).

## 설치 방식과 패키징

### 기본 설치: ASI

1. 기존 ESL의 ASI 로더와 SYW2X를 유지합니다.
2. `build/Release/hqcdd.asi`를 게임의 `plugins/hqcdd.asi`로 복사합니다.
3. `hqcdd.ini`는 게임 EXE 옆에 두며 기존 파일을 덮어쓰지 않습니다.

DLL판에서 이전할 때는 기존 로더 복원이 먼저 필요합니다.
[ASI 설치·복구 안내](../ASI-INSTALL.txt)와 [연결 방식](asi-integration.md)을 따릅니다.
로더와 SYW2X 바이너리는 배포 ZIP에 포함하지 않습니다.

### DLL 호환판: 로더 교체

1. 기존 게임 폴더의 ddraw.dll을 백업합니다.
2. 빌드한 hqcdd.dll을 게임 EXE 옆에 ddraw.dll로 복사합니다.
3. hqcdd.ini를 같은 폴더에 두고 기존 EXE를 실행합니다.

기존 dxwrapper/syw2x 경로는 대체됩니다. 사용자 설치의 최초 DLL 백업과 설정을 덮어쓰지 않습니다.
설정·로그 파일명은 hqcdd.ini/hqcdd.log로 유지되며 설치 기록·런처 해시 검사는 사용하지 않습니다.
게임 데이터나 시스템 DLL은 수정하지 않습니다.

검증에서는 임시 폴더에 DLL을 ddraw.dll로 복사하여 wrapper_test를 실행했습니다.
DirectDrawCreateEx export와 시스템 경로의 DirectDraw 클리퍼 위임을 포함한 통합 테스트가 통과했습니다.
기존 EXE의 자동 DLL 로딩부터 실제 전투·종료까지의 검증은 별도로 수행해야 합니다.

### 별도 시험본: 기존 환경과 비교할 때

prepare.py와 launch.ps1은 기존 DLL을 교체하지 않고 나란히 시험하기 위한 개발 도구입니다.
일반 사용자 기본 설치 절차에는 포함하지 않습니다.

```powershell
python .\prepare.py "D:\syw2plus\조선의반격 오리지날 실행 충무공넷.exe" --output-dir ".\output\syw2-graphics-test"
& ".\output\syw2-graphics-test\launch.ps1"
```

생성기는 원본 EXE의 import 이름만 바꾼 별도 복사본을 만들고 기존 파일을 덮어쓰지 않습니다.
생성 런처는 EXE·DLL·런처 해시를 검사하며 INI는 수정 가능 파일로 제외합니다.
자세한 PE 검증과 주소는 [호환성 자료](compatibility.md)를 참고합니다.

### 패키징

```powershell
python .\package.py
python .\package.py --asi
python .\package.py --developer
.\tests\test_distribution.ps1
```

- `output/syw2-ddraw-v<버전>-asi.zip`: 기본 릴리즈용. plugins/hqcdd.asi와 설정·설치 안내를 제공합니다.
- `output/syw2-ddraw-v<버전>.zip`: DLL 호환판. 루트에 ddraw.dll, hqcdd.ini, INSTALL.txt와 문서를 제공합니다.
- `output/syw2-ddraw-v<버전>-developer.zip`: 소스·도구·테스트와 build/Release/hqcdd.dll을 syw2-ddraw/ 아래에 제공합니다.
- 각 ZIP 옆의 `.zip.sha256`은 ZIP 전체 해시이며, ZIP 안의 `SHA256SUMS.txt`는 해당 파일을 제외한 내부 파일별 해시입니다.

패키징 명령은 VERSION과 실제 DLL 속성을 먼저 비교합니다.
필수 파일 누락이나 ZIP 생성 실패 시 기존 ZIP을 보존하며 게임 EXE·Git 내부 파일·기존 산출물은 제외합니다.
스크린샷을 포함한 docs 문서는 세 ZIP 모두에 들어갑니다. 사용자 설치에는 개발 도구가 필요 없습니다.

`test_distribution.ps1`은 사용자 ZIP을 한글·공백이 포함된 임시 폴더에 풀고 DLL 메타데이터를 확인합니다.
시스템 ddraw import library로 링크한 loader_test.exe가 옆의 ddraw.dll을 자동으로 불러오는지,
영상 파일 없이 시스템 amstream.dll 로딩, DirectDrawCreate를 통한 시스템 IDirectDraw 생성,
DirectDraw7 생성 및 시스템 클리퍼 위임을 확인한 뒤 임시 폴더를 정리합니다.
영상 제거 여부와 별개로 AMStream의 DDRAW.dll import에 필요한 DirectDrawCreate export를 검사합니다.
ASI ZIP은 기존 import를 ASI 코어로 연결하는 경로도 검사합니다.
CI는 세 ZIP을 생성하고 이 테스트를 실행한 뒤 ZIP과 해시를 아티팩트로 보관합니다.
이는 실제 게임의 로그인·전투·종료나 개발 도구 없는 Windows 10·11 검증을 대체하지 않습니다.
남은 실기기 검증과 기록 양식은 [0.6 구현·검증 계획](milestone-0.6.md)에 있습니다.

## 소스 구조

| 경로 | 역할 |
| --- | --- |
| `src/ddraw.cpp` | DirectDraw7 COM 객체, Windows 메시지, 입력 훅, 설정·출력 연동 |
| `src/display_settings.*` | 디스플레이 INI 읽기·저장, 이전 필터 키 호환, 비율 저장 결과 |
| `src/overlay_input.*` | 설정창의 키 차단·키 해제 대기, ShowCursor 카운터·커서 모양 복원, 해당 IAT 훅 수명 |
| `src/gdi_child.*` | 자식 컨트롤의 원래 위치·폰트 보관, 확대 폰트 수명, 클리핑 복원 |
| `src/pixels.*` | 소프트웨어 표면, 팔레트·RGB 변환, 복사·색키 |
| `src/gpu.*` | D3D11 업로드·색상 변환·확대·readback |
| `src/viewport.h` | 화면·GDI 자식 컨트롤·마우스의 공통 좌표 변환 |
| `src/scaling.h` | 확대 방식 이름과 이전 설정 해석 |
| `src/overlay.*`, `src/settings.*` | 게임 화면 안의 설정 UI |
| `src/version.*.in` | VERSION에서 생성하는 로그 헤더·Windows 버전 리소스 |
| `src/widescreen_runtime.*`, `src/widescreen_recipe.h` | 운영 ASI의 실험적 전장 패치와 생성 명세 |
| `src/battle_aspect.h` | 비율 초기화 및 구형 시제품 호환 |
| `tests/support/wide_terrain_model.h` | 테스트 전용 초기 캐시 모델. 운영 구현과 구분 |
| `prepare.py`, `launch.ps1` | 개발·비교용 별도 실행본 생성과 검증·실행 |
| `package.py` | 배포 파일 선택 및 ZIP 생성 |
| `tools/perf/` | 성능 보고서·실행기·게임 틱 관측 구현 |
| `tools/widescreen/` | 와이드 조사·생성·검증 도구 구현 |

기존 `python tools/perf_matrix.py ...` 등의 명령은 호환 진입점으로 유지합니다.
저장소 루트에서는 `python -m tools.perf.perf_matrix ...`도 사용할 수 있습니다.
패키지 안의 파일을 직접 실행하지 않습니다. 상세 도구 분류와 실행 방식은
개발자 소스의 `tools/README.md`에 있습니다. 개발자 ZIP에는 패키지와 호환 진입점을
모두 포함하며, 사용자 ASI/DLL ZIP에는 Python 도구를 포함하지 않습니다.

GDI 입력창과의 호환성을 위해 windowed blt-model swap chain을 사용합니다.

`display_settings_test`는 임시 INI에서 기본값, 이전 `LinearFilter`와 `Scaling`의
우선순위, 알 수 없는 값의 기존 해석, 미지정 비율·진단·외부 키 보존, 저장 실패를
검사합니다. 설정 저장은 기존과 같은 키별 쓰기이며 원자적 저장을 보장하지 않습니다.
전체 성공 여부와 비율 키 저장 성공을 구분해 UI의 저장 기준을 갱신합니다.

`overlay_input`은 설정창 열림 여부를 콜백으로 읽습니다. 설정창 생성·종료 및 포커스
전환은 `ddraw.cpp`가 담당하며 기존 호출 순서와 잠금 범위를 유지합니다.
게임 좌표로 바꾸는 GetCursorPos 훅과 GDI 자식 컨트롤 배치는 이 모듈의 책임이 아닙니다.
DLL/ASI `wrapper_test`가 숨겨진 커서 카운터, 설정창 도중 게임의 커서 변경,
키 입력 차단과 닫은 뒤 복원을 검사합니다. 실제 게임 플레이 검증을 대신하지 않습니다.

`gdi_child`는 컨트롤 하나의 상태와 복원 처리를 담당합니다. 창 등록·서브클래스 수명,
메시지 재진입 방지와 부모 관계 판단은 `Draw`가 관리합니다. 등록 목록에서 먼저 제거한
뒤 서브클래스를 해제하고 원래 위치·폰트를 복원하는 순서를 유지합니다.
`gdi_child_test`는 임시 창에서 위치·폰트·클리핑 복원과 확대 폰트 해제를 검사합니다.
빌려온 원래 폰트는 해제하지 않으며 이미 파괴된 창의 확대 폰트도 정리합니다.
전체화면은 borderless 방식이고 Alt+Enter는 래퍼가 관리합니다.
게임의 Lock/Blt 표면은 메모리에 유지하며 최종 출력의 색상 변환·확대를 GPU에서 수행합니다.
현재 구현은 범용 DirectDraw 대체물이 아니며 지원하지 않는 API는 오류로 처리합니다.

## INI 호환성

성능 지표와 자동 진단 도구는 [성능 검사](performance-testing.md)를 참고하세요.

설정 파일은 로드된 DLL 옆 `hqcdd.ini`의 `[Display]` 섹션입니다. DLL을 ddraw.dll로 바꿔도 설정·로그 파일명은 바뀌지 않습니다.

| 키 | 기본 배포값 | 의미 |
| --- | --- | --- |
| Fullscreen | 0 | 0=창 모드, 1=전체화면 |
| Renderer | auto | gdi=GDI, auto=GPU 시도 후 실패 시 GDI |
| Scaling | sharp-bilinear | nearest / bilinear / sharp-bilinear / integer |
| VSync | 0 | GPU Present 수직동기화 |
| LinearFilter | 0 | Scaling이 없는 이전 설정의 보간 여부 |

Scaling과 LinearFilter가 모두 없으면 Sharp Bilinear를 사용합니다.
Scaling이 없고 기존 LinearFilter가 있으면 1은 Bilinear, 0은 Nearest로 해석합니다. 알 수 없는 Scaling 값은 Nearest입니다.
GDI에서는 보간 방식 선택을 유지하더라도 실제 출력은 Nearest입니다.
Integer는 정수 배율과 공통 viewport를 사용하고 원본보다 작은 창에서는 비율 유지 축소합니다.
Sharp Bilinear는 정수 픽셀 복제 후 bilinear 보간의 효과를 직접 계산합니다.
[알고리즘 참고](https://github.com/rsn8887/Sharp-Bilinear-Shaders).

게임 속도 설정·타이머 값은 변경하지 않습니다. 다중 Present 때문에 VSync가 진행 속도에 영향을 줄 수 있습니다.
[타이밍 분석](graphics_timing.md)을 참고하고 실제 진행량과 화면 출력 FPS를 구분하세요.

## 문서와 릴리스 관리

- 사용자 안내는 README, 개발 절차는 이 문서, 변경 이력은 CHANGELOG에 기록합니다.
- [버전 정책](versioning.md)에 따라 VERSION을 갱신하고 다시 빌드합니다.
- DLL 제작자·제품명·저작권·버전 속성은 파일 정보이며 코드 서명이 아닙니다.
- 공개된 버전의 자산을 같은 이름으로 바꾸지 않습니다.
- [1.0 로드맵](https://github.com/Jeongyong-park/syw2-ddraw/issues/12)과 실제 환경별 검증 결과를 구분합니다.

### 스크린샷 갱신

`docs/images/`에는 실제 배포본에서 캡처한 PNG를 보관합니다.
이미지는 import를 바꾼 별도 시험본으로 촬영했습니다. DLL 교체 설치의 실게임 검증 결과를 의미하지 않습니다.
현재 사용자 안내 이미지는 0.6.0 HQNET 로그인·채팅·게임방 로비와 로비에서 연 설정 화면입니다.
당시 설정 화면은 기본 GDI + Nearest 상태이며 값을 변경하거나 저장하지 않고 닫았습니다.

UI가 바뀌면 해당 버전에서 다시 캡처하고 파일명·본문 캡션을 함께 갱신하세요.
계정·채팅·비밀번호가 보이는 화면을 문서에 넣지 않습니다.
이미지를 추가할 때 패키지 포함 여부도 검사합니다. 게임 화면의 권리는 게임 권리자에게 있습니다.
## 기본 릴리즈 산출물

다음 새 버전부터 `python package.py --asi`의 `syw2-ddraw-v<버전>-asi.zip`과
체크섬만 GitHub Release에 게시합니다. 일반 DLL ZIP과 개발용 ZIP은 CI 산출물로 유지합니다.
`python tools/release.py --tag v<버전>`은 게시 없이 태그, 체크섬, ASI ZIP 필수 파일과
로더·SYW2X 바이너리 미포함을 검증합니다. 실제 게시에는 `--publish`가 필요합니다.
기존 릴리즈는 덮어쓰지 않습니다. 버전 변경 없이 기존 v0.6.2 태그를 재생성하지 마세요.

CI는 `tests/test_distribution.ps1`로 DLL 호환판과 ASI ZIP을 한글·공백 경로에 풀고
실제 DirectDraw import 연결을 검사합니다. 게임 설치 폴더는 사용하지 않습니다.
설치 및 DLL판에서 이전하는 절차는 [README](../README.md)와 [ASI 안내](../ASI-INSTALL.txt)를 따릅니다.
