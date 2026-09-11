# 성능 진단 및 자동 검사

## 현재 구현 범위

- Unicorn: 실제 viewport·클램프 코드 3,852개 결정적 시나리오. 확대·축소·여백·경계 클릭·연속 이동·정지·반전을 검사한다.
- Windows 내부 계측: QPC 기반 CPU 소요 시간과 호출 시각 CSV. 기본 비활성화.
- 비교 실행기: 별도 게임 복사본에서 렌더러/필터/VSync/창·전체화면 조합을 반복하고 조건별 JSON 생성.
- 분석기: p50/p95/p99·최대 시간, 출력 API 호출 간격과 호출률. 표시 FPS와 입력→화면 지연은 null.

Unicorn의 DPI 시나리오는 주어진 크기의 산술 검증이며 Windows DPI API를 에뮬레이션하지 않는다.
가상 시간에 따른 EXE 내부 게임 틱·커서 그리기 검증은 아직 추가하지 않았다.
시뮬레이션 속도와 GPU 작업 시간은 이번 내부 CPU 계측에서 측정하지 않는다.

## 자동 검사

```powershell
rtk proxy powershell.exe -NoProfile -File build.ps1
rtk proxy python tests/unicorn_viewport.py --report output/unicorn-viewport.json
rtk proxy python -m unittest discover -s tests -p test_*.py -v
rtk proxy python tests/perf_smoke.py
```

마지막 검사는 Windows 대화형 데스크톱과 하드웨어 GPU가 필요하다.
폐기 가능한 DLL 복사본으로 계측 OFF/ON, GPU·GDI·입력 기록 및 종료 시 CSV 저장을 확인한다.
이 검사의 실행 시간을 실제 게임 성능이나 계측 오버헤드의 기준선으로 사용하지 않는다.
CI는 Unicorn JSON을 별도 아티팩트로 보관하며, 네이티브 성능 측정은 실행하지 않는다.
Unicorn 실패 시에도 오류와 재현 정보가 담긴 JSON을 보존한다.

## 계측 활성화

CSV footer의 QPC는 기록 종료 시각이다. 비교 구간 종료보다 기록이 먼저 닫히면
`covers_capture_end=false`로 표본을 무효화한다. DirectDraw 객체 해제나 창 파괴로
프로세스 전체 계측이 일찍 종료되는 경우를 검출하며 자동 재시작은 하지 않는다.
종료 시각이 0인 과거 CSV는 `covers_capture_end=null`로 보존한다. 이 과거 표본은
구간 끝까지 기록됐다는 검증이 없으므로 새 검증을 통과한 것으로 해석하지 않는다.

시험 프로세스 환경에 `HQCDD_PERF_FILE`을 절대 CSV 경로로 지정한다.
부모 디렉터리는 미리 만들고 기존 결과와 겹치지 않는 파일명을 사용한다.
정상 창 종료 또는 DirectDraw 객체 해제 시 저장하며 강제 종료에서는 파일이 없거나 불완전할 수 있다.
게임의 입력·계정·채팅 문자열은 기록하지 않는다. 이벤트 시각과 좌표는 기록한다.

고정 메모리 버퍼에는 최대 262,144개 이벤트를 보관한다. 초과분은 trace_dropped에 기록하며
분석기는 데이터 손실이나 종료 footer가 없는 기록을 유효하지 않다고 판정한다.
긴 기록에서 용량을 넘으면 우선 측정 시간을 줄인다. 프로세스당 첫 진단 세션만 지원한다.
계측 ON은 메모리와 이벤트 잠금 비용이 있으므로 실제 비교 시 ON/OFF 오버헤드를 별도 측정해야 한다.

