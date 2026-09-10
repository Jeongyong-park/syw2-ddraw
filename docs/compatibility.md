# 확인한 게임 빌드와 DLL 로딩 방식


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

운영 배포 전에는 HQNET 채팅·한글 IME, 동영상, 관전 포함 장시간 멀티플레이를 추가 확인해야 한다.
운영체제별 수동 검증과 GPU·DPI·다중 모니터 환경별 시험은 1.0 이후 후속 작업으로 연기한다.
Windows 11 동작은 사용자 확인이 있으나 상세 시험 기록은 없고 Windows 10은 장비가 없어 미검증이다.
미검증 환경에 대한 호환성을 확언하지 않는다.


## 기본 사용자 설치: EXE 수정 없음

확인한 게임 EXE는 DDRAW.dll의 DirectDrawCreateEx를 가져옵니다.
HQCDD를 EXE 옆에 ddraw.dll로 배치하면 기존 EXE의 import를 그대로 사용할 수 있습니다.
기존 게임 폴더의 ddraw.dll은 먼저 백업합니다. 시스템 폴더의 DLL은 변경하지 않습니다.

DLL을 ddraw.dll로 바꾼 네이티브 통합 테스트에서 로드·COM 객체·클리퍼·출력·입력을 확인했습니다.
실게임의 운영체제별 DLL 교체 설치·업데이트·복구 검증은 1.0 이후 항목으로 남아 있습니다.
다른 DirectDraw export를 요구하는 미확인 게임 빌드까지 지원하는 것은 아닙니다.

AMStream은 영상 파일을 열기 전 DLL 로딩 단계에서 DDRAW.dll의 DirectDrawCreate를 요구합니다.
이 레거시 함수는 절대 시스템 경로의 ddraw.dll로 위임하여 실제 IDirectDraw 객체를 반환합니다.
게임의 DirectDrawCreateEx/IDirectDraw7 경로는 HQCDD 구현을 유지합니다.
영상 없는 AMStream 로딩 및 레거시 객체 생성 검사는 실제 영상 재생·코덱 호환성 검증을 대신하지 않습니다.

## ESL 2606 클라이언트 정적 검토 (2026-09-10)

제보자와 같은 버전으로 제공된 `[ESL]Syw2plus 2606.exe`를 읽기 전용으로 분석했다.
크기는 1,032,192바이트이며 SHA-256은
`716dde6a1cd837c74c83c426fba634491143d624c0c284aad4dc4d5d5ea2417f`이다.
위의 기존 검증 EXE와 해시는 다르므로 동일 바이너리로 취급하지 않는다.

이 EXE도 DDRAW.dll에서 DirectDrawCreateEx만 직접 import한다.
import 이름의 파일 오프셋은 `0xebb7e`, IAT 주소는 `0x4e5018`이며,
`0x4e5928`의 GUID는 IDirectDraw7이다. prepare.py의 import 재지정 검증은
메모리에서 통과했다. EXE 파일은 수정하지 않았다.

영상 초기화 함수 `0x4697a0`의 호출 순서는 다음과 같다.
주소는 이미지 기준 주소 `0x400000`에서의 가상 주소다.

| 호출 위치 | 동작 |
| --- | --- |
| `0x4697d9` | CoCreateInstance로 CLSID_AMMultiMediaStream 생성, IID_IAMMultiMediaStream 요청 |
| `0x469814` | IAMMultiMediaStream::Initialize |
| `0x469839`, `0x469858` | AddMediaStream |
| `0x469877` | OpenFile로 영상 파일 열기 |

CLSID는 `49c47ce5-9ba4-11d0-8212-00c04fc32c45`, IID는
`bebe595c-9a6f-11d0-8fde-00c04fd9189d`다. GUID와 메서드 순서는 설치된
Windows SDK 10.0.26100.0의 amstream.h에 대조했다. 로컬 32비트 COM 등록은
이 CLSID를 `C:\Windows\SysWOW64\amstream.dll`로 연결하며, 해당 DLL은
DDRAW.dll의 DirectDrawCreate를 import한다.

따라서 영상 파일을 열기 전에 AMStream 로딩이 발생하는 경로가 있다.
영상 삭제만으로 이 경로의 DLL 의존성이 사라지지 않으며, v0.6.1의
DirectDrawCreate export 누락은 제보된 시작 지점 오류를 설명한다.
다운로드한 폴더에는 OPENPLUS.MPG, ENDINGPLUS.MPG, hqteamlogo2.mpg가 남아 있어
영상이 제거된 제보자 설치와 폴더 구성이 동일하다고 볼 수는 없다.

동봉된 ddraw.dll의 버전 정보는 Ultimate ASI Loader 9.7.0
(Ultimate-ASI-Loader-Win32)이며 HQCDD가 아니다. HQCDD로 교체하면 기존 로더의
플러그인 로딩 기능도 교체되므로, 해당 플러그인의 호환성은 별도 확인 대상이다.

정적 검토 후 원본 설치를 복사한 별도 시험 폴더에 수정 DLL을 배치하여 실행했다.
시작 지점 오류 없이 메인 메뉴 진입과 해당 프로세스의 HQCDD 로그를 확인했으며,
사용자도 정상 동작으로 보인다고 확인했다. 원본 설치는 변경하지 않았다.
전체 게임 진행·영상 재생·플러그인 동작까지 검증한 것은 아니다.

## 개발·비교용 별도 시험본

prepare.py는 기존 DLL을 보존하며 EXE 복사본의 import 이름 DDRAW.dll만 hqcdd.dll로 변경합니다.
이 복사본은 hqcdd.dll이 필요하므로 DLL을 ddraw.dll로만 배치한 기본 설치와 혼용하지 않습니다.
launch.ps1의 설치 기록·해시 검사는 이 별도 시험본에만 해당합니다.
기계 코드·HQNET 주소·DirectPlay 설정은 어느 방식에서도 변경하지 않습니다.
