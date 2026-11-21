"""Regressions in artifact identity, preview, and publication/recovery behavior."""

import copy
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import shareware


COMMIT = "a" * 40


class FakeGitHub:
    repository = "example/project"

    def __init__(self):
        self.releases = {}
        self.refs = {}
        self.mutations = []
        self.contents = {}
        self.interrupt_upload = False
        self.immutable = True

    def release(self, tag):
        return copy.deepcopy(self.releases.get(tag))

    def ref(self, tag):
        return copy.deepcopy(self.refs.get(tag))

    def assets(self, release):
        return copy.deepcopy(self.releases[release["tag_name"]]["assets"])

    def api(self, endpoint, method="GET", payload=None, **kwargs):
        if method == "GET" and kwargs.get("binary"):
            return self.contents[int(endpoint.rsplit("/", 1)[1])]
        self.mutations.append((endpoint, method, copy.deepcopy(payload)))
        if endpoint == "git/refs":
            tag = payload["ref"].removeprefix("refs/tags/")
            if tag in self.refs:
                raise ValueError("ref already exists")
            self.refs[tag] = {"object": {"type": "commit", "sha": payload["sha"]}}
            return self.ref(tag)
        if endpoint == "releases":
            tag = payload["tag_name"]
            if tag in self.releases:
                raise ValueError("release already exists")
            self.releases[tag] = dict(payload, id=len(self.releases) + 1, assets=[], immutable=False)
            return self.release(tag)
        if method == "PATCH":
            release = next(r for r in self.releases.values() if str(r["id"]) == endpoint.split("/")[1])
            release.update(payload, immutable=self.immutable)
            return copy.deepcopy(release)
        raise AssertionError((endpoint, method, payload))

    def upload(self, publication, names):
        release = self.releases[publication["tag"]]
        for name in names:
            self.mutations.append(("upload", name))
            content = (publication["directory"] / name).read_bytes()
            identifier = len(self.contents) + 1
            self.contents[identifier] = content
            release["assets"].append({"id": identifier, "name": name,
                                      "state": "uploaded", "size": len(content)})
            if self.interrupt_upload:
                self.interrupt_upload = False
                raise OSError("simulated interruption after one upload")


class SharewareTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.output = Path(self.temporary.name)
        self.original = b"An original self-extracting DOS distribution\x00\xff"
        self.package = {"title": "Descent 1 DOS shareware", "version": "1.4", "edition": "shareware",
                        "filename": "desc14sw.exe", "size": len(self.original),
                        "sha256": shareware.sha256(self.original), "sources": ["https://example.org/original.exe"]}
        self.publication = shareware.bundle("d1", self.package, self.original, self.output)
        self.github = FakeGitHub()

    def publish(self, publications=None, commit=COMMIT):
        plans = shareware.plan(self.github, publications or [self.publication], commit)
        shareware.publish(self.github, plans, commit)

    def test_artifact_identity_is_stable_and_original_is_unchanged(self):
        # A future switch to ZIP's default wall-clock timestamps must not silently
        # turn repeated refreshes into new publications.
        for date in ((2001, 1, 1, 0, 0, 0), (2030, 2, 3, 4, 5, 6)):
            with patch("time.localtime", return_value=date):
                again = shareware.bundle("d1", self.package, self.original, self.output / "other")
            self.assertEqual(self.publication["files"], again["files"])
            self.assertEqual(self.publication["tag"], again["tag"])
        filename = "descent1-dos-shareware.zip"
        with zipfile.ZipFile(io.BytesIO(again["files"][filename])) as archive:
            self.assertEqual(archive.read("original/desc14sw.exe"), self.original)
        manifest = json.loads(again["files"]["manifest.json"])
        self.assertEqual(manifest["asset"]["sha256"], shareware.sha256(again["files"][filename]))
        self.assertTrue(again["tag"].endswith(manifest["asset"]["sha256"][:12]))

    def test_download_rejects_changed_bytes_even_at_the_approved_size(self):
        response = io.BytesIO(b"x" * self.package["size"])
        response.url = self.package["sources"][0]
        with patch.object(shareware.urllib.request, "urlopen", return_value=response):
            with self.assertRaisesRegex(ValueError, "approved size and SHA-256"):
                shareware.download(self.package)

    def test_cli_refresh_defaults_to_preview_without_mutation(self):
        arguments = ["shareware.py", "refresh", "--output", str(self.output),
                     "--game", "d1", "--repository", self.github.repository, "--commit", COMMIT]
        with patch.object(shareware.sys, "argv", arguments), \
                patch.object(shareware, "catalogue", return_value={"d1": self.package}), \
                patch.object(shareware, "download", return_value=self.original), \
                patch.object(shareware, "GitHub", return_value=self.github):
            shareware.main()
        self.assertEqual(self.github.mutations, [])

    def test_second_publication_is_noop_even_at_later_commit(self):
        self.publish()
        mutations = list(self.github.mutations)
        self.publish(commit="b" * 40)
        self.assertEqual(self.github.mutations, mutations)
        release = self.github.release(self.publication["tag"])
        self.assertTrue(release["immutable"])
        self.assertFalse(release["draft"])
        self.assertEqual(release["make_latest"], "false")

    def test_resume_interrupted_draft_without_duplicate_uploads(self):
        self.github.interrupt_upload = True
        with self.assertRaises(OSError):
            self.publish()
        self.assertTrue(self.github.release(self.publication["tag"])["draft"])
        self.publish()
        assets = self.github.assets(self.github.release(self.publication["tag"]))
        self.assertEqual(len(assets), 3)
        self.assertEqual(len([m for m in self.github.mutations if m[0] == "upload"]), 3)

    def test_conflicting_publications_are_never_overwritten(self):
        for conflict in ("bytes", "missing asset", "unexpected asset", "failed upload", "moved tag"):
            with self.subTest(conflict=conflict):
                self.github = FakeGitHub()
                self.publish()
                release = self.github.releases[self.publication["tag"]]
                asset = release["assets"][0]
                if conflict == "bytes":
                    self.github.contents[asset["id"]] = b"x" * asset["size"]
                elif conflict == "missing asset":
                    release["assets"].pop()
                elif conflict == "unexpected asset":
                    asset["name"] = "unexpected"
                elif conflict == "failed upload":
                    release["draft"] = True
                    asset["state"] = "starter"
                else:
                    self.github.refs[self.publication["tag"]]["object"]["sha"] = "b" * 40
                before = list(self.github.mutations)
                with self.assertRaises(ValueError):
                    self.publish()
                self.assertEqual(self.github.mutations, before)

    def test_all_packages_are_preflighted_before_mutation(self):
        other = shareware.bundle("d2", dict(self.package, edition="demo"), self.original, self.output)
        self.github.refs[other["tag"]] = {"object": {"type": "commit", "sha": "b" * 40}}
        before = list(self.github.mutations)
        with self.assertRaisesRegex(ValueError, "orphan tag"):
            self.publish([self.publication, other])
        self.assertEqual(self.github.mutations, before)

    def test_missing_github_immutability_is_detected(self):
        self.github.immutable = False
        with self.assertRaisesRegex(ValueError, "did not become immutable"):
            self.publish()
        with self.assertRaisesRegex(ValueError, "not immutable"):
            shareware.plan(self.github, [self.publication], COMMIT)

    def test_github_adapter_paginates_to_find_old_draft(self):
        github = shareware.GitHub("example/project")
        draft = {"id": 1, "tag_name": self.publication["tag"], "draft": True}
        first_page = [{"tag_name": f"other-{i}"} for i in range(100)]
        with patch.object(github, "api", side_effect=[None, first_page, [draft]]) as api:
            self.assertEqual(github.release(draft["tag_name"]), draft)
            self.assertEqual(api.call_args_list[-1].args, ("releases?per_page=100&page=2",))

    def test_github_adapter_does_not_swallow_authorization_failure(self):
        github = shareware.GitHub("example/project")
        response = shareware.subprocess.CompletedProcess([], 1, b"", b"gh: forbidden (HTTP 403)")
        with patch.object(shareware.subprocess, "run", return_value=response):
            with self.assertRaisesRegex(ValueError, "403"):
                github.release(self.publication["tag"])


if __name__ == "__main__":
    unittest.main()