| 이벤트 | 의미 |
| --- | --- |
| output_attempt / output_skipped | 출력 요청·생략. 생략 a=1 재진입, 2 레이아웃, 3 주 표면 없음, 4 표면 잠금, 5 창 없음, 6 최소화 |
| request_flip | Flip에 의한 전체 화면 출력 요청 |
| request_blt / request_blt_fast | primary 대상 Blt/BltFast 출력 요청과 경계 안으로 잘린 요청 면적 |
| request_unlock | primary의 쓰기 Lock 영역을 Unlock한 출력 요청. 읽기 전용은 제외 |
| request_release_dc | primary DC 반납에 의한 출력 요청. 변경 영역을 알 수 없어 전체 면적 사용 |
| request_set_palette / request_palette_entries | primary 팔레트 연결/엔트리 변경에 의한 전체 화면 출력 요청 |
| request_repaint | 창 다시 그리기·레이아웃·설정 변경 등 나머지 출력 요청 |
| child_layout | 자식창 동기화 CPU 시간 |
| gpu_output / gpu_init / gpu_upload | GPU 경로 전체 및 초기화·업로드 CPU 시간 |
| gpu_present / gpu_present_result | Present CPU 시간 및 HRESULT. a,b는 VSync, scaling(0~3) |
| gdi_rgb / gdi_blit | CPU RGB 변환 및 StretchDIBits 호출 시간 |
| mouse_lock_wait / cursor_lock_wait | 마우스 window procedure·GetCursorPos 경로의 mutex 획득 대기 |
| mouse_dispatch / mouse_mapped | 게임 procedure 처리 시간과 변환 좌표 |
| cursor_query / cursor_query_mapped | GetCursorPos 래퍼 시간과 반환 좌표 |
| vertical_blank_wait | WaitForVerticalBlank 실제 CPU 경과 시간 |

이벤트는 종료 순서로 들어갈 수 있다. 분석기는 호출 간격 계산 전에 시작 시각으로 정렬한다.
동일 시각의 최신 마우스 이벤트가 특정 화면 프레임에 반영됐다고 추정하지 않는다.
GPU CPU 시간에는 GPU에서 실행이 완료되는 시간과 모니터 표시 시간이 포함되지 않을 수 있다.

```powershell
rtk proxy python tools/perf_report.py output/perf-smoke/trace.csv --output output/perf-summary.json
```

`call_rate_hz`는 API 호출률이다. 표시 FPS가 아니다. `valid`는 trace 무결성 판정이며
제어된 성능 시험 또는 커서 반응 개선을 보장하지 않는다.

`request_*`의 a/b는 요청 면적(픽셀 수)/primary 전체 면적이다. 출력 시도별 원인 scope이므로
렌더링을 생략한 요청도 포함되며 CPU 시간은 `output_attempt`와 중첩된다. 두 시간을 합산하지 않는다.
primary 표면이 없거나 이전 버전의 면적 없는 기록은 a=b=0으로 남고 알 수 없는 면적으로 집계한다.
팔레트·DC 반납의 전체 면적은 보수적 범위이며 실제 변경 픽셀 수가 아니다.
컬러키로 보존한 픽셀도 요청 사각형 안에 포함될 수 있다. GPU 업로드량이나 커서 영역으로 해석하지 않는다.

분석 JSON의 `output_requests`에는 원인별 횟수, 전체 면적 요청 수, 0 면적 요청 수,
화면 1/4 이하의 양수 면적 요청 수, 평균 면적 비율, 누적 요청 픽셀 수, CPU p50/p95/p99를 기록한다.
크기가 바뀌면 각 요청의 전체 면적을 기준으로 비율을 계산한다.
`Unlock`의 면적은 성공한 `Lock`에 전달된 영역이며 `Unlock` 인자에 의존하지 않는다.

2026-09-11 ESL 복사본, GPU/Sharp Bilinear/VSync OFF/창 모드에서 준비 10초 후 10초간 수집했다.
`output/perf-request-origins-20260911/run-00/result.json`은 포커스 이탈·기록 손실 없이 유효했다.
수집 구간의 요청은 모두 전체 화면 `request_blt_fast` 1,429회였다. 해당 출력 scope의 CPU p95는
0.3856 ms였으며 실제 표시 지연을 뜻하지 않는다. 작은 부분 갱신이 출력 빈도의 주원인이라는
가설은 이 표본에서 확인되지 않았다. 입력 잠금 이벤트가 없어 입력 지연에 대한 결론은 내리지 않는다.

## 비교 실행기

### 전장 진입 후 수집

메뉴 표본으로 전장 중 커서 지연이나 순간적인 게임 속도 저하를 판단하지 않는다.
ESL 2606의 진입 경로는 `임의 게임 → 혼자 하기 → 확인 → 게임 시작(확인)`이다.
`--wait-for-ready --scene 'random game / solo / battlefield'`를 사용하면 실행별
`awaiting-ready.json`에 PID를 기록하고 최대 600초 기다린다. 전장 화면을 확인한 후
해당 run 폴더에 `ready.json`을 기록한다. 최소 내용은 `{"scene_verified":true}`이며,
지도·상대·부하 설명과 스크린샷 증거 경로를 함께 기록하는 것을 권장한다.
이후 warmup과 측정이 시작된다. 각 반복마다 새 게임의 전장 진입을 확인해야 한다.
화면을 자동으로 판별하는 기능은 아니며, 로딩이나 로비에서 신호를 주면 안 된다.
기록 버퍼는 실행부터 사용하므로 오래 대기해서 발생한 overflow도 유효성 검사에서 제외한다.

