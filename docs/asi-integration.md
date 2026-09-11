# HQCDD ASI 실험판

## SYW2X 설정 탭

Ctrl+Alt+D의 SYW2X 탭은 게임 옆 `syw2x.ini`를 편집한다. 전장 확장·패널 글자색·빈 상태바·
내 유닛 식별 ON/OFF와 팔레트 번호 8개를 제공한다. `MyBrightIFF`만 4바이트 값이며 나머지
색상은 0~255다. 상태는 플러그인 파일 존재와 현재 로드를 구분하며 값을 즉시 적용하지 않는다.
저장 버튼은 이 탭의 값만 저장하고, 디스플레이의 저장 체크나 렌더러 설정을 바꾸지 않는다.
탭을 바꿔도 편집값을 유지하고 저장하지 않고 닫으면 파일을 변경하지 않는다.

저장은 원본을 같은 폴더의 임시 파일로 복사한 뒤 변경한 키만 갱신하고 원자적으로 교체한다.
알 수 없는 키·섹션을 보존하고 외부 편집·읽기 실패·범위 오류 시 저장을 거부한다.
색상 견본은 원본 `syw2xconfig.exe`의 `ColorPaletteForm.Palette`와 동일한 고정 256색 표를 쓴다.
입력값이 잘못되면 견본에 `?`를 표시하며 선택을 막는다.
견본 클릭으로 16×16 팔레트를 열고 클릭 또는 방향키+Enter/Space로 선택한다. Esc/뒤로는 취소한다.
선택 결과는 입력란에만 반영하고 저장은 별도로 수행한다. 4색은 16진수 표기의 왼쪽 바이트부터
표시하고 선택한 바이트만 바꾼다. 실제 게임의 명암 단계 순서를 의미하지 않는다.
실제 게임 팔레트는 장면에 따라 바뀔 수 있으므로 이 표가 모든 장면의 최종 RGB를 보장하지는 않는다.
색상 번호와 4색 순서는 원본 설정기와 동일하다. `0x44=RGB(4,200,4)`, `0xFB=RGB(188,188,192)`.
대조한 원본 설정기 SHA-256: `8f4bb2f3b0849b5cb87dad37725e3ddb47e60461d2c30a8af8849cdfd2535c2a`.
어셈블리 버전은 1.0.0.0이며 실행 코드를 가져오지 않고 인덱스/RGB 호환성 데이터만 기록했다.

GPU 없이 실행하는 `syw2x_test`에서 파일 보존·범위·외부 변경·읽기 전용 파일과
팔레트 RGB 표시·클릭·키보드 선택·취소·잘못된 입력·4색의 나머지 바이트 보존을 검증하고,
래퍼 통합 테스트에서 탭 전환과 게임 입력창 클리핑을 함께 확인한다.
네이티브 테스트 9개와 Python 테스트 54개를 통과했다. 게임과 분리한 x86 미리보기 호스트에서
ASI의 실제 설정창 배치를 확인했다. 이 호스트의 플러그인 파일은 설치 상태 표시용 더미이며
SYW2X를 실행하거나 게임 설정을 변경하지 않는다. 초기 팔레트 UI 검증에는 합성 256색을 썼으나,
최종 UI는 원본 SYW2X의 고정 팔레트로 변경했다. 실제 게임 재시작 후 기능 적용 검증과는 별도다.

일반 DLL판과 같은 렌더링·팔레트·입력창·OSD 코어를 ASI 플러그인으로 빌드한다.
ESL의 기존 ASI 로더와 SYW2X를 보존하려는 구성이다. 구현 브랜치의 실험판이며
버전 번호가 같아도 기존 v0.6.2 릴리즈에 포함된 기능은 아니다.

```text
게임 폴더/
  ddraw.dll           기존 Ultimate ASI Loader
  hqcdd.ini           HQCDD 설정
  syw2x.ini           SYW2X 설정
  plugins/
    hqcdd.asi         이번 빌드
    syw2x.asi         기존 플러그인
```

기존 DLL판으로 로더를 덮어쓴 설치는 원래 로더 복원이 필요하다.
`ddrawHooked.dll` 체인 방식과 동시에 사용하지 않는다. 설정은 게임 EXE 옆에 유지하며
기존 `hqcdd.ini`를 덮어쓰지 않는다. 상세 설치·복구 절차는 `ASI-INSTALL.txt`와
ASI ZIP의 `INSTALL.txt`를 참고한다. ASI 로더와 SYW2X 바이너리는 배포물에 포함하지 않는다.

## 연결 방식

`DllMain`은 모듈 핸들만 기록한다. ASI 로더가 `InitializeASI`를 호출하면 최초 한 번만
게임 EXE의 명명된 `DDRAW.dll!DirectDrawCreateEx` IAT 슬롯을 연결한다.
실행 파일 자체, 다른 모듈의 IAT, 전역 API 진입점, 게임 타이머는 변경하지 않는다.
기존 `DirectDrawCreateEx` 구현으로 IDirectDraw7 객체를 생성하므로 팔레트·표시·GDI 컨트롤
처리는 DLL판과 같은 코드다. AMStream 등의 레거시 DirectDraw는 기존 로더/시스템 경로를 따른다.

