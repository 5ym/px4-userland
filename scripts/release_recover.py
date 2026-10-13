#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Narrow, separately dispatched recovery for the verified v0.2.1 tag failure.

The workflow checkout is release tooling. The product commit always comes from
the immutable annotated tag and is checked independently.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import sys
import tempfile

import release_publish as publish


PRODUCT = "px4-userland"
REPOSITORY = "Khronos31/px4-userland"
REPOSITORY_ID = 1353579541
TAG = "v0.2.1"
PRODUCT_SHA = "dd1c1bea16a5871085d5f2a42c009364c1ec7345"
TAG_OBJECT = "4939fdec32900a807252586e807e97f19e6b6612"
TAG_BUILD_RUN_ID = 38330455992
TAG_BUILD_ATTEMPT = 1
TAG_BUILD_WORKFLOW_ID = 349002699
TAG_BUILD_WORKFLOW_PATH = ".github/workflows/build_userland.yml"
TAG_BUILD_ARTIFACT_ID = 11782756340
TAG_PROVENANCE_ARTIFACT_ID = 11782641618
FAILED_PUBLISH_RUN_ID = 38330456338
FAILED_PUBLISH_ATTEMPT = 1
FAILED_PUBLISH_WORKFLOW_ID = 380986299
FAILED_PUBLISH_WORKFLOW_PATH = ".github/workflows/release_tag.yml"
FAILED_REPORT_ARTIFACT_ID = 11782910579
FAILED_REPORT_ARTIFACT_NAME = "release-verification-1"
RECOVERY_WORKFLOW_PATH = ".github/workflows/release_recovery.yml"
RECOVERY_REPORT_NAME = "release-verification.json"
EXPECTED_DISPATCH_INPUTS = {
    "tag": TAG,
    "expected_tag_object": TAG_OBJECT,
    "failed_publish_run_id": str(FAILED_PUBLISH_RUN_ID),
    "failed_publish_run_attempt": str(FAILED_PUBLISH_ATTEMPT),
    "tag_build_run_id": str(TAG_BUILD_RUN_ID),
    "tag_build_run_attempt": str(TAG_BUILD_ATTEMPT),
}
INPUT_ENV = {
    "tag": "RECOVERY_TAG",
    "expected_tag_object": "EXPECTED_TAG_OBJECT",
    "failed_publish_run_id": "FAILED_PUBLISH_RUN_ID",
    "failed_publish_run_attempt": "FAILED_PUBLISH_RUN_ATTEMPT",
    "tag_build_run_id": "TAG_BUILD_RUN_ID",
    "tag_build_run_attempt": "TAG_BUILD_RUN_ATTEMPT",
}


def input_text(value, name):
    publish.require(not isinstance(value, bool) and isinstance(value, (str, int)),
                    f"invalid dispatch input: {name}")
    return str(value)