2026-09-11 첫 전장 표본: `output/perf-battle-20260911/run-00`.
`[ESL] 죽음의바다`, 컴퓨터 1명, 초반 기지에서 농부 선택 및 이동 명령을 내린 뒤
3840×2160 전체화면, GPU + Sharp Bilinear + VSync OFF로 30초 커서 sweep을 수행했다.
전장 화면 증거는 `battle-verified.png`에 저장했다. 대규모 교전 또는 반복 스크롤 부하는 아니다.
포커스 이탈·강제 종료·계측 손실은 없었으며 기록 종료 시각은 측정 종료를 포함했다.

| 전장 지표 | 결과 |
| --- | ---: |
| 연결된 입력 / 주입 입력 | 1726 / 1734 |
| 입력 전달 시간 상한 p95 / p99 / 최대 | 4.7709 / 6.8109 / 8.2392 ms |
| 출력 호출 간격 p95 / p99 / 최대 | 50.9685 / 51.9439 / 53.3917 ms |
| Present 호출 CPU p95 / 최대 | 0.1784 / 0.4682 ms |
| 전체 출력 처리 CPU p95 / 최대 | 0.3586 / 0.6192 ms |
| mouse_lock_wait 최대 | 0.0007 ms |
| 프로세스 CPU (논리 코어 하나 = 100%) | 98.74% |
| 전체 화면 BltFast / Unlock 출력 요청 | 600 / 282회 |

가장 긴 출력 간격 53.3917ms 중 직전 Present 호출은 0.1172ms였고,
나머지 53.2745ms는 그 Present 종료 후 다음 Present 진입까지였다.
이 표본의 긴 간격을 GPU Present 대기로 설명할 근거는 없다. BltFast 600회/30초의
화면 갱신 주기와 Unlock의 추가 출력은 게임 측 호출 간격 조사 대상으로 남긴다.
호출률 29.39/s를 표시 FPS로 해석하지 않는다. 입력 전달이 빨라도 화면 속 커서 갱신이
같이 빠르다는 뜻은 아니다. 시뮬레이션 틱과 실제 화면 표시를 계측하지 않았으므로
정상 게임 주기인지 사용자가 보고한 순간적 랙인지 아직 확정하지 않는다.
후속 검증은 이동·스크롤·교전 구간과 증상 발생 시점을 명시한 전장 표본을 사용한다.

후속 `output/perf-battle-actions-20260911/run-00`은 같은 지도에서 창 모드,
GPU + Sharp Bilinear + VSync OFF로 90초 수집했다. 농부 선택(수집 약 10초)과
이동 명령(약 25초)을 수행했고, 약 42초 스크린샷에서 농부가 목적지에 도달한 것을 확인했다.
Right 키·미니맵 클릭·Tab 입력도 시도했으나 카메라 이동은 화면으로 확인되지 않았다.
따라서 유닛 이동 표본으로만 분류하고 스크롤·교전 성공 표본으로 취급하지 않는다.
후속 드래그 명령은 CLI 오류로 전달되지 않았고, 마지막 가장자리 클릭은 수집 종료 후
게임이 닫힌 상태에서 실패했다. 두 실패를 성공한 조작 목록에 포함하지 않는다.

| 전장 유닛 이동 90초 지표 | 결과 |
| --- | ---: |
| 출력 호출 간격 p95 / p99 / 최대 | 51.7266 / 52.4320 / 54.6309 ms |
| Present 호출 CPU p95 / 최대 | 0.2179 / 0.5428 ms |
| 전체 출력 처리 CPU p95 / 최대 | 0.4265 / 0.7537 ms |
| mouse_lock_wait 최대 | 0.0006 ms |
| 프로세스 CPU (논리 코어 하나 = 100%) | 99.68% |

