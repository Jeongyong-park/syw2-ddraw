# 버전 관리

[Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html)을 기준으로 관리합니다.
호환성 판단 대상은 문서화한 게임 빌드 지원, DirectDraw 연동 동작,
INI 키와 의미, 기본 DLL 교체 설치·업데이트·복구 절차 및 개발용 준비/실행 도구의 사용 방법입니다. 내부 구현 자체는 공개 API가 아닙니다.

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
- ZIP 이름은 `syw2-ddraw-v<버전>.zip`, Git 태그는 `v<버전>`입니다.
- DLL 설치 이름은 ddraw.dll이지만 빌드 이름 및 OriginalFilename 속성은 hqcdd.dll입니다. 파일명 변경은 버전을 변경하지 않습니다.
- 제작자 표시는 LICENSE에 맞춘 Park Jeongyong입니다. Windows 속성에서는 CompanyName에 표시합니다.
  이 정보는 코드 서명이나 인증서의 게시자 신원과 별개입니다.

## 배포 순서

1. VERSION을 변경하고 CHANGELOG의 Unreleased 항목을 새 버전으로 이동합니다.
2. `./build.ps1`과 `python -m unittest discover -s tests -p "test_*.py" -v`를 실행합니다.
3. `python package.py`로 ZIP을 만들고 DLL 속성과 패키지 내용을 확인합니다.
   게임 폴더에 DLL을 ddraw.dll로 배치한 상태의 실제 실행·설정·종료·복구도 검사합니다.
   사용자 ZIP 루트의 ddraw.dll과 hqcdd.ini가 README의 설치 경로와 일치해야 합니다.
   `python package.py --developer`로 개발자 ZIP도 생성하고 `tests/test_distribution.ps1`을 실행합니다.
   두 ZIP 및 각 .zip.sha256을 확인하고 [0.6 실기기 검증](milestone-0.6.md)을 기록합니다.
4. 확정한 변경을 커밋한 뒤 해당 커밋에 `v<버전>` 태그를 붙여 배포합니다.
5. 배포한 버전은 같은 이름으로 내용을 바꾸지 않습니다. 변경 시 새 버전을 사용합니다.

로컬 패키징은 같은 버전 ZIP을 다시 만들 수 있지만, 이미 공개한 릴리스 자산은 교체하지 않습니다.
현재 작업은 0.6.0 배포 준비이며 태그·GitHub Release 생성은 별도입니다.

DLL 메타데이터 형식: [Microsoft VERSIONINFO](https://learn.microsoft.com/en-us/windows/win32/menurc/versioninfo-resource).
