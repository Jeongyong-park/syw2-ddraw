# 확인한 게임 빌드와 패치 원리


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


prepare.py는 원본을 보존하고 PE import 이름 DDRAW.dll만 hqcdd.dll로 변경합니다.
