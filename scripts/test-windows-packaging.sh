#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Offline Windows packaging/audit tests: synthetic PE/zip envelope
# rejection, deterministic zip, and corresponding-source Windows coverage.
set -eu
script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH='' cd -- "$script_dir/.." && pwd)
test_root=$(mktemp -d /tmp/px4-windows-src.XXXXXX)
trap 'find "$test_root" -depth -delete' EXIT

shellcheck "$script_dir/build-windows.sh" "$script_dir/test-windows-static-relink.sh"
python3 -m py_compile "$script_dir/audit-artifact.py" "$script_dir/package-artifact.py" "$script_dir/package-source.py"
python3 "$script_dir/audit-artifact.py" --self-test >/dev/null
python3 "$script_dir/package-artifact.py" --self-test >/dev/null
python3 - "$root" <<'PY'
import importlib.util
from pathlib import Path
import sys

root = Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("audit", root / "scripts/audit-artifact.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)
exports = {}
for line in (root / "scripts/px4-winscard.def").read_text().splitlines():
    parts = line.strip().split()
    if not parts or parts[0] in ("LIBRARY", "EXPORTS"):
        continue
    if parts[0] in exports or len(parts) > 2 or (len(parts) == 2 and parts[1] != "DATA"):
        raise SystemExit("WinSCard export map must contain unique named code/DATA exports without ordinals")
    exports[parts[0]] = parts[1:]
expected = {name: [] for name in audit.WINDOWS_WINSCARD_FUNCTIONS}
expected.update({name: ["DATA"] for name in audit.WINDOWS_WINSCARD_PCI})
if exports != expected:
    raise SystemExit("WinSCard export map and package audit contract differ")
print("WinSCard export map matches the named code/DATA audit contract")
PY

libusb_archive=${PX4_LIBUSB_1_0_30_ARCHIVE:-}
if [ -z "$libusb_archive" ]; then
    printf '%s\n' 'PX4_LIBUSB_1_0_30_ARCHIVE is required to exercise the Windows source archive'
    exit 0
fi
[ -f "$libusb_archive" ] || {
    printf '%s\n' "PX4_LIBUSB_1_0_30_ARCHIVE is not a file: $libusb_archive" >&2
    exit 1
}

python3 - "$script_dir" "$root" "$test_root" "$libusb_archive" <<'PY'
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

script_dir = Path(sys.argv[1])
root = Path(sys.argv[2])
test_root = Path(sys.argv[3])
libusb_archive = Path(sys.argv[4])
version = (root / "VERSION").read_text(encoding="ascii").strip()

REQUIRED = {
    "LICENSE": "license\n",
    "README.md": "readme\n",
    "THIRD_PARTY_NOTICES.md": "notices\n",
    "scripts/build-linux-static.sh": "#!/bin/sh\n",
    "scripts/build-linux-ifd.sh": "#!/bin/sh\n",
    "scripts/build-macos-static.sh": "#!/bin/sh\n",
    "scripts/test-static-relink.sh": "#!/bin/sh\n",
}
WINDOWS = {
    "CMakeLists.txt": "# build\n",
    "docs/platforms/windows-winscard.md": "WinSCard placement and limits\n",
    "scripts/px4-winscard.def": "EXPORTS\n",
    "scripts/build-windows.sh": "#!/bin/sh\n",
    "scripts/test-windows-static-relink.sh": "#!/bin/sh\n",
    "userland/src/windows/windows_ipc.cpp": "// ipc\n",
    "userland/src/windows/windows_sleep.cpp": "// sleep\n",
    "userland/src/windows/windows_tuner_nonce.cpp": "// nonce\n",
    "userland/src/windows/winscard_card_client.h": "// card client header\n",
    "userland/src/windows/winscard_card_client.cpp": "// card client\n",
    "userland/src/windows/winscard_native.h": "// native API\n",
    "userland/src/windows/winscard_proxy.cpp": "// WinSCard proxy\n",
    "userland/tests/windows_winscard_client_tests.cpp": "// client tests\n",
    "userland/tests/windows_winscard_proxy_tests.cpp": "// proxy tests\n",
    "userland/tests/windows_winscard_import_tests.cpp": "// import tests\n",
    "userland/tools/px4_ts_windows.cpp": "// ts\n",
    "userland/tools/px4_windows_args.h": "// args\n",
    "userland/tests/windows_platform_tests.cpp": "// tests\n",
    "userland/tests/windows_worker_tests_main.cpp": "// main\n",
    "userland/include/px4/windows_tuner_nonce.h": "// header\n",
}


def make_repo(name: str, extra: dict[str, str]) -> Path:
    repo = test_root / name
    (repo / "packaging").mkdir(parents=True)
    for relative, text in {**REQUIRED, **extra}.items():
        path = repo / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    (repo / "VERSION").write_text(version + "\n", encoding="ascii")
    for template in ("REBUILD.md.in", "DEPENDENCY-NOTICE.txt.in"):
        (repo / "packaging" / template).write_text(
            (root / "packaging" / template).read_text(encoding="utf-8"), encoding="utf-8")
    subprocess.run(["git", "init", "-q", str(repo)], check=True)
    subprocess.run(["git", "-C", str(repo), "config", "user.email", "self@test"], check=True)
    subprocess.run(["git", "-C", str(repo), "config", "user.name", "self"], check=True)
    subprocess.run(["git", "-C", str(repo), "add", "-A"], check=True)
    subprocess.run(["git", "-C", str(repo), "commit", "-q", "-m", "fixture"], check=True)
    return repo


def package(repo: Path, output: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(script_dir / "package-source.py"), "--version", version,
         "--source-root", str(repo), "--source-ref", "HEAD",
         "--libusb-source-archive", str(libusb_archive), "--output-dir", str(output)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )


valid_repo = make_repo("valid", WINDOWS)
valid_out = test_root / "valid-out"
result = package(valid_repo, valid_out)
if result.returncode != 0:
    raise SystemExit(f"valid Windows source archive was rejected: {result.stdout}")
archive = valid_out / f"px4-userland-{version}-source.tar.gz"
audit = subprocess.run(
    [sys.executable, str(script_dir / "audit-artifact.py"), "--source-archive", "--archive", str(archive)],
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
)
if audit.returncode != 0:
    raise SystemExit(f"valid Windows source archive failed audit: {audit.stdout}")
print("valid Windows corresponding-source archive accepted")

for index, missing in enumerate(name for name in WINDOWS if "winscard" in name.lower()):
    incomplete = dict(WINDOWS)
    incomplete.pop(missing)
    repo = make_repo(f"incomplete-winscard-{index}", incomplete)
    if package(repo, test_root / f"incomplete-winscard-out-{index}").returncode == 0:
        raise SystemExit(f"WinSCard source archive without {missing} was accepted")
print("incomplete WinSCard implementation, export map, and import-test source rejected")

partial = {"userland/src/windows/windows_ipc.cpp": "// ipc\n"}
partial_repo = make_repo("partial", partial)
partial_out = test_root / "partial-out"
if package(partial_repo, partial_out).returncode == 0:
    raise SystemExit("partial Windows source tree was accepted")
print("partial Windows source coverage rejected")

legacy_repo = make_repo("legacy", {"drivers/windows/legacy.c": "int x;\n"})
legacy_out = test_root / "legacy-out"
if package(legacy_repo, legacy_out).returncode == 0:
    raise SystemExit("legacy Windows driver tree was accepted")
print("legacy Windows driver tree rejected")

win32_repo = make_repo("win32", {"win32/host.c": "int x;\n"})
win32_out = test_root / "win32-out"
if package(win32_repo, win32_out).returncode == 0:
    raise SystemExit("legacy win32 tree was accepted")
print("legacy win32 tree rejected")
PY

printf '%s\n' 'Windows packaging/audit self-tests: PASS'