1803개 출력 호출을 수집했고 GPU 유지·기록 손실 없음·정상 종료·포커스 유지 검사를 통과했다.
가장 긴 간격 54.6309ms 중 54.5482ms는 직전 Present 종료 이후에 있었다.
이 구간은 이동 명령 호출 전후 분석 창에 포함되지만, 조작 호출 시각에는 Orca 캡처 비용도
포함되므로 명령이 지연을 유발했다고 판단하지 않는다. 조작과 캡처 창은 `actions.jsonl`,
주변 간격은 `action-analysis.json`, 스크린샷은 같은 폴더에 보관한다.
약 50ms 출력 주기가 반복됐으며 이 표본에서는 그 주기를 크게 벗어나는 멈춤은 관찰되지 않았다.
태그 입력을 주입하지 않아 입력 전달 지연은 미측정이다. CPU 약 100%는 논리 코어 하나 기준이다.

GPU 자동 출력 실험은 계측 ON/OFF 모두 실행 로그의 하드웨어 활성화와 GDI 폴백 여부를
검사한다. D3D 초기화·resize 실패로 폴백하면 `backend_mismatch`로 무효화한다.
실패 직전의 요청 설정이 GPU로 기록됐다는 이유만으로 GPU 성공 표본으로 인정하지 않는다.

먼저 dry-run으로 조건과 해시를 확인한다. dry-run은 파일을 만들거나 게임을 실행하지 않는다.

```powershell
rtk proxy python tools/perf_matrix.py --exe 'D:/[ESL]Syw2plus/[ESL]Syw2plus 2606.exe' --output output/perf-esl
```

같은 명령에 `--run`을 추가하면 원본 폴더 전체를 새 output 폴더 아래로 복사하고,
복사본에 현재 빌드 DLL을 배치한 다음 게임을 반복 실행한다. 충분한 디스크 공간이 필요하다.
원본 게임 폴더 내부 또는 기존 출력 폴더는 거부하고 복사본은 시험 후 보존한다.
정상 종료 요청에 응답하지 않는 경우 실행기가 만든 자식 프로세스만 종료하고 시험을 중단한다.

기본값은 5가지 출력 설정 × 창/전체화면 × 3회, 준비 10초/수집 60초다.
옵션: `--repeats`, `--warmup`, `--seconds`, `--dll`, `--presentmon`, `--window-only`, `--case`, `--trace-mode`.
`--case renderer:scaling:vsync:fullscreen`으로 조건을 선택하며 여러 번 지정할 수 있다.
`--trace-mode both`는 동일 조건의 계측 ON/OFF를 섞어 실행한다. 기본값은 `on`이다.

```powershell
rtk proxy python tools/perf_matrix.py --exe 'D:/[ESL]Syw2plus/[ESL]Syw2plus 2606.exe' --output output/perf-baseline --case auto:sharp-bilinear:0:1 --trace-mode both --repeats 3 --warmup 10 --seconds 60 --run
```

GetProcessTimes로 수집 구간의 프로세스 CPU 시간을 측정한다. `cpu_percent_one_core`는
논리 코어 하나를 100%로 잡은 값이며, 여러 스레드가 실행되면 100%를 넘을 수 있다.
조건별 중앙값과 최소·최대를 보존하고 ON/OFF 중앙값 차이는 퍼센트포인트로 표시한다.
이 차이에는 실행 간 변동이 포함되므로 정확한 계측 오버헤드로 단정하지 않는다.

PresentMon 실행 파일을 지정하면 GPU 조건의 v1 metrics CSV를 보관·분석한다.
PID와 swap chain을 분리하며 표시되지 않은 프레임, N/A, 미지원 스키마를 처리한다.
한 swap chain일 때만 조건 요약에 ETW 표시 FPS를 추가하며 여러 개이면 선택을 추정하지 않는다.
외부 표시 이벤트와 내부 QPC의 수집 시작·끝은 완전히 일치하지 않을 수 있다.
정의: [PresentMon 공식 콘솔 문서](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md).

각 run에는 QPC 수집 구간과 원시 CSV·통계·조건을 저장한다.
포커스 이탈, 설정 불일치, GPU 실패로 GDI 전환, 강제 종료, 계측 손실은 유효하지 않은 결과로 남긴다.
수집 중 창 위치·크기 변경과 설정창 열기·적용도 무효 처리한다. 창 좌표는 가능하면
per-monitor-v2 관찰자 컨텍스트에서 조회하고 실제 사용한 좌표 컨텍스트를 기록한다.
설정창을 열거나 장면을 바꾸지 말고 실제 게임 창을 활성 상태로 유지한다.
실행기는 메인 메뉴 진입 여부 자체를 확인하지 못하므로 영상·팝업·장면 상태는 사람이 확인해야 한다.

