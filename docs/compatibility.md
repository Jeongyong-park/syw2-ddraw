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

## 개발·비교용 별도 시험본

prepare.py는 기존 DLL을 보존하며 EXE 복사본의 import 이름 DDRAW.dll만 hqcdd.dll로 변경합니다.
이 복사본은 hqcdd.dll이 필요하므로 DLL을 ddraw.dll로만 배치한 기본 설치와 혼용하지 않습니다.
launch.ps1의 설치 기록·해시 검사는 이 별도 시험본에만 해당합니다.
기계 코드·HQNET 주소·DirectPlay 설정은 어느 방식에서도 변경하지 않습니다.
