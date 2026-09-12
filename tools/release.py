"""Validate user assets; publish only when explicitly called with --publish."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zipfile


def release_plan(root, tag):
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise ValueError("VERSION must be normal SemVer")
    if any(int(n)>65535 for n in version.split(".")) or tag != "v"+version:
        raise ValueError("Release tag must exactly match VERSION")
    archive = root / "output" / f"syw2-ddraw-{tag}-asi.zip"
    checksum = archive.with_suffix(".zip.sha256")
    expected = f"{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}"
    if checksum.read_text(encoding="ascii").strip() != expected:
        raise ValueError("User ZIP SHA-256 mismatch")
    with zipfile.ZipFile(archive) as packaged:
        names = {name.replace('\\', '/').lower() for name in packaged.namelist()}
        if not {'plugins/hqcdd.asi', 'install.txt', 'hqcdd.ini'}.issubset(names):
            raise ValueError('ASI release is missing required files')
        if any(name.rsplit('/', 1)[-1] in {'ddraw.dll', 'ddrawhooked.dll', 'syw2x.asi'} for name in names):
            raise ValueError('ASI release must preserve the existing loader and SYW2X')
    notes = f"""## 설치
아래 Assets에서 **{archive.name}**을 내려받아 압축을 해제하세요.
ESL의 기존 Ultimate ASI Loader와 함께 사용하는 ASI판입니다. 게임을 종료하고 ZIP의 plugins/hqcdd.asi를 게임의 plugins 폴더에 복사하세요.
기존 ddraw.dll(ASI 로더)과 plugins/syw2x.asi는 유지하세요. 로더와 SYW2X는 ZIP에 포함하지 않습니다.
이전 HQCDD DLL판으로 ddraw.dll을 덮어썼다면 원래 ESL/SYW2X 배포본의 ASI 로더를 먼저 복원하세요.
ddrawHooked.dll 체인 파일은 별도로 백업해 게임 폴더에서 옮기세요. DLL판과 ASI판을 동시에 설치하지 마세요.
hqcdd.ini는 게임 EXE 옆에 두며 기존 설정과 최초 DLL 백업을 보존하세요.
기존 게임 EXE를 실행합니다. Python·Visual Studio·EXE 패치는 필요 없습니다.
Source code ZIP은 빌드 DLL이 없는 개발용 소스입니다.

Ctrl+Alt+D: 디스플레이 설정 / Ctrl+Alt+F: OSD / Alt+Enter: 창·전체화면 전환.
제거하려면 게임 종료 후 plugins/hqcdd.asi만 별도로 옮기세요. 기존 로더와 SYW2X는 유지합니다.
상세 설치·복구 안내와 스크린샷은 ZIP의 README.md 및 INSTALL.txt에 있습니다.

## 주요 변경
v0.7.1: 코드·데이터·주소 배치가 같은 리소스 수정 EXE의 16:9 요청을 허용합니다. 외부 프로파일은 필요 없습니다.
게임 코드·데이터 변경이나 주소 이동은 지원하지 않으며, 적용 실패 시 이유를 오버레이로 한 번 안내합니다.
확인/Esc로 닫으면 입력을 복원하고 현재 화면 모드와 저장값을 유지합니다.

ASI 기본 배포, 실시간 성능 OSD, SYW2X 설정·팔레트, 실험적 16:9 전장과 중앙 HUD를 제공합니다.
창 테두리를 드래그하면 현재 적용된 화면 비율을 유지합니다. 4:3이 기본이며 16:9 변경은 저장 후 재실행합니다.
세부 변경은 ZIP의 CHANGELOG.md를 참고하세요.

검증 범위: ESL 로더 9.7.0과 SYW2X 공존, 37창병 전장 및 831×624 확장을 확인했습니다.
이번 호환성 개선은 네이티브 회귀 검사와 리소스 수정 ESL 사본의 사설 메모리 패치 적용으로 검증했습니다. 외부 수정판의 장시간 실게임 검증은 포함하지 않습니다.
4:3·16:9 정지 병력 전장에서 각각 30초 동안 600·599틱을 관측했습니다(각 1회).
16:9는 실험 기능이며 4:3/16:9 혼합 멀티플레이와 장시간 전투는 미검증입니다.
SYW2X 전체 기능·확장 전체화면·실제 온라인 채팅은 미검증이며 애니메이션 지연 해결을 보장하지 않습니다. Windows 10은 미검증입니다.
운영체제별 설치·업데이트·복구와 GPU·DPI·다중 모니터 수동 검증은 1.0 이후에 진행합니다.
"""
    if version.startswith("0."):
        notes += "\n시험 배포입니다.\n"
    return archive, checksum, notes, version.startswith("0.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--publish", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    archive, checksum, notes, prerelease = release_plan(root, args.tag)
    print(f"Validated {args.tag}: {archive.name}, {checksum.name}; prerelease={prerelease}")
    if not args.publish:
        return
    repository = os.environ["GITHUB_REPOSITORY"]
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "release-notes.md"
        path.write_text(notes, encoding="utf-8")
        command = ["gh", "release", "create", args.tag, str(archive), str(checksum),
                   "--repo", repository, "--verify-tag", "--title", f"HQCDD {args.tag}",
                   "--notes-file", str(path)]
        if prerelease:
            command.append("--prerelease")
        # gh refuses an existing release; never upload with --clobber.
        subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