현재 자동화는 실행/설정/수집/종료 반복이다. 1:1 물리 창 크기 지정, 동일 저장게임 자동 로드,
하드웨어 마우스 입력 재현, 고속 촬영 및 자동 성능 회귀 판정은 아직 구현하지 않았다.
게임이 기록하는 save/config 등은 복사본 안에서 반복 간 유지되므로 상태가 바뀐 구간은 비교에 사용하지 않는다.
OS·GPU·모니터·주사율·DPI·마우스 폴링레이트는 별도 시험 기록에 추가해야 한다.

실제 ESL 장비의 기본 설정 ON/OFF 측정 결과는 아래에 기록했다. 전체 조건의 60초×3회 기준선은 아직 없다.
이 진단 도구의 도입은 커서 지연 해결이나 기존 OS 수동 검증 일정 변경을 의미하지 않는다.

## 커서 이동 실측 (2026-09-11)

`output/perf-cursor-sweep-retry-20260911`에서 ESL 2606 메인 메뉴, 창 모드,
GPU + Sharp Bilinear를 사용해 각 조건을 10초 준비 후 8초씩 1회 측정했다.
두 실행 모두 유효했으며 포커스 이탈이나 강제 종료가 없었다.

| 지표 | VSync 켜기 | VSync 끄기 |
| --- | ---: | ---: |
| SendInput 이동 수 | 463 | 464 |
| Present CPU p95 (ms) | 11.3121 | 0.1870 |
| mouse_dispatch CPU p95 (ms) | 0.0255 | 0.0297 |
| mouse_lock_wait p95 (ms) | 0.0005 | 0.0006 |
| CPU 사용률 (논리 코어 1개 = 100%) | 49.18% | 99.97% |

이 표본에서는 잠금 경합보다 VSync에 따른 Present 호출 대기가 두드러졌다.
VSync를 끄면 대기는 줄지만 CPU 사용량은 늘었다. 메시지 처리 전 큐 대기,
화면 표시 시점, 실제 커서 지연은 측정하지 않았으므로 원인을 확정하거나
입력 지연 감소량으로 해석하지 않는다. 조건별 1회 결과이며 반복 측정이 필요하다.
`GetCursorPos` 계측 이벤트는 없었으며, `mouse_dispatch` 수는 입력 병합과 다른
마우스 메시지의 영향을 받으므로 주입 수와 일대일로 대응하지 않는다.

첫 시도 `output/perf-cursor-sweep-20260911`는 사용자 수동 이동으로 중단되어
성능 비교에서 제외했다. 사용자가 직접 이동했음을 확인했다.

## 클릭 없는 커서 이동 실험

`--cursor-sweep`는 수집 중 클라이언트 내부를 수평 이동 1초 → 정지 1초 → 반대 이동 1초 →
정지 1초 순서로 반복한다. 약 8ms마다 위치를 갱신하며 정지 중 같은 위치를 반복 주입하지 않는다.
Windows SendInput의 절대 가상 데스크톱 좌표를 사용하고 버튼·휠 입력은 보내지 않는다.
스케줄러와 기본 WM_MOUSEMOVE 병합에 따라 실제 이벤트 간격/개수는 달라질 수 있다.
[MOUSEINPUT 문서](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-mouseinput)

```powershell
rtk proxy python tools/perf_matrix.py --exe 'D:/[ESL]Syw2plus/[ESL]Syw2plus 2606.exe' --output output/perf-cursor --case auto:sharp-bilinear:0:0 --case auto:sharp-bilinear:1:0 --repeats 1 --warmup 10 --seconds 8 --cursor-sweep --run
```

게임 전경 이탈, 버튼 누름, 창 크기·위치 변경, 경로에서 벗어난 커서 이동이 발견되면 중단한다.
이 실험에서는 마우스를 놓아 둔다. 종료 시 전경과 커서 위치가 여전히 실험 상태일 때만 원래 위치로 복구한다.
보이는 게임 창이 하나이고 물리 좌표 컨텍스트를 사용할 수 있을 때만 시작한다.
`cursor-injections.json`은 SendInput 호출 전후 QPC와 목표 좌표, 실행 내 고유 sequence를 저장한다.
이는 게임의 처리 시각이나 실제 표시 시각이 아니며 input-to-photon 값으로 계산하지 않는다.
이동 실험과 비자동 이동 결과는 비교 보고서에서 별도 조건으로 묶는다.

