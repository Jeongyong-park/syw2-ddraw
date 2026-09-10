# HQCDD — 조선의반격 전용 그래픽 래퍼

임진록 2: 조선의반격(SYW2Plus)의 오래된 DirectDraw 화면을 현대 Windows에서 표시하는 x86 그래픽 래퍼입니다.
팔레트 색상 문제와 HQNET 입력칸 덮어쓰기를 개선하고 창 모드·전체화면 전환과 게임 내 디스플레이 설정을 제공합니다.

**초기 시험 버전**입니다. 현재 버전은 [VERSION](VERSION), 변경 사항은 [CHANGELOG](CHANGELOG.md)를 확인하세요.
HQnet 연구 저장소에서 분리한 프로젝트이며 네트워크 접속·VPN 기능은 포함하지 않습니다.

## 주요 기능

- 8비트 팔레트, 16비트 RGB565, 32비트 화면 출력.
- GDI 호환 출력 또는 D3D11 하드웨어 색상 변환·확대.
- 화면 비율 유지, 창 크기 변경, 테두리 없는 전체화면.
- HQNET 로그인·채팅 입력창 보호와 화면 배율에 따른 위치·마우스 좌표 보정.
- 게임 내 설정 오버레이와 설정 파일 저장.

GPU 초기화·출력 실패 시 GDI로 전환합니다. 현재 기본 설정은 **GDI**이며 오버레이에서 GPU를 선택할 수 있습니다.
CPU 사용량과 FPS 개선 폭은 아직 비교 측정하지 않았습니다.

## 준비 환경

- Windows, Visual Studio의 C++ 데스크톱 개발 도구와 Windows SDK, CMake 3.24 이상.
- Python 3.10 이상(설치·패키징 도구).
- 사용자가 보유한 SYW2Plus 게임 파일. 충무공넷을 이용하려면 해당 접속 패치가 적용된 EXE.
- GPU 출력은 D3D11 feature level 11.0 하드웨어 필요.

DLL은 x86으로 빌드합니다. GDI 설정에서도 D3D11/DXGI/D3DCompiler 런타임 DLL은 로드 시점에 필요합니다.
런타임 자체가 없는 구형 Windows는 지원을 검증하지 않았습니다.

## 빌드와 실행

저장소 루트에서 실행합니다.

```powershell
.\build.ps1
python -m unittest discover -s tests -p "test_*.py" -v

python .\prepare.py "D:\syw2plus\조선의반격 오리지날 실행 충무공넷.exe" --output-dir ".\output\syw2-graphics-test"
& ".\output\syw2-graphics-test\launch.ps1"
```

`build.ps1`은 DLL을 빌드한 뒤 버전 속성과 네이티브 테스트를 검사합니다.
테스트 창이 잠깐 나타나며 GPU 테스트에는 대화형 데스크톱과 D3D11 하드웨어가 필요합니다.
산출물은 `build/Release/hqcdd.dll`입니다.

생성기는 아래 다섯 파일을 새 출력 폴더에 만듭니다. 이미 존재하면 덮어쓰지 않습니다.

| 파일 | 용도 |
| --- | --- |
| `… HQ그래픽.exe` | 그래픽 DLL 연결만 바꾼 게임 복사본 |
| `hqcdd.dll` | 전용 그래픽 래퍼 |
| `hqcdd.ini` | 디스플레이 설정 |
| `hqcdd-install.json` | 원본 경로와 생성 파일 해시 |
| `launch.ps1` | 해시 확인 및 원본 게임 폴더를 작업 디렉터리로 지정 |

**출력 폴더의 런처를 실행하세요.** 저장소 루트의 런처는 설치 기록이 없어 직접 실행할 수 없습니다.
원본 게임 폴더는 계속 필요합니다. 생성된 파일만으로 게임 전체가 설치되는 것은 아닙니다.

## 화면 조작

| 조작 | 동작 |
| --- | --- |
| Ctrl+Alt+D | 디스플레이 설정 열기·닫기 |
| Alt+Enter | 창 모드·전체화면 전환 |
| Esc / 닫기 버튼 | 오버레이 닫기, 적용하지 않은 선택 취소 |
| Tab / Shift+Tab | 설정 항목 이동 |
| Space / Enter | 설정 선택·버튼 실행 |
| F10 | 기존 게임 생산 단축키 유지 |

전체화면은 모니터 해상도를 바꾸지 않는 borderless 방식입니다.
창 모드로 돌아오면 이전 위치·크기를 복원합니다.

설정에서 화면 모드, GPU/GDI, 업스케일 방식, 수직동기화를 선택한 뒤 **변경 적용**을 누릅니다.
**다음 실행에도 저장**을 선택하면 INI에 저장합니다.
오버레이 동안 게임 입력은 차단하지만 **게임 진행은 멈추지 않습니다.**

## 설정 파일

`hqcdd.dll` 옆의 `hqcdd.ini`를 사용합니다. 직접 편집한 설정은 재실행 시 반영됩니다.

```ini
[Display]
Fullscreen=0
Renderer=gdi
VSync=0
Scaling=nearest
```

