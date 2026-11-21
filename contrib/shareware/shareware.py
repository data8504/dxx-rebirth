#!/usr/bin/env python3
"""Preserve original DOS distributions in reproducible, content-addressed ZIPs."""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import urllib.parse
import urllib.request
import zipfile


HERE = Path(__file__).resolve().parent


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def catalogue(path):
    packages = json.loads(path.read_text())
    if set(packages) != {"d1", "d2"}:
        raise ValueError("catalogue must contain exactly d1 and d2")
    for game, package in packages.items():
        if not re.fullmatch(r"[a-z0-9]+\.exe", package["filename"]):
            raise ValueError(f"{game}: invalid original filename")
        if not re.fullmatch(r"[a-z0-9]+", package["edition"]):
            raise ValueError(f"{game}: invalid edition")
        if not re.fullmatch(r"[0-9a-f]{64}", package["sha256"]):
            raise ValueError(f"{game}: invalid SHA-256")
        if not isinstance(package["size"], int) or not 0 < package["size"] < 100_000_000:
            raise ValueError(f"{game}: invalid distribution size")
        if not package["sources"] or any(
            urllib.parse.urlsplit(url).scheme != "https" for url in package["sources"]
        ):
            raise ValueError(f"{game}: sources must be HTTPS URLs")
        for key in ("title", "version"):
            if not isinstance(package[key], str) or not package[key]:
                raise ValueError(f"{game}: missing {key}")
    return packages


def download(package):
    errors = []
    for url in package["sources"]:
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                if urllib.parse.urlsplit(response.url).scheme != "https":
                    raise ValueError("redirected to a non-HTTPS URL")
                data = response.read(package["size"] + 1)
            if len(data) != package["size"] or sha256(data) != package["sha256"]:
                raise ValueError("download does not match approved size and SHA-256")
            return data
        except (OSError, ValueError) as error:
            errors.append(f"{url}: {error}")
    raise ValueError("\n".join(errors))


def bundle(game, package, original, output):
    if len(original) != package["size"] or sha256(original) != package["sha256"]:
        raise ValueError(f"{game}: original distribution failed verification")
    provenance = {"schema": 1, "game": game, "distribution": package}
    readme = (
        f"{package['title']} — original version {package['version']}\n\n"
        f"original/{package['filename']} is the complete original distribution,\n"
        "preserved byte for byte, including its original notices and terms.\n"
        "It is a legacy self-extracting archive; do not launch it. Extract its\n"
        "contents using archive tools to obtain the game data and documentation.\n"
        "The original notices and terms govern the distribution's contents.\n\n"
        "provenance.json records the source, original filename, size and SHA-256.\n"
        "The outer ZIP uses stored entries with fixed metadata for reproducibility.\n"
    ).encode("utf-8")
    entries = {
        "README.txt": readme,
        f"original/{package['filename']}": original,
        "provenance.json": json_bytes(provenance),
    }
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_STORED) as archive:
        for name, data in sorted(entries.items()):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    data = buffer.getvalue()
    digest = sha256(data)
    tag = f"data-{game}-{package['edition']}-sha256-{digest[:12]}"
    directory = output / game
    directory.mkdir(parents=True, exist_ok=True)
    filename = f"descent{game[1:]}-dos-{package['edition']}.zip"
    manifest = dict(provenance, tag=tag, asset={
        "filename": filename, "size": len(data), "sha256": digest,
    })
    files = {filename: data, "manifest.json": json_bytes(manifest)}
    files["SHA256SUMS"] = "".join(
        f"{sha256(content)}  {name}\n" for name, content in sorted(files.items())
    ).encode("ascii")
    for name, content in files.items():
        (directory / name).write_bytes(content)
    return {"tag": tag, "title": f"{package['title']} — original version {package['version']}",
            "files": files, "directory": directory}


class GitHub:
    def __init__(self, repository):
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
            raise ValueError("repository must be OWNER/REPO")
        self.repository = repository

    def api(self, endpoint, method="GET", payload=None, missing_ok=False, binary=False):
        command = ["gh", "api", f"repos/{self.repository}/{endpoint}", "--method", method]
        if binary:
            command += ["-H", "Accept: application/octet-stream"]
        if payload is not None:
            command += ["--input", "-"]
        result = subprocess.run(command, input=None if payload is None else json_bytes(payload),
                                capture_output=True, check=False)
        if result.returncode:
            error = result.stderr.decode("utf-8", errors="replace")
            if missing_ok and "(HTTP 404)" in error:
                return None
            raise ValueError(f"GitHub {method} {endpoint}: {error.strip()}")
        return result.stdout if binary else json.loads(result.stdout)

    def release(self, tag):
        release = self.api(f"releases/tags/{tag}", missing_ok=True)
        if release is not None:
            return release
        # The REST tag endpoint only finds published releases. Authenticated
        # listings also expose drafts, including interrupted publications.
        page = 1
        while True:
            batch = self.api(f"releases?per_page=100&page={page}")
            matches = [item for item in batch if item["tag_name"] == tag]
            if matches:
                if len(matches) != 1:
                    raise ValueError(f"{tag}: multiple releases claim the same tag")
                return matches[0]
            if len(batch) < 100:
                return None
            page += 1

    def ref(self, tag):
        return self.api(f"git/ref/tags/{tag}", missing_ok=True)

    def assets(self, release):
        # Use pagination: never mistake an incomplete list for an empty draft.
        assets = []
        page = 1
        while True:
            batch = self.api(f"releases/{release['id']}/assets?per_page=100&page={page}")
            assets.extend(batch)
            if len(batch) < 100:
                return assets
            page += 1

    def upload(self, publication, names):
        subprocess.run(["gh", "release", "upload", publication["tag"],
                        *[str(publication["directory"] / name) for name in names],
                        "--repo", self.repository], check=True)


