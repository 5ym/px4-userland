#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Isolated end-to-end corresponding-source archive fixture.

Builds throwaway product/common Git snapshots, produces the corresponding-source
archive with the production helper, audits it, and checks that the bundled
foundation tree closes the dependency offline.  It never touches the product
worktree, the network, or release metadata.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile


def load_audit(scripts: Path):
    spec = importlib.util.spec_from_file_location("fixture_audit", scripts / "audit-artifact.py")
    if spec is None or spec.loader is None:
        raise SystemExit("cannot load audit-artifact.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def extract_archive(archive: Path, destination: Path) -> None:
    with tarfile.open(archive, "r:gz") as stream:
        try:
            stream.extractall(destination, filter="data")
        except TypeError:
            stream.extractall(destination)


def run(command: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None) -> None:
    completed = subprocess.run(command, cwd=cwd, env=env, text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if completed.returncode != 0:
        raise SystemExit(f"command failed ({' '.join(command)}):\n{completed.stdout}")


def write(path: Path, data: str | bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        data = data.encode("utf-8")
    path.write_bytes(data)


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rebuild_archive(root: Path, output: Path) -> None:
    manifest_path = root / "source-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    inventory = {}
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.name not in ("source-manifest.json", "SHA256SUMS"):
            inventory[path.relative_to(root).as_posix()] = {
                "size": path.stat().st_size, "sha256": sha256_file(path)}
    manifest["files"] = inventory
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    rows = [f"{sha256_file(path)}  {path.relative_to(root).as_posix()}"
            for path in sorted(root.rglob("*"))
            if path.is_file() and path.name != "SHA256SUMS"]
    (root / "SHA256SUMS").write_text("\n".join(rows) + "\n", encoding="utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(output, "w:gz") as stream:
        for path in sorted(root.rglob("*")):
            if path.is_file():
                stream.add(path, arcname=path.relative_to(root).as_posix())


def expect_audit_rejects(audit, archive: Path, label: str) -> None:
    try:
        audit.audit_source_archive(argparse.Namespace(archive=archive))
    except audit.AuditError:
        return
    raise SystemExit(f"audit accepted {label}")


def git_commit(root: Path, message: str) -> str:
    run(["git", "init", "-q"], cwd=root)
    run(["git", "-c", "user.email=fixture@example.invalid",
         "-c", "user.name=fixture", "add", "-A"], cwd=root)
    run(["git", "-c", "user.email=fixture@example.invalid",
         "-c", "user.name=fixture", "commit", "-q", "-m", message], cwd=root)
    result = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True)
    return result.strip()


PRODUCT_DUMMY_FILES = {
    "LICENSE": "product license\n",
    "README.md": "product readme\n",
    "THIRD_PARTY_NOTICES.md": "product notices\n",
    "scripts/build-linux-static.sh": "#!/bin/sh\n",
    "scripts/build-linux-ifd.sh": "#!/bin/sh\n",
    "scripts/build-macos-static.sh": "#!/bin/sh\n",
    "scripts/test-static-relink.sh": "#!/bin/sh\n",
    "packaging/REBUILD.md.in": "version @VERSION@ commit @COMMIT@ tree @TREE@\n",
    "packaging/DEPENDENCY-NOTICE.txt.in": (
        "notice @VERSION@ @PLATFORM@\n@DEPENDENCY_TEXT@\n"
    ),
}


def make_product(base: Path, name: str, *, lock: str | None, module: Path,
                 with_module: bool) -> Path:
    product = base / name
    write(product / "VERSION", "0.0.0\n")
    for path, data in PRODUCT_DUMMY_FILES.items():
        write(product / path, data)
    if lock is not None:
        write(product / "cmake/dtv-userland-core.revision", lock + "\n")
    if with_module:
        shutil.copyfile(module, product / "cmake/DtvUserlandCoreDependency.cmake")
        write(product / "CMakeLists.txt", (
            "cmake_minimum_required(VERSION 3.16)\n"
            "project(closure LANGUAGES CXX)\n"
            "set(CMAKE_CXX_STANDARD 17)\n"
            "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
            "include(cmake/DtvUserlandCoreDependency.cmake)\n"
            "px4_add_dtv_userland_core()\n"
            "add_executable(closure main.cpp)\n"
            "target_link_libraries(closure PRIVATE dtv::core)\n"
        ))
        write(product / "main.cpp", (
            '#include "dtv/userland/error.h"\n'
            '#include "dtv/userland/mock_transport.h"\n'
            "int main() {\n"
            "    using namespace dtv::userland;\n"
            "    MockTransport transport(MockTransportPolicy{65535U, 1048576U, 0x84U});\n"
            "    return error_string(Error::OK)[0] == 'O' && !transport.stream_active() ? 0 : 1;\n"
            "}\n"
        ))
    else:
        write(product / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.16)\nproject(old LANGUAGES CXX)\n")
    git_commit(product, name)
    return product


def package_source(helper: Path, product: Path, libusb: Path, output: Path,
                   repository: str | None) -> subprocess.CompletedProcess[str]:
    command = [sys.executable, str(helper), "--version", "0.0.0",
               "--source-root", str(product), "--source-ref", "HEAD",
               "--libusb-source-archive", str(libusb), "--output-dir", str(output)]
    if repository is not None:
        command += ["--common-core-repository", repository]
    return subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--libusb-source-archive", type=Path, required=True)
    parser.add_argument("--common-core-source-root", type=Path, required=True,
                        help="git checkout used as the pinned dependency repository")
    args = parser.parse_args()

    scripts = Path(__file__).resolve().parent
    helper = scripts / "package-source.py"
    module = scripts.parent / "cmake" / "DtvUserlandCoreDependency.cmake"
    libusb = args.libusb_source_archive.resolve()
    common = args.common_core_source_root.resolve()
    common_commit = subprocess.check_output(
        ["git", "-C", str(common), "rev-parse", "HEAD"], text=True).strip()

    with tempfile.TemporaryDirectory(prefix="px4-source-fixture-") as temporary:
        base = Path(temporary)

        # New snapshot: exact pin, whole-archive audit, offline closure.
        # Dirty the working-tree lock after committing; packaging --source-ref
        # HEAD must still use the committed lock, not the dirty checkout.
        product = make_product(base, "product", lock=common_commit, module=module, with_module=True)
        write(product / "cmake/dtv-userland-core.revision", "f" * 40 + "\n")
        output = base / "out"
        completed = package_source(helper, product, libusb, output, str(common))
        if completed.returncode != 0:
            raise SystemExit(f"source packaging failed:\n{completed.stdout}")
        archive = output / "px4-userland-0.0.0-source.tar.gz"
        if not archive.is_file():
            raise SystemExit("fixture did not produce a source archive")

        extracted = base / "extracted"
        extracted.mkdir()
        extract_archive(archive, extracted)
        bundled = extracted / "repository" / "third_party" / "dtv-userland-core"
        if not (bundled / "LICENSE").is_file() or not (bundled / "PROVENANCE.json").is_file():
            raise SystemExit("bundled foundation tree is incomplete")
        staged_lock = (extracted / "repository" / "cmake" / "dtv-userland-core.revision").read_text().strip()
        if staged_lock != common_commit:
            raise SystemExit("staged product lock does not match the pinned commit")

        # Offline closure: configure/build the product with only the bundled tree.
        build = base / "closure-build"
        run(["cmake", "-S", str(extracted / "repository"), "-B", str(build),
             "-G", "Ninja", "-DPX4_BUILD_TESTS=OFF"])
        run(["cmake", "--build", str(build), "-j2"])

        # Negative: a declared dependency whose build closure is removed must be
        # rejected even when the inventory and checksums are internally consistent.
        audit = load_audit(scripts)
        stripped = base / "stripped"
        shutil.copytree(extracted, stripped)
        stripped_bundle = stripped / "repository" / "third_party" / "dtv-userland-core"
        for name in ("CMakeLists.txt", "cmake", "include", "src"):
            target = stripped_bundle / name
            if target.is_dir():
                shutil.rmtree(target)
            elif target.exists():
                target.unlink()
        stripped_archive = base / "stripped-out" / "px4-userland-0.0.0-source.tar.gz"
        rebuild_archive(stripped, stripped_archive)
        expect_audit_rejects(audit, stripped_archive,
                             "a dependency bundle with no build closure")

        # Negative: a single missing common source file must also be rejected.
        partial = base / "partial"
        shutil.copytree(extracted, partial)
        (partial / "repository" / "third_party" / "dtv-userland-core"
         / "src" / "mock_transport.cpp").unlink()
        partial_archive = base / "partial-out" / "px4-userland-0.0.0-source.tar.gz"
        rebuild_archive(partial, partial_archive)
        expect_audit_rejects(audit, partial_archive,
                             "a dependency bundle missing a source file")

        # Historical snapshot without the dependency lock still audits.
        old_product = make_product(base, "product-old", lock=None, module=module, with_module=False)
        old_output = base / "out-old"
        completed = package_source(helper, old_product, libusb, old_output, None)
        if completed.returncode != 0:
            raise SystemExit(f"historical source packaging failed:\n{completed.stdout}")
        old_archive = old_output / "px4-userland-0.0.0-source.tar.gz"
        with tarfile.open(old_archive, "r:gz") as stream:
            old_members = stream.getnames()
        if any(name.startswith("repository/third_party/dtv-userland-core") for name in old_members):
            raise SystemExit("historical snapshot unexpectedly bundled the foundation")

        # Mismatched pin is refused.
        bad_product = make_product(base, "product-bad", lock="f" * 40, module=module, with_module=True)
        completed = package_source(helper, bad_product, libusb, base / "out-bad", str(common))
        if completed.returncode == 0:
            raise SystemExit("mismatched common pin was accepted")

        # A staged bundled tree is not accepted as the pinned Git source.
        bundled_product = make_product(base, "product-bundled", lock=common_commit,
                                       module=module, with_module=True)
        write(bundled_product / "third_party/dtv-userland-core/CMakeLists.txt",
              "project(stub)\n")
        git_commit(bundled_product, "bundled")
        completed = package_source(helper, bundled_product, libusb, base / "out-bundled",
                                   str(base / "no-such-repository"))
        if completed.returncode == 0:
            raise SystemExit("packaged a dependency from a non-Git bundled tree")

    print("source archive fixture: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