class Recovery:
    def __init__(self, api, env, event):
        self.api = api
        self.env = env
        self.event = event
        self.prefix = f"/repos/{REPOSITORY}"

    def _selectors(self):
        publish.require(self.api.repository == REPOSITORY, "unexpected recovery repository")
        publish.require(self.env.get("GITHUB_EVENT_NAME") == "workflow_dispatch",
                        "recovery requires workflow_dispatch")
        publish.require(self.env.get("GITHUB_REPOSITORY") == REPOSITORY,
                        "recovery is scoped to the PX4 repository")
        publish.require(self.env.get("GITHUB_REF") == "refs/heads/main",
                        "recovery must run from refs/heads/main")
        publish.require(isinstance(self.event, dict), "invalid workflow dispatch event")
        repository = self.event.get("repository", {})
        publish.require(repository.get("full_name") == REPOSITORY
                        and repository.get("id") == REPOSITORY_ID
                        and repository.get("fork") is False,
                        "workflow dispatch event is from a fork or different repository")
        publish.require(self.event.get("ref") == "refs/heads/main",
                        "workflow dispatch payload is not scoped to main")
        inputs = self.event.get("inputs")
        publish.require(isinstance(inputs, dict) and set(inputs) == set(EXPECTED_DISPATCH_INPUTS),
                        "workflow dispatch inputs are incomplete or unexpected")
        for name, expected in EXPECTED_DISPATCH_INPUTS.items():
            event_value = input_text(inputs[name], name)
            env_value = self.env.get(INPUT_ENV[name])
            publish.require(event_value == expected and env_value == expected,
                            f"unexpected recovery selector: {name}")

        workflow_sha = self.env.get("GITHUB_WORKFLOW_SHA", "")
        tooling_sha = self.env.get("GITHUB_SHA", "")
        publish.require(publish.SHA_RE.fullmatch(workflow_sha)
                        and tooling_sha == workflow_sha,
                        "workflow definition and tooling checkout must use GITHUB_SHA")
        run_id = self._positive_env_int("GITHUB_RUN_ID")
        attempt = self._positive_env_int("GITHUB_RUN_ATTEMPT")
        return {
            "tooling_sha": tooling_sha,
            "workflow_sha": workflow_sha,
            "recovery_run_id": run_id,
            "recovery_run_attempt": attempt,
        }

    def _positive_env_int(self, name):
        value = self.env.get(name, "")
        publish.require(isinstance(value, str) and re.fullmatch(r"[1-9][0-9]*", value),
                        f"invalid {name}")
        return int(value)

    @staticmethod
    def _repository_identity(run):
        for key in ("repository", "head_repository"):
            repository = run.get(key, {})
            publish.require(repository.get("id") == REPOSITORY_ID
                            and repository.get("full_name") == REPOSITORY
                            and repository.get("fork") is False,
                            "workflow run is from a fork or different repository")

    def _check_run(self, run, *, run_id, attempt, path, workflow_id=None,
                   event, head_branch, head_sha, status, conclusion):
        publish.require(run.get("id") == run_id and run.get("run_attempt") == attempt,
                        "workflow run identity or attempt changed")
        actual_workflow_id = publish.positive_id(run.get("workflow_id"), "workflow id")
        if workflow_id is not None:
            publish.require(actual_workflow_id == workflow_id,
                            "workflow run has the wrong workflow id")
        publish.require(run.get("path") == path and run.get("event") == event
                        and run.get("head_branch") == head_branch
                        and run.get("head_sha") == head_sha
                        and run.get("status") == status
                        and run.get("conclusion") == conclusion,
                        "workflow run does not match the recovery identity")
        self._repository_identity(run)
        publish.require(run.get("pull_requests") == [],
                        "PR-associated workflow runs cannot be used for recovery")
        return actual_workflow_id

    def _current_and_attempt(self, run_id, attempt, **expected):
        current = self.api.json("GET", f"{self.prefix}/actions/runs/{run_id}")
        exact = self.api.json("GET", f"{self.prefix}/actions/runs/{run_id}/attempts/{attempt}")
        current_workflow = self._check_run(current, run_id=run_id, attempt=attempt, **expected)
        exact_workflow = self._check_run(exact, run_id=run_id, attempt=attempt, **expected)
        publish.require(current_workflow == exact_workflow,
                        "current run and exact attempt use different workflows")
        return current, exact

    def _check_recovery_run(self, context):
        expected = {
            "run_id": context["recovery_run_id"],
            "attempt": context["recovery_run_attempt"],
            "path": RECOVERY_WORKFLOW_PATH,
            "event": "workflow_dispatch",
            "head_branch": "main",
            "head_sha": context["tooling_sha"],
            "status": "in_progress",
            "conclusion": None,
        }
        current, exact = self._current_and_attempt(**expected)
        current_id = publish.positive_id(current.get("workflow_id"), "recovery workflow id")
        publish.require(exact.get("workflow_id") == current_id,
                        "recovery workflow identity changed between run and attempt")

    def _check_product_tag(self):
        reference = self.api.json("GET", f"{self.prefix}/git/ref/tags/{TAG}")
        obj = reference.get("object", {})
        publish.require(reference.get("ref") == f"refs/tags/{TAG}"
                        and obj.get("type") == "tag"
                        and obj.get("sha") == TAG_OBJECT,
                        "the annotated product tag moved or changed")
        tag = self.api.json("GET", f"{self.prefix}/git/tags/{TAG_OBJECT}")
        publish.require(tag.get("sha") == TAG_OBJECT and tag.get("tag") == TAG
                        and tag.get("object", {}).get("type") == "commit"
                        and tag["object"].get("sha") == PRODUCT_SHA,
                        "the immutable product tag does not resolve to the approved commit")

    def _check_failed_publisher_jobs(self, run_id, attempt):
        jobs = self.api.list(
            f"{self.prefix}/actions/runs/{run_id}/attempts/{attempt}/jobs", "jobs",
        )
        publish.require(len(jobs) == 2, "failed publisher job inventory changed")
        by_name = {job.get("name"): job for job in jobs}
        publish.require(set(by_name) == {"release / verify", "release / publish"},
                        "failed publisher job names changed")
        verify, publication = by_name["release / verify"], by_name["release / publish"]
        publish.require(verify.get("status") == "completed"
                        and verify.get("conclusion") == "failure"
                        and publication.get("status") == "completed"
                        and publication.get("conclusion") == "skipped",
                        "original publisher failure was not a pre-publication verification failure")
        for job in jobs:
            publish.require(job.get("run_id") == run_id
                            and job.get("head_sha") == PRODUCT_SHA,
                            "original publisher job belongs to another run or commit")

    def _check_failed_report(self, run_id, attempt, directory):
        listed = self.api.list(f"{self.prefix}/actions/runs/{run_id}/artifacts", "artifacts")
        matches = [item for item in listed
                   if item.get("id") == FAILED_REPORT_ARTIFACT_ID
                   and item.get("name") == FAILED_REPORT_ARTIFACT_NAME]
        publish.require(len(matches) == 1, "original verification report artifact is missing or ambiguous")
        artifact = self.api.json(
            "GET", f"{self.prefix}/actions/artifacts/{FAILED_REPORT_ARTIFACT_ID}",
        )
        publish.require(artifact.get("id") == FAILED_REPORT_ARTIFACT_ID
                        and artifact.get("name") == FAILED_REPORT_ARTIFACT_NAME
                        and artifact.get("expired") is False,
                        "original verification report artifact is not available")
        listed_digest = matches[0].get("digest")
        digest = artifact.get("digest")
        publish.require(isinstance(digest, str)
                        and re.fullmatch(r"sha256:[0-9a-f]{64}", digest)
                        and listed_digest == digest,
                        "original report artifact digest is missing or changed")
        identity = artifact.get("workflow_run", {})
        publish.require(identity.get("id") == run_id
                        and identity.get("head_sha") == PRODUCT_SHA
                        and identity.get("repository_id") == REPOSITORY_ID
                        and identity.get("head_repository_id") == REPOSITORY_ID,
                        "original report artifact is not bound to the failed publisher run")
        try:
            expiry = datetime.fromisoformat(artifact["expires_at"].replace("Z", "+00:00"))
            size = artifact["size_in_bytes"]
            publish.require(expiry > datetime.now(timezone.utc)
                            and type(size) is int and 0 < size <= publish.MAX_DOWNLOAD,
                            "original report artifact is expired or exceeds the download limit")
        except (KeyError, ValueError, TypeError) as exc:
            raise publish.ReleaseError("invalid original report artifact metadata") from exc
        archive = Path(directory) / "failed-verification.zip"
        self.api.download(f"{self.prefix}/actions/artifacts/{FAILED_REPORT_ARTIFACT_ID}/zip",
                          archive)
        publish.require(archive.stat().st_size == size and publish.sha256(archive) == digest[7:],
                        "original report artifact bytes do not match its identity")
        folder = Path(directory) / "failed-verification"
        publish.extract_artifact(archive, folder, (RECOVERY_REPORT_NAME,))
        report = publish.parse_json((folder / RECOVERY_REPORT_NAME).read_bytes())
        publish.require(report.get("mode") == "verify"
                        and report.get("product") == PRODUCT
                        and report.get("status") == "blocked"
                        and report.get("phase") == "verify-inputs"
                        and report.get("publication_attempted") is False
                        and "release_id" in report and report["release_id"] is None
                        and report.get("error") == "unsafe API path",
                        "original verification report does not show the known pre-publication compare-path failure")
        return report

    def _check_original_publisher(self, directory):
        run, exact = self._current_and_attempt(
            FAILED_PUBLISH_RUN_ID, FAILED_PUBLISH_ATTEMPT,
            path=FAILED_PUBLISH_WORKFLOW_PATH,
            workflow_id=FAILED_PUBLISH_WORKFLOW_ID,
            event="push", head_branch=TAG, head_sha=PRODUCT_SHA,
            status="completed", conclusion="failure",
        )
        references = run.get("referenced_workflows", [])
        expected_path = f"{REPOSITORY}/.github/workflows/release_reusable.yml@{PRODUCT_SHA}"
        publish.require(len(references) == 1
                        and references[0].get("path") == expected_path
                        and references[0].get("ref") == f"refs/tags/{TAG}"
                        and references[0].get("sha") == PRODUCT_SHA,
                        "original publisher did not use the product-commit reusable workflow")
        self._check_failed_publisher_jobs(FAILED_PUBLISH_RUN_ID, FAILED_PUBLISH_ATTEMPT)
        self._check_failed_report(FAILED_PUBLISH_RUN_ID, FAILED_PUBLISH_ATTEMPT, directory)
        return {"run_id": run["id"], "run_attempt": run["run_attempt"],
                "workflow_id": FAILED_PUBLISH_WORKFLOW_ID,
                "workflow_path": FAILED_PUBLISH_WORKFLOW_PATH,
                "report_artifact_id": FAILED_REPORT_ARTIFACT_ID}

    def _check_tag_build(self):
        run, exact = self._current_and_attempt(
            TAG_BUILD_RUN_ID, TAG_BUILD_ATTEMPT,
            path=TAG_BUILD_WORKFLOW_PATH,
            workflow_id=TAG_BUILD_WORKFLOW_ID,
            event="push", head_branch=TAG, head_sha=PRODUCT_SHA,
            status="completed", conclusion="success",
        )
        jobs = self.api.list(
            f"{self.prefix}/actions/runs/{TAG_BUILD_RUN_ID}/attempts/{TAG_BUILD_ATTEMPT}/jobs",
            "jobs",
        )
        publish.require(len(jobs) == 20 and all(
            job.get("status") == "completed" and job.get("conclusion") == "success"
            and job.get("run_id") == TAG_BUILD_RUN_ID and job.get("head_sha") == PRODUCT_SHA
            for job in jobs
        ), "original tag build is not the successful 20-job run")
        artifacts = self.api.list(f"{self.prefix}/actions/runs/{TAG_BUILD_RUN_ID}/artifacts",
                                  "artifacts")
        expected = {
            TAG_BUILD_ARTIFACT_ID: publish.ARTIFACT,
            TAG_PROVENANCE_ARTIFACT_ID: "release-provenance",
        }
        for artifact_id, name in expected.items():
            matches = [item for item in artifacts
                       if item.get("id") == artifact_id and item.get("name") == name]
            publish.require(len(matches) == 1, "original tag build artifact identity changed")
        return {"run_id": run["id"], "run_attempt": run["run_attempt"],
                "workflow_id": TAG_BUILD_WORKFLOW_ID,
                "workflow_path": TAG_BUILD_WORKFLOW_PATH,
                "release_candidate_artifact_id": TAG_BUILD_ARTIFACT_ID,
                "provenance_artifact_id": TAG_PROVENANCE_ARTIFACT_ID}

    def validate(self):
        context = self._selectors()
        self._check_recovery_run(context)
        repository = self.api.json("GET", self.prefix)
        publish.require(repository.get("id") == REPOSITORY_ID
                        and repository.get("full_name") == REPOSITORY
                        and repository.get("fork") is False,
                        "GitHub repository identity changed")
        self._check_product_tag()
        with tempfile.TemporaryDirectory(prefix="release-recovery-evidence-") as directory:
            failed = self._check_original_publisher(directory)
        build = self._check_tag_build()
        context.update({
            "product": PRODUCT,
            "repository": REPOSITORY,
            "tag": TAG,
            "product_sha": PRODUCT_SHA,
            "tag_object": TAG_OBJECT,
            "failed_publisher": failed,
            "tag_build": build,
        })
        return context