### 주입부터 래퍼 진입까지의 시간

후속 전체화면 측정 `output/perf-input-fullscreen-20260911`도 같은 날 조건별 3회,
준비 10초 + 수집 8초로 수행했다. 6/6 실행이 유효했고 모든 실행의 물리 클라이언트
크기는 3840×2160이었다. 중복 태그, 미등록 태그, 시간 역전은 모두 0이었다.

| 전체화면 지표 | VSync 켜기 | VSync 끄기 |
| --- | ---: | ---: |
| 연결된 입력 / 주입 입력 (3회 합계) | 650 / 1388 | 1336 / 1387 |
| 전달 시간 하한 p95의 반복 중앙값 (ms) | 8.7034 | 7.3903 |
| 전달 시간 상한 p95의 반복 중앙값 (ms) | 8.9958 | 7.7606 |
| 상한 p95의 반복 범위 (ms) | 8.5803–9.0828 | 7.7079–7.8940 |
| CPU 사용률 반복 중앙값 (코어 하나 = 100%) | 48.24% | 100.33% |

전체화면에서도 VSync OFF의 관찰된 입력 전달 개선과 CPU 사용 증가가 확인됐다.
아래 창 모드 결과와 별도로 집계했으며 동일한 입력 병합 편향이 적용된다.
이것은 메인 메뉴의 소프트웨어 주입 실험이며 전투 장면, 표시 FPS, 실제 커서 표시 지연,
운영체제별 수동 검증을 대신하지 않는다. OS별 수동 검증은 기존 방침대로 1.0 이후 진행한다.

2026-09-11 `output/perf-input-delivery-20260911`에서 같은 ESL 메인 메뉴 창 모드,
GPU + Sharp Bilinear 조건으로 준비 10초 + 수집 8초를 VSync별 3회 반복했다.
6/6 실행이 유효했고 중복 태그, 미등록 태그, 시간 역전은 모두 0이었다.

| 지표 | VSync 켜기 | VSync 끄기 |
| --- | ---: | ---: |
| 연결된 입력 / 주입 입력 (3회 합계) | 640 / 1390 | 1334 / 1393 |
| 전달 시간 하한 p95의 반복 중앙값 (ms) | 8.6481 | 7.1699 |
| 전달 시간 상한 p95의 반복 중앙값 (ms) | 9.0128 | 7.4899 |
| 상한 p95의 반복 범위 (ms) | 8.8270–9.1021 | 7.3957–7.5880 |
| CPU 사용률 반복 중앙값 (코어 하나 = 100%) | 43.52% | 102.47% |

VSync OFF에서 연결된 입력의 전달이 빨라지고 개별 입력 관찰 비율이 증가했다.
그러나 병합되어 관찰되지 않은 입력의 지연은 알 수 없다. 이 선택 편향 때문에
전체 입력 지연이 위 차이만큼 줄었다고 해석하지 않는다. 현재 VSync OFF 기본값을
뒷받침하지만 전체화면·전투 장면이나 실제 커서 표시 지연까지 일반화하지 않는다.
이번 결과만으로 렌더 스레드 분리나 잠금 구조 변경을 도입하지 않는다.

이동 입력의 `dwExtraInfo`에 `0x48510000 | sequence`를 기록하고, 래퍼는
`WM_MOUSEMOVE` 진입 직후 잠금을 얻기 전에 `GetMessageExtraInfo`로 이를 읽어
`mouse_injected_entry`를 남긴다. 계측이 꺼져 있으면 부가 정보를 읽지 않는다.
동기 전송 메시지는 제외하며 원래 입력 병합 정책을 유지한다. sequence는
실행마다 1부터 시작하고 65535를 넘으면 재사용 없이 중단한다. 종료 시 커서 복원은 태그가 없다.

