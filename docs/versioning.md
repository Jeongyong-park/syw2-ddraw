# 버전 관리

[Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html)을 기준으로 관리합니다.
호환성 판단 대상은 문서화한 게임 빌드 지원, DirectDraw 연동 동작,
INI 키와 의미, 기본 ASI 설치·업데이트·복구 절차 및 개발용 준비/실행 도구의 사용 방법입니다. 내부 구현 자체는 공개 API가 아닙니다.

| 변경 | 버전 |
| --- | --- |
| 호환되는 오류 수정·문서·배포 메타데이터 보완 | PATCH 증가 |
| 호환되는 기능 추가 | MINOR 증가, PATCH = 0 |
| 1.0 이후 기존 설정·사용 방법을 깨뜨리는 변경 | MAJOR 증가, MINOR/PATCH = 0 |

현재 0.y.z는 초기 개발 단계이며 안정 API를 보장하지 않습니다.
이 프로젝트에서는 0.y.z 단계의 기능 추가와 호환성 파괴를 MINOR 증가로 구분하고,
호환되는 수정은 PATCH를 증가시키며 변경 내용을 CHANGELOG에 명시합니다.

## 단일 버전 소스

루트 `VERSION`에 접두사 없이 `MAJOR.MINOR.PATCH`를 기록합니다.
현재 배포 도구는 SemVer의 정규 버전만 지원하며 `-rc.1`, `+build`는 허용하지 않습니다.
선행 0은 금지합니다. Windows VERSIONINFO 숫자 필드에 맞춰 각 숫자는 0~65535로 제한합니다.

- CMake가 VERSION을 검증하고 프로젝트 버전과 리소스를 생성합니다.
- DLL의 FileVersion/ProductVersion 문자열과 시작 로그는 동일한 세 자리 버전을 사용합니다.
- Windows 숫자 버전은 `MAJOR.MINOR.PATCH.0`입니다. 마지막 0은 SemVer의 별도 버전 요소가 아닙니다.
- 기본 ASI ZIP 이름은 `syw2-ddraw-v<버전>-asi.zip`, DLL 호환판은 `syw2-ddraw-v<버전>.zip`, Git 태그는 `v<버전>`입니다.
- DLL 설치 이름은 ddraw.dll이지만 빌드 이름 및 OriginalFilename 속성은 hqcdd.dll입니다. 파일명 변경은 버전을 변경하지 않습니다.
- 제작자 표시는 LICENSE에 맞춘 Park Jeongyong입니다. Windows 속성에서는 CompanyName에 표시합니다.
  이 정보는 코드 서명이나 인증서의 게시자 신원과 별개입니다.

## 배포 순서

1. VERSION을 변경하고 CHANGELOG의 Unreleased 항목을 새 버전으로 이동합니다.
2. `./build.ps1`과 `python -m unittest discover -s tests -p "test_*.py" -v`를 실행합니다.
3. `python package.py --asi`로 기본 ASI ZIP, `python package.py`로 DLL 호환판을 만들고 파일 속성과 패키지 내용을 확인합니다.
   확보한 환경의 실제 실행 결과와 미검증 범위를 기록합니다. 운영체제별 수동 검증은 1.0 이후 후속 작업입니다.
   ASI ZIP의 plugins/hqcdd.asi와 루트 hqcdd.ini가 README의 설치 경로와 일치해야 합니다. 기존 ASI 로더와 SYW2X는 포함하지 않습니다.
   `python package.py --developer`로 개발자 ZIP도 생성하고 `tests/test_distribution.ps1`을 실행합니다.
   세 ZIP 및 각 .zip.sha256을 확인하고 자동 검증 결과와 후속 수동 검증 범위를 기록합니다.
4. 확정한 변경을 커밋한 뒤 해당 커밋에 `v<버전>` 태그를 붙여 배포합니다.
5. 배포한 버전은 같은 이름으로 내용을 바꾸지 않습니다. 변경 시 새 버전을 사용합니다.

로컬 패키징은 같은 버전 ZIP을 다시 만들 수 있지만, 이미 공개한 릴리스 자산은 교체하지 않습니다.
### GitHub Release 자동 게시

VERSION과 정확히 일치하는 `v<버전>` 태그를 push하면 빌드 CI가 실행됩니다.
Unicorn 좌표 변환, Python 테스트, DLL 속성, 추출 ZIP의 DLL import 및 태그·ZIP 해시 검사가 통과해야 게시 작업이 시작됩니다.
ASI ZIP과 `.zip.sha256`만 Release Assets에 게시하며 DLL 호환판·개발자 ZIP은 Actions 아티팩트로 제공합니다.
0.x는 GitHub Pre-release로 표시하고 1.0 이상은 정식 Release로 게시합니다.
이는 VERSION의 SemVer 문법과 별개인 GitHub 표시이며 `-rc.1` 태그 지원은 아직 없습니다.

PR/main 빌드는 게시하지 않고 `python tools/release.py --tag v<버전>`으로 오프라인 배포 검사를 수행합니다.
게시 작업만 contents: write 권한과 GH_TOKEN을 받습니다.
`gh release create --verify-tag`로 기존 원격 태그를 사용하며 기존 Release·자산은 덮어쓰지 않습니다.
같은 태그의 재실행으로 기존 Release가 발견되면 실패합니다. 수정 배포는 새 버전을 사용하세요.
태그 생성·push는 실제 공개 배포를 시작하므로 배포할 커밋의 검증 결과와 CHANGELOG를 먼저 확인합니다.

DLL 메타데이터 형식: [Microsoft VERSIONINFO](https://learn.microsoft.com/en-us/windows/win32/menurc/versioninfo-resource).