def execute(mode, env, event, report_path):
    report = {"mode": mode, "product": PRODUCT, "status": "blocked"}
    publisher_instance = None
    result = 1
    try:
        api = publish.GitHub(REPOSITORY, env.get("GITHUB_TOKEN", ""))
        recovery = Recovery(api, env, event)
        context = recovery.validate()
        with tempfile.TemporaryDirectory(prefix="stable-release-recovery-") as directory:
            publisher_instance = publish.Publisher(
                api, PRODUCT, TAG, PRODUCT_SHA, directory,
                expected_tag_object=TAG_OBJECT,
                expected_tag_run_id=TAG_BUILD_RUN_ID,
                expected_tag_run_attempt=TAG_BUILD_ATTEMPT,
                expected_tag_artifact_id=TAG_BUILD_ARTIFACT_ID,
                expected_tag_provenance_artifact_id=TAG_PROVENANCE_ARTIFACT_ID,
            )
            report.update(publisher_instance.prepare())
            report["recovery"] = context
            if mode == "publish":
                # Revalidate all dispatch/original-run evidence after the full
                # offline comparison, immediately before Publisher's mutation gate.
                report["recovery"] = recovery.validate()
                report["publication"] = publisher_instance.publish()
            report["status"] = "success"
            result = 0
    except (publish.ReleaseError, publish.release_verify.VerificationError,
            KeyError, ValueError, OSError) as exc:
        report["error"] = str(exc)
        print(f"release recovery blocked: {exc}", file=sys.stderr)
    if publisher_instance is not None:
        report.update({
            "phase": publisher_instance.phase,
            "publication_attempted": publisher_instance.publication_attempted,
            "release_id": publisher_instance.release_id,
        })
    try:
        Path(report_path).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                                     encoding="utf-8")
    except OSError:
        print("could not preserve recovery report", file=sys.stderr)
        return 1
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("verify", "publish"))
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        event = publish.parse_json(Path(os.environ["GITHUB_EVENT_PATH"]).read_bytes())
    except (KeyError, OSError, publish.ReleaseError) as exc:
        report = {"mode": args.mode, "product": PRODUCT, "status": "blocked",
                  "error": str(exc)}
        try:
            args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                                   encoding="utf-8")
        except OSError:
            print("could not preserve recovery report", file=sys.stderr)
        return 1
    return execute(args.mode, os.environ, event, args.report)


if __name__ == "__main__":
    raise SystemExit(main())