`result.json`의 `input_delivery`는 고유 태그가 한 번 관찰된 입력만 연결한다.
SendInput 호출 중에도 수신이 가능하므로 하한은 `max(0, 진입 - 호출 종료)`,
상한은 `진입 - 호출 시작`으로 계산한다. 이는 OS 전달과 게임 스케줄링을 포함하며
순수한 메시지 큐 대기나 하드웨어 입력 지연, input-to-photon은 아니다.
중복 태그와 시간 역전은 제외하고 개수를 표시한다. 관찰되지 않은 입력은 병합됐을 수 있으므로
하드웨어 손실률로 해석하지 않는다. 태그가 없으면 지연은 0이 아니라 미측정이다.
완전한 유효 trace만 분석하며 HTML/JSON 비교는 실행별 p95의 중앙값을 보여 준다.
태그 연결이 없는 커서 실험도 표에서 숨기지 않고 `Unavailable runs`와 `N/A`로 표시한다.
계측 OFF 실행 역시 입력 전달 시간은 미측정이며, 주입 수는 실행기의 기록을 사용한다.
근거: [MOUSEINPUT 부가 정보](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-mouseinput).

## 조건별 보고서와 실게임 점검

실행 manifest에는 EXE/DLL/실행기 해시 외에 실행기에서 사용하는 6개 Python 모듈의
SHA-256과 Python 버전을 기록한다. 원본 INI의 존재 여부와 해시도 남기며, 각 run에는
실행 직전 적용한 `hqcdd.ini` 스냅샷 및 `settings_sha256`을 보관한다. 실행이 끝난 뒤
게임 폴더에 남은 마지막 설정만으로 이전 실행을 추정하지 않는다. 예전 결과에 없는
재현 정보는 소급해서 채우지 않는다.

커서 주입 기록 저장이 실패하면 원래 측정 오류를 보존하면서 저장 실패를 추가하고,
커서 복원 시도와 테스트용 게임 종료 절차를 계속한다. 해당 실행은 유효 표본에서 제외한다.
디스크 전체가 가득 차면 최종 결과 파일도 저장하지 못할 수 있으므로, 결과 파일이 없는
실행은 기존과 같이 미완료로 취급한다. 저장 실패와 복원 실패 경로는 모의 테스트로 검증한다.
2026-09-11 `output/perf-provenance-smoke-20260911`의 계측 ON/OFF 각 2초 검증은
2/2 유효했으며, 도구 해시 6개와 실행별 설정 스냅샷 해시 일치를 확인했다.
이 실행은 기록 기능 검증용이며 기존 성능 표본과 합산하지 않는다.

실행기가 완료되면 comparison.json과 comparison.html을 생성한다. 기존 결과도 다음과 같이 다시 집계할 수 있다.

```powershell
rtk proxy python tools/perf_compare.py output/perf-esl-smoke-20260911
```

무효 표본을 제외하고 조건별 반복 수, 각 run의 CPU p95 중앙값·범위, API 호출률과
지원되는 경우의 ETW 표시 FPS를 보여 준다. 전체 프레임을 합친 p95로 표현하지 않는다.
HTML의 막대 그래프는 CPU 처리 시간 비교이며 입력 지연 그래프가 아니다.

2026-09-11 ESL 2606 복사본에서 준비 10초·수집 2초·반복 1회의 10개 조건을 실행했다.
10/10이 수집 구간의 포커스·설정 일치·정상 종료·데이터 손실 검사에 통과했다.
이는 실행기의 실게임 smoke test이며 설정별 성능 순위나 커서 지연 해결의 근거가 아니다.
PresentMon 실측은 이 smoke test에 포함하지 않았고 CSV 파서는 합성 표본으로 검증했다.

같은 날 GPU + Sharp Bilinear + VSync OFF + 전체화면에서 준비 10초, 수집 60초로
계측 ON/OFF 각각 3회 실행했다. ON 3회와 OFF 2회가 유효했고, OFF 1회는 포커스 이탈로 제외했다.
물리 모니터는 3840×2160, 60Hz였다. 이 측정 당시 실행기는 DPI 가상화된 창 좌표를 기록했으며,
이후 추가한 좌표 컨텍스트·수집 중 창 크기 변경 검사는 이 표본에 소급 적용하지 않았다.

| 지표 | 계측 ON (3회) | 계측 OFF (2회) |
| --- | --- | --- |
| 프로세스 CPU 중앙값, 코어 하나 기준 | 103.123% | 103.326% |
| 반복별 CPU 범위 | 102.503–103.304% | 103.284–103.369% |
| 출력 처리 CPU p95의 반복 중앙값 | 0.2636 ms | 계측 없음 |
| Present API 호출률의 반복 중앙값 | 147.918 /s | 계측 없음 |

