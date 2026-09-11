# ESL 배포판의 SYW2X 로더와 HQCDD 검토

2026-09-11 기준 로컬 ESL 배포판과 [SYW2X v0.2.3.3a 게시글](https://m.cafe.naver.com/ca-fe/web/cafes/30560749/articles/118)을 확인했다.
게시글은 일반 웹 검색 도구에서는 열리지 않았지만, 인증 없이 공개 게시글 API에서 본문을 읽었다.

이 문서는 ASI 구현 전의 조사 기록이다. 아래의 체인 방식은 당시 검토 후보이며,
현재 구현과 831×624 확장 검증 결과는 [ASI 통합 문서](asi-integration.md)를 따른다.
현재 실험판은 `plugins/hqcdd.asi`를 사용하며 `ddrawHooked.dll`과 함께 설치하지 않는다.

## 확인된 설치 충돌

게시글의 2026-03-05 변경 내역은 ASI 로더용 `ddraw.dll` 추가와 기존 플러그인의
`plugins/syw2x.asi` 이동을 설명한다. 이 로더가 plugins 아래 플러그인을 자동 연결한다.
또한 기존 dxwrapper 연동을 해제하고, DxWnd의 추가 모듈에 등록된 `syw2x.dll`을
제거하도록 안내한다. 게시글은 해당 구조 변경에서 플러그인 기능 자체는 바뀌지 않았다고 설명한다.

HQCDD도 게임 옆의 `ddraw.dll`로 배치된다. 따라서 두 DLL을 같은 경로에 그대로
설치할 수 없으며, HQCDD로 덮어쓰면 SYW2X의 ASI 로더 역할이 사라질 수 있다.
현재 HQCDD에는 plugins 폴더를 탐색하거나 `syw2x.asi`를 로드하는 코드가 없다.
반대 방향으로 ASI 로더를 복원하면 HQCDD가 로드되지 않을 수 있다.
단순 DLL 이름 변경이나 DxWnd 병행 실행을 검증된 해결책으로 제시하지 않는다.

## 로컬 파일 및 실행 증거

- 원본 검사 위치: `D:\[ESL]Syw2plus`.
- `plugins/syw2x.asi`: 22,016바이트, SHA-256
  `a320872eedd8b94b19ffb57886bc372335fed721770c73ddac63fcd6bd528895`.
- 원본과 기존 벤치마크 사본 모두 해당 플러그인과 `syw2x.ini`를 포함한다.
- `ViewPortPlusOn=0`, `EmptyBarFillOn=1`이다. 다른 항목이 0이어도 플러그인 전체가
  꺼졌다는 의미는 아니며, 파일 존재 역시 실제 로드를 보장하지 않는다.
- 원본 `hqcdd.ini`는 `VSync=1`이다. 앞선 전장 벤치마크는 별도 사본에서 OFF로 실행했으므로
  원본 실행과 동일한 설정이라고 가정하면 안 된다. 원본 설정은 변경하지 않았다.
- 기존 `output/osd-preview-20260911/game` 사본을 직접 실행하고 32비트 모듈을 포함한
  Toolhelp 목록을 수집했다. 게임 옆 HQCDD `DDRAW.dll`은 로드됐지만 `syw2x.asi`,
  `syw2x.dll`, `dxwnd.dll`은 목록에 없었다. 메뉴 시점의 관측이며 이전 실행의 소급 증명은 아니다.
- Windows의 `ddraw.dll`도 목록에 있지만, HQCDD의 시스템 DirectDraw 위임 경로가 있으므로
  이것을 SYW2X나 DxWnd 동시 로드 증거로 취급하지 않는다.
- 정적 import에 `VirtualProtect`, `CreateThread`, `Sleep`가 있으나, 이 사실만으로
  게임 타이머 후킹이나 랙의 원인을 확정하지 않는다.

원시 자료는 로컬 `output/syw2x-review`의 `article.json`, `pe-analysis.json`과 세션의
직접 실행 모듈 출력에 남겼다. `modules.json`은 후속 체인 실험에서 갱신됐으므로 최초
직접 실행 목록으로 사용하지 않는다. 외부 DLL·플러그인 바이너리는 저장소에 추가하지 않았다.
조사용 게임은 종료했다.

## 결론과 남은 검증

동일한 `ddraw.dll` 경로를 사용하는 **설치 및 로딩 충돌은 확인했다.** 이번 직접 실행에서
두 플러그인이 동시에 실행됐다는 증거는 없으므로, 애니메이션 정지·급가속을 SYW2X와
HQCDD의 런타임 충돌로 확정할 수 없다. 앞선 성능 테스트 역시 SYW2X ON 상태의
호환성 테스트라고 부르면 안 된다.

공존 지원을 구현하려면 원래 ASI 로더의 연결 방식 또는 명시적인 플러그인 로딩 방식을
검토하고, 실제 로드 모듈을 증거로 남긴 ON/OFF 비교가 필요하다. 동일 세이브·VSync·해상도를
유지해 비교해야 한다. 해상도 확장(동봉 설명서의 831×624 및 DxWnd 요구)은 별도 조건으로
검증해야 하며 현재는 확장이 꺼진 상태만 확인했다.

## 공존 방식 후속 검토

### 권장 후보: 기존 ASI 로더의 체인 기능

공개 게시글의 분할 ZIP에서 `ddraw.dll`만 CRC 검증 후 추출했다. 버전 리소스는
ThirteenAG Ultimate ASI Loader 9.7.0, GitHash `f9f6ccf`다. 로더 SHA-256은
`ad5d8d449ce305caf62cb17d8698ebf17e94f9120ee993d0dd85211dfe14f8dd`다.

[해당 커밋 소스](https://github.com/ThirteenAG/Ultimate-ASI-Loader/blob/f9f6ccf/source/dllmain.cpp#L687)는
게임 옆 `ddrawHooked.dll`이 존재하면 그 DLL을 DirectDraw 위임 대상으로 선택한다.
이는 [공식 문서의 Hooked DLL 체인 방식](https://github.com/ThirteenAG/Ultimate-ASI-Loader#description)과 일치한다.

```text
게임 폴더/
  ddraw.dll          ← ESL 동봉 Ultimate ASI Loader 9.7.0
  ddrawHooked.dll    ← HQCDD의 hqcdd.dll을 이 이름으로 복사
  hqcdd.ini         ← HQCDD 설정
  syw2x.ini         ← SYW2X 설정 (검토 시 ViewPortPlusOn=0)
  plugins/syw2x.asi  ← 기존 SYW2X
```

로더가 DirectDraw 호출을 HQCDD에 전달하고 SYW2X를 별도로 로드한다. HQCDD는 자신의
모듈 경로를 기준으로 설정·로그를 찾으므로 DLL 이름 변경 후에도 같은 게임 폴더의
`hqcdd.ini`, `hqcdd.log`를 사용한다. HQCDD의 시스템 DirectDraw 위임은 System32의
절대 경로를 사용하므로 게임 옆 ASI 로더로 재귀 진입하는 구조가 아니다.

`output/syw2x-chain-review/game`의 별도 사본에서 이 배치를 실제 실행했다.
`modules.json`에 ASI 로더 `DDRAW.dll`, HQCDD `ddrawHooked.dll`, `plugins/syw2x.asi`가
동시에 확인됐다. HQCDD 로그와 메뉴 화면도 확인한 뒤 정상 종료했다.
이 실험의 HQCDD SHA-256은
`d13c8a2277438e5e01e2ab459f8e901e91e7ccabcd6a607c9c99cd64152373db`다.
**메뉴 시작과 동시 로딩만 검증했으며, 전장 기능·성능 및 모든 SYW2X 기능의 호환성을
보증하는 결과는 아니다.** 원본 설치와 저장소 구현은 변경하지 않았다.

### 다른 후보와 판단

| 방식 | 판단 |
|---|---|
| 기존 ASI 로더 → ddrawHooked.dll(HQCDD) | 우선 채택 후보. 기존 배포 로더 활용, HQCDD 로딩 코드 추가 불필요. 메뉴 동시 로딩 확인. |
| HQCDD가 특정 syw2x.asi를 명시적으로 로드 | 대안. 초기화 시점·중복 로드·의존 DLL 실패·패치 대상 EXE 확인과 로그를 직접 관리해야 한다. |
| ASI 로더를 dinput8.dll 등 다른 이름으로 배치 | 게임이 해당 DLL을 실제로 로드해야 하므로 단순 이름 변경만으로 보장할 수 없다. 현재 권장하지 않음. |
| DxWnd도 함께 실행 | 추가 화면·입력 후킹이 생겨 원인 분리가 어려워진다. 공존의 필수 구성에서 제외. |

직접 로딩을 구현한다면 HQCDD의 `DllMain` 안에서 `LoadLibrary`를 호출하지 않는다.
[Microsoft DLL 초기화 지침](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)에
따라 loader lock 밖에서 초기화해야 한다. 현재 `DirectDrawCreateEx`는 래퍼 mutex를 잡으므로
그 안에 무조건 로딩 코드를 넣는 방식도 재진입·스레드 초기화를 검토해야 한다.
플러그인의 메모리 패치와 스레드가 남는 동안 임의 `FreeLibrary` 또는 실행 중 OFF 전환을
지원한다고 가정하지 않는다.

### 배포 반영 전 조건

1. 동일한 37창병 세이브·GPU·Sharp Bilinear·VSync OFF·800×600에서 SYW2X OFF/ON 비교.
   양쪽 모두 ASI 로더와 HQCDD 체인을 유지하고 플러그인 유무만 바꿔 로더 비용을 통제한다.
2. 실제 모듈 경로·해시를 기록하고, 커서 이동·집단 이동·저장/불러오기·OSD·전체화면 전환을 확인한다.
3. 831×624 확장 및 기존 패널 데이터는 별도 검증한다. 현 단계에서 확장을 자동으로 켜지 않는다.
4. 준비/설치 도구가 기존 로더를 식별하고 보존하도록 변경해야 한다. 현재 도구처럼
   `ddraw.dll`을 HQCDD로 덮어쓰면 체인 구성이 깨진다. 기존 파일 백업과 복구도 함께 설계한다.
5. 로더의 추가 export가 HQCDD에서 구현되지 않은 API를 요구하는 경로는 별도 확인한다.
   메뉴 실행 성공을 게임의 모든 DirectDraw 호출 호환성으로 확대하지 않는다.