| 설정 | 값 |
| --- | --- |
| Fullscreen | `0`: 창 모드 / `1`: 전체화면 |
| Renderer | `gdi`: GDI / `auto`: GPU 사용, 실패 시 GDI |
| VSync | `0`: 끄기 / `1`: GPU 수직동기화 |
| Scaling | `nearest`, `bilinear`, `sharp-bilinear`, `integer` |

| 업스케일 방식 | 특성 | 지원 |
| --- | --- | --- |
| Nearest Neighbor | 원본 픽셀을 선명하게 확대 | GDI/GPU |
| Bilinear | 픽셀 사이를 부드럽게 보간 | GPU |
| Sharp Bilinear | 픽셀 내부를 유지하며 경계만 보간 | GPU |
| Integer | 1배·2배·3배 등 정수 배율로 표시, 남는 영역은 여백 | GDI/GPU |

GDI에서 GPU 전용 필터를 유지한 경우 실제 출력은 Nearest입니다.
Integer에서 창이 원본보다 작으면 화면 잘림을 피하도록 비율 유지 축소합니다.
기존 INI에 Scaling이 없으면 LinearFilter=1을 Bilinear로, 0을 Nearest로 읽습니다.
알 수 없는 Scaling 값은 Nearest로 처리합니다.

Sharp Bilinear는 정수 배율로 픽셀을 복제한 뒤 bilinear로 확대하는 원리를 직접 계산합니다.
Lossless Scaling과 출력 결과가 같다고 보장하지 않습니다.
LS1/FSR/NIS/SGSR/Anime4K/xBR 및 프레임 생성은 이번 구현에 포함하지 않습니다.
[Sharp Bilinear 참고 자료](https://github.com/rsn8887/Sharp-Bilinear-Shaders).

게임이 한 프레임에 여러 번 출력할 수 있어 수직동기화는 진행 속도에 영향을 줄 수 있습니다.
기본값은 끄기입니다. 래퍼는 게임의 속도 설정이나 타이머 값을 수정하지 않습니다.
관련 분석은 [게임 타이밍 분석](docs/graphics_timing.md)을 참고하세요.

## 업데이트와 복구

- 새 버전은 게임을 종료한 뒤 새 출력 폴더에 준비합니다. 사용 중인 DLL은 교체하지 않습니다.
- 생성 후 EXE나 DLL을 수동 교체하면 런처의 해시 검사가 실패합니다. 해당 버전의 생성기로 다시 준비하세요.
- INI는 사용자 설정이므로 해시 검사 대상에서 제외합니다.
- 원래 방식으로 실행하려면 기존 EXE를 실행합니다.
- 제거할 때는 게임 종료 후 생성된 시험 폴더를 삭제합니다. 원본 옆에 설치했다면 설치 기록에 있는 생성 파일과 INI·로그만 제거합니다.

원본 EXE와 기존 `ddraw.dll`, DxWnd/dxwrapper 설정은 변경하지 않습니다.
시험본은 기존 dxwrapper와 그 DLL이 불러오던 **syw2x 플러그인을 자동 로딩하지 않습니다.**

## 배포와 버전

```powershell
python .\package.py
```

현재 VERSION에 맞는 ZIP을 `output/`에 생성합니다. 소스·문서·x86 Release DLL을 포함하며 게임 파일과 Git 내부 파일은 제외합니다.
패키징 명령은 먼저 DLL 속성의 버전이 VERSION과 일치하는지 검사합니다. VERSION 변경 후에는 다시 빌드하세요.

[SemVer 정책과 배포 절차](docs/versioning.md)를 따릅니다.
DLL 속성의 자세히 탭에서 제작자(회사), 제품명, 파일·제품 버전과 저작권 정보를 확인할 수 있습니다.
메타데이터는 코드 서명을 의미하지 않습니다.

## 검증 범위

로컬 네이티브 테스트는 COM 수명, 팔레트·표면 복사, GDI 입력칸 보호, 창 전환,
오버레이 커서·입력 복원, F10 전달과 GPU 출력 픽셀을 검사합니다.
CI는 Windows x86 빌드, DLL 메타데이터, 설치·패키징 도구를 검사합니다.
CI 빌드 성공이 실제 GPU 실행 테스트를 대신하지는 않습니다.

실제 게임 메인 화면·HQNET 입력칸·로비 출력과 사용자 한글 전환 보고를 확인했습니다.
장시간 멀티플레이, IME 전체 조합, 다중 모니터/DPI, GPU·Windows별 호환성 및 원본 대비 전투 속도 비교는 추가 검증이 필요합니다.
모든 DirectDraw 게임을 지원하는 범용 래퍼는 아닙니다.

## 개발 자료와 라이선스

- [확인한 게임 빌드와 패치 원리](docs/compatibility.md)
- [게임 타이밍 분석](docs/graphics_timing.md)
- [변경 이력](CHANGELOG.md)
- [버전 관리](docs/versioning.md)

MIT License — Copyright (c) 2026 Park Jeongyong. [LICENSE](LICENSE) 참조.
게임 파일과 DxWnd/dxwrapper 코드는 포함하지 않습니다.