ON−OFF CPU 중앙값 차이는 −0.203 퍼센트포인트다. 이 표본으로 계측이 성능을 개선한다거나
오버헤드가 없다고 결론 내릴 수 없다. 표시 FPS와 커서 입력→화면 지연은 측정하지 않았다.
로컬 원시 기록과 보고서는 `output/perf-esl-baseline-20260911`에 보관한다.

PresentMon 2.5.1 실측도 별도로 시도했으나 ETW 세션 생성이 access denied(exit 6)로 실패했다.
Windows 관리자 권한 또는 Performance Log Users 권한이 필요한 환경이며 계정 권한은 변경하지 않았다.
실패 기록은 `output/perf-etw-check-20260911`에 보관하며 표시 FPS 결과에 포함하지 않는다.

후속 관리자 실행에서는 수집 도우미 토큰이 High Mandatory Level이고 Administrators 그룹이
활성화된 것을 확인했다. PresentMon 로그에 `Started recording.`과 `Stopped recording.`이
남고 실제 종료 코드가 0이어서 ETW 접근 거부는 해소됐다. 게임은 일반 권한으로 실행했다.
다만 전체화면과 창 모드의 10초 수집 모두 CSV를 생성하지 않아 표시 FPS 검증은 아직 완료되지 않았다.
권한 문제 해결과 프레임 수집 성공은 별도로 판정한다. 진단 기록은
`output/perf-etw-admin-20260911-102703`과 `output/perf-etw-admin-20260911-102819`에 보관한다.
계정 그룹이나 UAC 설정은 변경하지 않았으므로 기존 일반 권한 실행기에 `--presentmon`만
지정하는 방식에는 여전히 승격이 필요하다.

추가 진단에서 `--no_track_display --no_track_gpu --no_track_input`을 함께 지정하면
ESL PID의 CSV가 생성되는 것을 확인했다(`output/perf-etw-admin-20260911-103042/minimal.csv`).
10초에 1,415개 Present 기록을 수집했으며 제출 간격 p50/p95/p99는 각각
6.8414/8.0546/8.5729 ms였다. 이 모드에는 화면 표시 이벤트가 없으므로 표시 FPS와
입력→화면 지연은 계속 미측정이다. 기본 추적은 같은 실행에서 CSV를 생성하지 않았다.
따라서 권한과 Present 호출 수집은 확인했지만, 표시/GPU/입력 추적 중 실패 지점은 아직 분리 중이다.

게임 창을 복원하고 메인 메뉴를 화면으로 확인한 뒤에도 표시 추적 유지 시 CSV는 없고,
최소 추적에서는 CSV가 생성됐다(`output/perf-etw-admin-20260911-103814`).
PresentMon 1.10.0의 기본 표시 추적도 이 게임에서는 CSV를 생성하지 않았다.
같은 장비의 독립 D3D11 Blt/Flip 대조 프로그램에서는 2.5.1의 표시 추적과 1.10.0 모두
CSV를 생성했다(`output/perf-etw-admin-20260911-104110`). 따라서 ETW 전반의 접근 실패는
배제했으며 ESL 출력 경로의 표시 이벤트 연결 문제로 범위를 좁혔다. 드라이버나 래퍼의
특정 결함까지 확정한 것은 아니다. 이 대조 프로그램의 FPS를 게임 FPS로 사용하지 않는다.

`--presentmon-api-only`를 `--presentmon`과 함께 지정하면 세 추적 해제 옵션을 함께 전달한다.
이 선택은 manifest와 실행 결과에 `presentmon_tracking=api-only`로 기록되며 자동 전환하지 않는다.
표시 FPS와 입력 지연은 미측정으로 유지한다. 관리자 권한 요구 사항은 동일하다.
기본 모드에서 표시 열이 없는 CSV가 들어오면 유효한 전체 추적으로 인정하지 않는다.

실측 CSV에 맞춰 파서는 `msBetweenPresents`와 `msBetweenDisplayChange` 열을 지원한다.
표시 간격 열이 없는 최소 추적 CSV도 읽되 `display_tracking_available=false`,
`displayed_fps=null`로 보존한다. 기존 대문자 `Ms` 열도 호환 목적으로 지원한다.

최종 실행기의 좌표 컨텍스트 및 수집 중 변경 검사 추가 후 별도 2초 ON/OFF 검사도 수행했다.
2/2가 유효했고 창·클라이언트 좌표가 3840×2160으로 기록됐다.
이 짧은 검사(`output/perf-runner-final-20260911`)는 위 60초 성능 표본과 합치지 않는다.
