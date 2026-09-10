"""Validate user assets; publish only when explicitly called with --publish."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile


def release_plan(root, tag):
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise ValueError("VERSION must be normal SemVer")
    if any(int(n)>65535 for n in version.split(".")) or tag != "v"+version:
        raise ValueError("Release tag must exactly match VERSION")
    archive = root / "output" / f"syw2-ddraw-{tag}.zip"
    checksum = archive.with_suffix(".zip.sha256")
    expected = f"{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}"
    if checksum.read_text(encoding="ascii").strip() != expected:
        raise ValueError("User ZIP SHA-256 mismatch")
    notes = f"""## 설치
아래 Assets에서 **{archive.name}**을 내려받아 압축을 해제하세요.
게임을 종료하고 기존 게임 폴더의 ddraw.dll을 백업한 뒤, ZIP의 ddraw.dll과 hqcdd.ini를 게임 EXE 옆에 복사하세요.
업데이트 시 기존 hqcdd.ini와 최초 DLL 백업을 보존하세요.
기존 게임 EXE를 실행합니다. Python·Visual Studio·EXE 패치는 필요 없습니다.
Source code ZIP은 빌드 DLL이 없는 개발용 소스입니다.

Ctrl+Alt+D: 디스플레이 설정 / Alt+Enter: 창·전체화면 전환.
상세 설치·복구 안내와 스크린샷은 ZIP의 README.md 및 INSTALL.txt에 있습니다.
"""
    if version.startswith("0."):
        notes += "\n시험 배포입니다. Windows 10·11 전체 호환성 및 실제 게임 설치·복구 검증은 진행 중입니다.\n"
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
