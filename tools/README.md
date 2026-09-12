# 개발 도구

| 위치 | 용도 |
| --- | --- |
| `perf/` | 출력·입력 보고서, 성능 비교 실행, 읽기 전용 게임 틱 관측 |
| `widescreen/` | 전장 패치 조사·시제품 생성·명세 생성·에뮬레이션 |
| `release.py` | 릴리즈 자산 검사 및 명시적 게시 |
| `re_graphics_timing.py` | 기존 읽기 전용 타이밍 디스어셈블 도구 |

기능 변경은 패키지 안의 구현 파일에서 합니다. 루트의 동일 이름 파일은
기존 문서·스크립트의 CLI 및 공개 함수 import를 위한 호환 진입점입니다.
`tools/release.py`와 `tools/re_graphics_timing.py`는 이번 이동 대상이 아닙니다.

저장소 루트에서 두 방식 모두 사용할 수 있습니다.

```powershell
python tools/perf_matrix.py --help
python -m tools.perf.perf_matrix --help
python tools/export_widescreen_patch.py --help
python -m tools.widescreen.export_widescreen_patch --help
```

다른 작업 폴더에서는 기존 진입점의 절대 경로로 실행합니다. 패키지 안의 파일을
직접 실행하는 대신 `python -m`을 사용합니다. CLI 옵션과 상대 입력·출력 경로의
기준은 기존처럼 현재 작업 폴더입니다. 시제품 생성기는 별도로 저장소의 `output/`
아래 게임 사본만 허용합니다.

성능 실행기의 `runner_sha256`과 `tools_sha256`은 `perf/` 안의 실제 구현 파일을
해시합니다. 호환 진입점 파일의 해시가 아니며, 기존 결과 JSON의 키는 유지합니다.
와이드 패치 헤더의 생성 명령은 기존 진입점을 계속 사용할 수 있습니다.

보고서 도구와 `--help`에는 게임 파일이 필요 없습니다. 실제 와이드 분석·생성에는
도구별로 pefile, Capstone, Unicorn 및 지원 게임 원본이 필요합니다.
게임 실행, 커서 이동, 시제품 생성 등의 효과는 각 도구의 실행 옵션을 확인합니다.
패키지 import나 도움말만으로 게임을 실행하지 않습니다.
