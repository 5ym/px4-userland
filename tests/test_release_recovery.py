# SPDX-License-Identifier: GPL-2.0-only
"""Offline identity and provenance tests for the bounded v0.2.1 recovery."""

from io import BytesIO
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import release_publish as publish
import release_recover as recovery


TOOL_SHA = "e" * 40
RECOVERY_RUN_ID = 51001
RECOVERY_WORKFLOW_ID = 51002


def zip_report(report):
    stream = BytesIO()
    with zipfile.ZipFile(stream, "w") as archive:
        archive.writestr(recovery.RECOVERY_REPORT_NAME, json.dumps(report).encode("utf-8"))
    return stream.getvalue()


class FakeAPI:
    repository = recovery.REPOSITORY
    prefix = f"/repos/{recovery.REPOSITORY}"

    def __init__(self):
        self.calls = []
        self.tag_object = recovery.TAG_OBJECT
        self.report = {
            "mode": "verify",
            "product": recovery.PRODUCT,
            "status": "blocked",
            "phase": "verify-inputs",
            "publication_attempted": False,
            "release_id": None,
            "error": "unsafe API path",
        }
        self.report_zip = zip_report(self.report)
        self.report_digest = "sha256:" + hashlib.sha256(self.report_zip).hexdigest()
        self.repository_data = {
            "id": recovery.REPOSITORY_ID,
            "full_name": recovery.REPOSITORY,
            "fork": False,
        }
        self.runs = {}
        self._add_run(
            RECOVERY_RUN_ID, 1, RECOVERY_WORKFLOW_ID, recovery.RECOVERY_WORKFLOW_PATH,
            "workflow_dispatch", "main", TOOL_SHA, "in_progress", None,
        )
        self._add_run(
            recovery.FAILED_PUBLISH_RUN_ID, recovery.FAILED_PUBLISH_ATTEMPT,
            recovery.FAILED_PUBLISH_WORKFLOW_ID, recovery.FAILED_PUBLISH_WORKFLOW_PATH,
            "push", recovery.TAG, recovery.PRODUCT_SHA, "completed", "failure",
            referenced_workflows=[{
                "path": f"{recovery.REPOSITORY}/.github/workflows/release_reusable.yml@{recovery.PRODUCT_SHA}",
                "ref": f"refs/tags/{recovery.TAG}",
                "sha": recovery.PRODUCT_SHA,
            }],
        )
        self._add_run(
            recovery.TAG_BUILD_RUN_ID, recovery.TAG_BUILD_ATTEMPT,
            recovery.TAG_BUILD_WORKFLOW_ID, recovery.TAG_BUILD_WORKFLOW_PATH,
            "push", recovery.TAG, recovery.PRODUCT_SHA, "completed", "success",
        )
        self.ref = {
            "ref": f"refs/tags/{recovery.TAG}",
            "object": {"type": "tag", "sha": recovery.TAG_OBJECT},
        }
        self.tag = {
            "sha": recovery.TAG_OBJECT,
            "tag": recovery.TAG,
            "object": {"type": "commit", "sha": recovery.PRODUCT_SHA},
        }
        self.jobs = {
            recovery.FAILED_PUBLISH_RUN_ID: [
                self._job("release / verify", "failure", recovery.FAILED_PUBLISH_RUN_ID),
                self._job("release / publish", "skipped", recovery.FAILED_PUBLISH_RUN_ID),
            ],
            recovery.TAG_BUILD_RUN_ID: [
                self._job(f"build job {index}", "success", recovery.TAG_BUILD_RUN_ID)
                for index in range(20)
            ],
        }
        self.report_artifact = {
            "id": recovery.FAILED_REPORT_ARTIFACT_ID,
            "name": recovery.FAILED_REPORT_ARTIFACT_NAME,
            "digest": self.report_digest,
        }
        self.report_artifact_detail = {
            **self.report_artifact,
            "expired": False,
            "expires_at": "2099-01-01T00:00:00Z",
            "size_in_bytes": len(self.report_zip),
            "workflow_run": {
                "id": recovery.FAILED_PUBLISH_RUN_ID,
                "head_sha": recovery.PRODUCT_SHA,
                "repository_id": recovery.REPOSITORY_ID,
                "head_repository_id": recovery.REPOSITORY_ID,
            },
        }
        self.build_artifacts = [
            {"id": recovery.TAG_BUILD_ARTIFACT_ID, "name": publish.ARTIFACT},
            {"id": recovery.TAG_PROVENANCE_ARTIFACT_ID, "name": "release-provenance"},
        ]

    @staticmethod
    def _job(name, conclusion, run_id):
        head_sha = (recovery.PRODUCT_SHA if run_id != RECOVERY_RUN_ID else TOOL_SHA)
        return {"name": name, "status": "completed", "conclusion": conclusion,
                "run_id": run_id, "head_sha": head_sha}

    def _add_run(self, run_id, attempt, workflow_id, path, event, branch, sha,
                 status, conclusion, referenced_workflows=None):
        repository = {"id": recovery.REPOSITORY_ID, "full_name": recovery.REPOSITORY,
                      "fork": False}
        run = {
            "id": run_id,
            "run_attempt": attempt,
            "workflow_id": workflow_id,
            "path": path,
            "event": event,
            "head_branch": branch,
            "head_sha": sha,
            "status": status,
            "conclusion": conclusion,
            "repository": dict(repository),
            "head_repository": dict(repository),
            "pull_requests": [],
        }
        if referenced_workflows is not None:
            run["referenced_workflows"] = referenced_workflows
        self.runs[run_id] = run

    def json(self, method, path, body=None, **_kwargs):
        self.calls.append((method, path))
        if method != "GET":
            raise AssertionError(f"unexpected API method: {method}")
        if path == self.prefix:
            return dict(self.repository_data)
        if path == f"{self.prefix}/git/ref/tags/{recovery.TAG}":
            return self.ref
        if path == f"{self.prefix}/git/tags/{recovery.TAG_OBJECT}":
            return self.tag
        if path == f"{self.prefix}/actions/artifacts/{recovery.FAILED_REPORT_ARTIFACT_ID}":
            return self.report_artifact_detail
        if path.startswith(f"{self.prefix}/actions/runs/"):
            remainder = path[len(f"{self.prefix}/actions/runs/"):]
            parts = remainder.split("/")
            run_id = int(parts[0])
            if len(parts) == 1:
                return dict(self.runs[run_id])
            if len(parts) == 3 and parts[1] == "attempts":
                return dict(self.runs[run_id])
        raise AssertionError(f"unexpected API path: {path}")

    def list(self, path, key=None):
        self.calls.append(("LIST", path))
        if path == f"{self.prefix}/actions/runs/{recovery.FAILED_PUBLISH_RUN_ID}/artifacts":
            return [self.report_artifact]
        if path == f"{self.prefix}/actions/runs/{recovery.TAG_BUILD_RUN_ID}/artifacts":
            return list(self.build_artifacts)
        for run_id, jobs in self.jobs.items():
            expected = f"{self.prefix}/actions/runs/{run_id}/attempts/"
            if path.startswith(expected) and path.endswith("/jobs"):
                return [dict(job) for job in jobs]
        raise AssertionError(f"unexpected list path: {path}")

    def download(self, path, destination):
        self.calls.append(("DOWNLOAD", path))
        if path != f"{self.prefix}/actions/artifacts/{recovery.FAILED_REPORT_ARTIFACT_ID}/zip":
            raise AssertionError(f"unexpected download path: {path}")
        Path(destination).write_bytes(self.report_zip)


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeAPI()
        self.env = {
            "GITHUB_TOKEN": "test-token",
            "GITHUB_EVENT_NAME": "workflow_dispatch",
            "GITHUB_REPOSITORY": recovery.REPOSITORY,
            "GITHUB_REF": "refs/heads/main",
            "GITHUB_SHA": TOOL_SHA,
            "GITHUB_WORKFLOW_SHA": TOOL_SHA,
            "GITHUB_RUN_ID": str(RECOVERY_RUN_ID),
            "GITHUB_RUN_ATTEMPT": "1",
            "RECOVERY_TAG": recovery.TAG,
            "EXPECTED_TAG_OBJECT": recovery.TAG_OBJECT,
            "FAILED_PUBLISH_RUN_ID": str(recovery.FAILED_PUBLISH_RUN_ID),
            "FAILED_PUBLISH_RUN_ATTEMPT": str(recovery.FAILED_PUBLISH_ATTEMPT),
            "TAG_BUILD_RUN_ID": str(recovery.TAG_BUILD_RUN_ID),
            "TAG_BUILD_RUN_ATTEMPT": str(recovery.TAG_BUILD_ATTEMPT),
        }
        self.event = {
            "repository": {"id": recovery.REPOSITORY_ID,
                           "full_name": recovery.REPOSITORY, "fork": False},
            "ref": "refs/heads/main",
            "inputs": {
                "tag": recovery.TAG,
                "expected_tag_object": recovery.TAG_OBJECT,
                "failed_publish_run_id": recovery.FAILED_PUBLISH_RUN_ID,
                "failed_publish_run_attempt": recovery.FAILED_PUBLISH_ATTEMPT,
                "tag_build_run_id": recovery.TAG_BUILD_RUN_ID,
                "tag_build_run_attempt": recovery.TAG_BUILD_ATTEMPT,
            },
        }

    def validate(self):
        return recovery.Recovery(self.api, self.env, self.event).validate()

    def test_accepts_exact_workflow_dispatch_and_original_provenance(self):
        result = self.validate()
        self.assertEqual(result["tooling_sha"], TOOL_SHA)
        self.assertEqual(result["product_sha"], recovery.PRODUCT_SHA)
        self.assertEqual(result["tag_object"], recovery.TAG_OBJECT)
        self.assertEqual(result["failed_publisher"]["report_artifact_id"],
                         recovery.FAILED_REPORT_ARTIFACT_ID)
        self.assertEqual(result["tag_build"]["release_candidate_artifact_id"],
                         recovery.TAG_BUILD_ARTIFACT_ID)
        self.assertIn(("DOWNLOAD",
                       f"{self.api.prefix}/actions/artifacts/{recovery.FAILED_REPORT_ARTIFACT_ID}/zip"),
                      self.api.calls)

    def test_rejects_spoofed_dispatch_event_and_inputs(self):
        cases = (
            ("event name", {"GITHUB_EVENT_NAME": "push"}, None),
            ("event ref", {}, {"ref": f"refs/tags/{recovery.TAG}"}),
            ("input mismatch", {}, {"inputs": {**self.event["inputs"], "tag": "v9.9.9"}}),
            ("fork payload", {}, {"repository": {"id": 123, "full_name": "fork/px4",
                                                  "fork": True}}),
        )
        for label, env_change, event_change in cases:
            with self.subTest(label=label), self.assertRaises(publish.ReleaseError):
                env = {**self.env, **env_change}
                event = {**self.event, **(event_change or {})}
                recovery.Recovery(self.api, env, event).validate()

    def test_rejects_wrong_repository_ref_tooling_sha_and_attempt(self):
        cases = (
            ("repository", {"GITHUB_REPOSITORY": "fork/px4-userland"}, None),
            ("ref", {"GITHUB_REF": f"refs/tags/{recovery.TAG}"}, None),
            ("workflow SHA", {"GITHUB_WORKFLOW_SHA": "f" * 40}, None),
            ("run attempt", {"GITHUB_RUN_ATTEMPT": "2"}, None),
        )
        for label, env_change, event_change in cases:
            with self.subTest(label=label), self.assertRaises(publish.ReleaseError):
                recovery.Recovery(self.api, {**self.env, **env_change},
                                  {**self.event, **(event_change or {})}).validate()

    def test_rejects_wrong_recovery_workflow_path_or_SHA(self):
        for field, value in (("path", ".github/workflows/release_tag.yml"),
                             ("head_sha", "f" * 40)):
            with self.subTest(field=field), self.assertRaises(publish.ReleaseError):
                self.setUp()
                self.api.runs[RECOVERY_RUN_ID][field] = value
                self.validate()

    def test_rejects_tag_object_movement(self):
        self.api.ref["object"]["sha"] = "f" * 40
        with self.assertRaises(publish.ReleaseError):
            self.validate()

    def test_rejects_failed_publisher_with_wrong_identity_or_attempt(self):
        changes = (
            ("workflow_id", 7),
            ("path", ".github/workflows/other.yml"),
            ("head_branch", "main"),
            ("head_sha", "f" * 40),
            ("run_attempt", 2),
        )
        for field, value in changes:
            with self.subTest(field=field), self.assertRaises(publish.ReleaseError):
                self.setUp()
                self.api.runs[recovery.FAILED_PUBLISH_RUN_ID][field] = value
                self.validate()

    def test_rejects_unrelated_failure_report_or_publication_attempt(self):
        for field, value in (("phase", "publish-latest"),
                             ("publication_attempted", True),
                             ("release_id", 42),
                             ("error", "unrelated failure")):
            with self.subTest(field=field), self.assertRaises(publish.ReleaseError):
                self.setUp()
                self.api.report[field] = value
                self.api.report_zip = zip_report(self.api.report)
                self.api.report_digest = "sha256:" + hashlib.sha256(self.api.report_zip).hexdigest()
                self.api.report_artifact["digest"] = self.api.report_digest
                self.api.report_artifact_detail.update(
                    digest=self.api.report_digest, size_in_bytes=len(self.api.report_zip))
                self.validate()

    def test_rejects_failed_or_mismatched_original_tag_build(self):
        run = self.api.runs[recovery.TAG_BUILD_RUN_ID]
        run["conclusion"] = "failure"
        with self.assertRaises(publish.ReleaseError):
            self.validate()

    def test_rejects_wrong_original_tag_build_attempt(self):
        self.api.runs[recovery.TAG_BUILD_RUN_ID]["run_attempt"] = 2
        with self.assertRaises(publish.ReleaseError):
            self.validate()

    def test_rejects_build_job_failure_and_artifact_identity_change(self):
        self.api.jobs[recovery.TAG_BUILD_RUN_ID][0]["conclusion"] = "failure"
        with self.assertRaises(publish.ReleaseError):
            self.validate()
        self.setUp()
        self.api.build_artifacts[0]["id"] += 1
        with self.assertRaises(publish.ReleaseError):
            self.validate()

    def test_rejects_publisher_job_shape_or_report_digest_mismatch(self):
        self.api.jobs[recovery.FAILED_PUBLISH_RUN_ID][1]["conclusion"] = "success"
        with self.assertRaises(publish.ReleaseError):
            self.validate()
        self.setUp()
        self.api.report_artifact_detail["digest"] = "sha256:" + "0" * 64
        with self.assertRaises(publish.ReleaseError):
            self.validate()

    def test_recovery_workflow_is_dispatch_only_and_least_privilege(self):
        root = Path(__file__).resolve().parents[1]
        workflow = (root / ".github/workflows/release_recovery.yml").read_text()
        ordinary = (root / ".github/workflows/release_tag.yml").read_text()
        self.assertIn("workflow_dispatch:", workflow)
        self.assertIn("github.ref == 'refs/heads/main'", workflow)
        self.assertIn("stable-release-${{ github.repository }}", workflow)
        self.assertIn("contents: read", workflow)
        self.assertIn("contents: write", workflow)
        self.assertIn("release_recover.py verify", workflow)
        self.assertIn("release_recover.py publish", workflow)
        self.assertNotIn("workflow_dispatch:", ordinary)
        self.assertIn("uses: ./.github/workflows/release_reusable.yml", ordinary)


if __name__ == "__main__":
    unittest.main()
