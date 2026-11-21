"""Offline checks of content identity and interrupted GitHub publication."""

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

    def test_reproducible_archive_preserves_original_and_fixed_metadata(self):
        again = shareware.bundle("d1", self.package, self.original, self.output / "other")
        self.assertEqual(self.publication["files"], again["files"])
        self.assertEqual(self.publication["tag"], again["tag"])
        filename = "descent1-dos-shareware.zip"
        with zipfile.ZipFile(io.BytesIO(again["files"][filename])) as archive:
            self.assertEqual(archive.read("original/desc14sw.exe"), self.original)
            self.assertEqual(archive.namelist(), sorted(archive.namelist()))
            for entry in archive.infolist():
                self.assertEqual(entry.date_time, (1980, 1, 1, 0, 0, 0))
                self.assertEqual(entry.compress_type, zipfile.ZIP_STORED)
                self.assertEqual(entry.external_attr >> 16, 0o100644)
        manifest = json.loads(again["files"]["manifest.json"])
        self.assertEqual(manifest["asset"]["sha256"], shareware.sha256(again["files"][filename]))
        self.assertTrue(again["tag"].endswith(manifest["asset"]["sha256"][:12]))
        self.assertNotIn("obtained", again["files"]["manifest.json"].decode())

    def test_checksum_list_covers_zip_and_manifest(self):
        for line in self.publication["files"]["SHA256SUMS"].decode().splitlines():
            digest, name = line.split("  ")
            self.assertEqual(digest, shareware.sha256(self.publication["files"][name]))

    def test_changed_approved_distribution_changes_identity(self):
        original = self.original + b"new"
        package = dict(self.package, sha256=shareware.sha256(original), size=len(original))
        changed = shareware.bundle("d1", package, original, self.output / "changed")
        self.assertNotEqual(changed["tag"], self.publication["tag"])

    def test_unapproved_original_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "failed verification"):
            shareware.bundle("d1", self.package, b"corrupt", self.output)

    def test_download_checks_hash_and_bounds_read(self):
        response = io.BytesIO(b"x" * self.package["size"])
        response.url = self.package["sources"][0]
        with patch.object(shareware.urllib.request, "urlopen", return_value=response):
            with self.assertRaisesRegex(ValueError, "approved size and SHA-256"):
                shareware.download(self.package)

    def test_download_falls_back_to_verified_mirror(self):
        package = dict(self.package, sources=["https://example.org/broken", "https://example.org/good"])
        response = io.BytesIO(self.original)
        response.url = package["sources"][1]
        with patch.object(shareware.urllib.request, "urlopen", side_effect=[OSError("offline"), response]):
            self.assertEqual(shareware.download(package), self.original)

    def test_https_downgrade_is_rejected(self):
        response = io.BytesIO(self.original)
        response.url = "http://example.org/original.exe"
        with patch.object(shareware.urllib.request, "urlopen", return_value=response):
            with self.assertRaisesRegex(ValueError, "non-HTTPS"):
                shareware.download(self.package)

    def test_no_publication_without_explicit_publish(self):
        plans = shareware.plan(self.github, [self.publication], COMMIT)
        self.assertEqual(plans[0][-1], "create release")
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

    def test_resume_after_tag_creation(self):
        self.github.refs[self.publication["tag"]] = {"object": {"type": "commit", "sha": COMMIT}}
        self.publish()
        self.assertFalse(any(m[0] == "git/refs" for m in self.github.mutations))

    def test_orphan_tag_at_other_commit_is_rejected_before_mutation(self):
        self.github.refs[self.publication["tag"]] = {"object": {"type": "commit", "sha": "b" * 40}}
        with self.assertRaisesRegex(ValueError, "orphan tag"):
            self.publish()
        self.assertEqual(self.github.mutations, [])

    def test_moved_release_tag_is_rejected(self):
        self.publish()
        self.github.refs[self.publication["tag"]]["object"]["sha"] = "b" * 40
        with self.assertRaisesRegex(ValueError, "recorded commit"):
            shareware.plan(self.github, [self.publication], COMMIT)

    def test_full_hash_conflict_under_same_short_tag_is_rejected(self):
        self.publish()
        asset = self.github.releases[self.publication["tag"]]["assets"][0]
        self.github.contents[asset["id"]] = b"x" * asset["size"]
        with self.assertRaisesRegex(ValueError, "conflicting asset"):
            shareware.plan(self.github, [self.publication], COMMIT)

    def test_incomplete_published_release_is_rejected(self):
        self.publish()
        self.github.releases[self.publication["tag"]]["assets"].pop()
        with self.assertRaisesRegex(ValueError, "incomplete"):
            shareware.plan(self.github, [self.publication], COMMIT)

    def test_unexpected_draft_asset_is_rejected(self):
        self.github.interrupt_upload = True
        with self.assertRaises(OSError):
            self.publish()
        self.github.releases[self.publication["tag"]]["assets"][0]["name"] = "unexpected"
        with self.assertRaisesRegex(ValueError, "unexpected"):
            shareware.plan(self.github, [self.publication], COMMIT)

    def test_failed_upload_asset_requires_inspection(self):
        self.github.interrupt_upload = True
        with self.assertRaises(OSError):
            self.publish()
        self.github.releases[self.publication["tag"]]["assets"][0]["state"] = "starter"
        with self.assertRaisesRegex(ValueError, "incomplete/conflicting"):
            shareware.plan(self.github, [self.publication], COMMIT)

    def test_all_packages_are_preflighted_before_mutation(self):
        self.publish()
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

    def test_checked_in_catalogue_validates_both_dos_distributions(self):
        packages = shareware.catalogue(shareware.HERE / "catalogue.json")
        self.assertEqual(packages["d1"]["filename"], "desc14sw.exe")
        self.assertEqual(packages["d2"]["filename"], "d2shar10.exe")

    def test_github_adapter_finds_draft_when_tag_endpoint_returns_404(self):
        github = shareware.GitHub("example/project")
        draft = {"id": 1, "tag_name": self.publication["tag"], "draft": True}
        with patch.object(github, "api", side_effect=[None, [draft]]) as api:
            self.assertEqual(github.release(draft["tag_name"]), draft)
            self.assertEqual(api.call_args_list[1].args, ("releases?per_page=100&page=1",))

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