def verify_assets(github, publication, release, complete):
    expected = publication["files"]
    assets = github.assets(release)
    names = [asset["name"] for asset in assets]
    if len(names) != len(set(names)) or set(names) - expected.keys():
        raise ValueError(f"{publication['tag']}: unexpected or duplicate release assets")
    if complete and set(names) != expected.keys():
        raise ValueError(f"{publication['tag']}: published release is incomplete")
    for asset in assets:
        content = expected[asset["name"]]
        if asset["state"] != "uploaded" or asset["size"] != len(content):
            raise ValueError(f"{publication['tag']}: incomplete/conflicting asset {asset['name']}")
        downloaded = github.api(f"releases/assets/{asset['id']}", binary=True)
        if sha256(downloaded) != sha256(content):
            raise ValueError(f"{publication['tag']}: conflicting asset {asset['name']}")
    return set(expected) - set(names)


def plan(github, publications, commit):
    plans = []
    for publication in publications:
        tag = publication["tag"]
        release = github.release(tag)
        ref = github.ref(tag)
        if release is not None:
            if (ref is None or ref["object"]["type"] != "commit"
                    or ref["object"]["sha"] != release["target_commitish"]):
                raise ValueError(f"{tag}: release tag does not match its recorded commit")
            missing = verify_assets(github, publication, release, complete=not release["draft"])
            if not release["draft"] and not release.get("immutable", False):
                raise ValueError(f"{tag}: existing published release is not immutable")
            status = "resume draft" if release["draft"] else "already published"
        else:
            if ref is not None and (ref["object"]["type"] != "commit" or ref["object"]["sha"] != commit):
                raise ValueError(f"{tag}: orphan tag points elsewhere; inspect before retrying")
            missing = set(publication["files"])
            status = "create release"
        plans.append((publication, release, ref, missing, status))
        print(f"{tag}: {status}")
    return plans


def publish(github, plans, commit):
    # GITHUB_TOKEN cannot read the admin-only immutable-releases setting.
    # The workflow requires a repository opt-in; verify actual protection after publishing.
    for publication, release, ref, missing, status in plans:
        if status == "already published":
            continue
        tag = publication["tag"]
        if release is None:
            if ref is None:
                github.api("git/refs", "POST", {"ref": f"refs/tags/{tag}", "sha": commit})
            release = github.api("releases", "POST", {
                "tag_name": tag, "target_commitish": commit, "name": publication["title"],
                "draft": True, "prerelease": False, "make_latest": "false",
                "body": "Original DOS distribution preserved unchanged inside a reproducible ZIP.\n\n"
                        "See manifest.json for provenance and SHA256SUMS for download checksums.\n",
            })
        if missing:
            github.upload(publication, sorted(missing))
        verify_assets(github, publication, release, complete=True)
        github.api(f"releases/{release['id']}", "PATCH", {"draft": False, "make_latest": "false"})
        published = github.release(tag)
        if published["draft"] or not published.get("immutable", False):
            raise ValueError(f"{tag}: publication did not become immutable; inspect release")
        verify_assets(github, publication, published, complete=True)
        print(f"https://github.com/{github.repository}/releases/tag/{tag}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("build", "refresh"))
    parser.add_argument("--catalogue", type=Path, default=HERE / "catalogue.json")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--game", choices=("all", "d1", "d2"), default="all")
    parser.add_argument("--repository", help="OWNER/REPO; required for refresh")
    parser.add_argument("--commit", help="full commit SHA; required for refresh")
    parser.add_argument("--publish", action="store_true", help="publish; default is read-only")
    args = parser.parse_args()
    if args.publish and args.command != "refresh":
        parser.error("--publish requires refresh")
    if args.command == "refresh" and (not args.repository or not re.fullmatch(r"[0-9a-f]{40}", args.commit or "")):
        parser.error("refresh requires --repository OWNER/REPO and --commit FULL_SHA")
    packages = catalogue(args.catalogue)
    publications = []
    for game, package in sorted(packages.items()):
        if args.game in ("all", game):
            publication = bundle(game, package, download(package), args.output)
            publications.append(publication)
            print(f"{publication['tag']}: built {len(next(iter(publication['files'].values())))} bytes")
    if args.command == "refresh":
        github = GitHub(args.repository)
        plans = plan(github, publications, args.commit)
        if args.publish:
            publish(github, plans, args.commit)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as stream:
            stream.write("Shareware data identities (full hashes are in the generated manifests):\n\n")
            stream.writelines(f"- `{publication['tag']}`\n" for publication in publications)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