동봉 Ultimate ASI Loader 9.7.0은 시작 API의 IAT 후킹을 복원한 뒤 플러그인을 초기화한다.
실제 ESL에서 첫 DirectDraw 생성 전에 ASI가 연결되는 것을 로그로 확인했다.
지연 import·ordinal import·동적 GetProcAddress 연결은 지원하지 않으며,
게임 초기화가 끝난 뒤 임의 주입하는 방식은 지원하지 않는다.

기존 HQCDD DLL/체인 모듈, 알 수 없는 DDRAW 제공자, 이미 바뀐 IAT 슬롯,
지원하지 않는 import 구조는 연결하지 않고 `ASI disabled` 이유를 기록한다.
로더는 `IsUltimateASILoader` export로 식별하며, 수동 테스트 호스트의 시스템 DDRAW도 허용한다.
이 식별은 바이너리 서명/버전 인증이 아니다. 성공한 콜백은 프로세스 수명 동안 모듈을 pin해
유효하게 유지한다. 실행 중 언로드·OFF 전환은 제공하지 않는다.

ASI가 `plugins` 폴더에 있으면 설정과 로그를 그 부모 게임 폴더에 둔다.
직접 게임 폴더에 두는 경우에도 동일 폴더를 사용하지만 검증 배치는 `plugins/hqcdd.asi`다.

## 빌드·검증

```powershell
rtk proxy cmake --build build --config Release
rtk proxy ctest --test-dir build -C Release --output-on-failure
rtk proxy python -m unittest discover -s tests -p test_*.py
rtk proxy python package.py --asi
```

빌드 결과는 `build/Release/hqcdd.dll`과 `build/Release/hqcdd.asi`다.
ASI ZIP은 `output/syw2-ddraw-v<버전>-asi.zip`이며 `ddraw.dll`을 포함하지 않는다.
기존 `prepare.py`의 EXE import 변경 기능과 성능 실행기의 DLL 교체 방식은 ASI 설치에
사용하지 않는다. 자동 릴리즈 게시 스크립트는 ASI ZIP과 체크섬만 게시한다.
일반 DLL 및 개발용 ZIP은 CI 호환성 산출물로 유지한다. 다음 새 태그부터 적용하며 기존 릴리즈는 덮어쓰지 않는다.

네이티브 테스트는 DLL 및 ASI 코어 각각의 팔레트, GPU/GDI, EDIT 위치·클리핑,
설정창·포커스·종료 처리를 검사한다. 실제 DDRAW import를 가진 별도 테스트 EXE는
DllMain에서 연결하지 않는 점, InitializeASI의 반복 호출, 실제 DD7 라우팅,
모듈 pin과 기존 DLL 중복 거부를 검사한다.
이미 가로챈 IAT를 덮어쓰지 않는 회귀 검사도 포함하며, 네이티브 8개와 Python 52개 테스트가 통과했다.
최종 보호 조건을 반영한 빌드로 ESL을 다시 실행해 `ASI active`와 GPU hardware 출력을 재확인했다.

2026-09-11 별도 사본 `output/asi-esl-smoke/game`에서 다음을 확인했다.

- 기존 ASI 로더, `hqcdd.asi`, `syw2x.asi` 동시 로드 (`modules.json`).
- 게임 폴더 `hqcdd.log`의 `ASI active`, `protection_restored=1`, GPU hardware 출력.
- 800×600 전장 37창병 세이브 불러오기, 집단 선택·이동, OSD 갱신, 디스플레이 설정 열기.
- SYW2X 우상단 게임 시간 표시. `ViewPortPlusOn=0`, GPU/Sharp Bilinear/VSync OFF.

이번 실행은 기능 스모크 테스트이며 통제된 성능 비교가 아니다. ASI 전환으로 애니메이션
정지·급가속이 해결됐다고 판정하지 않는다. 실제 온라인 로그인·채팅 입력과
SYW2X 전체 기능, OS별 수동 호환성은 미검증이다. 온라인 통신 없이 GDI EDIT 회귀 테스트를
실행했으며 OS별 수동 검증은 기존 방침대로 1.0 이후 진행한다.

### 해상도 확장 후속 확인

같은 날 테스트 사본의 `ViewPortPlusOn=1`로 재시작하고 37창병 세이브를 불러왔다.
메뉴는 800×600, 전장 진입 후 `SetDisplayMode 831x624 8-bit`가 기록됐다.
OSD에서도 D3D11 / Sharp Bilinear / VSync OFF / 831×624를 확인했다.
부대 드래그 선택, 우클릭 이동 후 위치 변화, 하단 설정 버튼의 게임 메뉴 열기를 확인했다.
SYW2X 시간 표시도 갱신됐다. `viewport-modules.json`은 실제 로드 모듈 기록이며,
`output/asi-esl-smoke/viewport-battle.png`와 `viewport-hqcdd.log`에 증거를 보존했다.
이번 구성은 DxWnd 없이 실행했다. 동봉 설명서의 DxWnd 요구를 이 ASI 구성의 필수 조건으로
보지 않는다. 임의 해상도·4K 내부 렌더링, 전체화면 확장, SYW2X의 다른 옵션 전체를
검증한 것은 아니다. 게임 종료 후 테스트 사본 설정을 복원했으며 원본은 변경하지 않았다.
