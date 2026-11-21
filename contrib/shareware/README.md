# Shareware data publication prototype

This workflow preserves the complete original DOS distributions of Descent 1
shareware and the Descent 2 demo inside reproducible ZIPs. It does not run the
legacy executables, extract/repackage their contents, or change engine releases.
Both distributions are treated as approved inputs. Their original notices and
terms remain inside the unchanged executables; they are not relicensed.

## Build and preview

Python 3.10 or later and its standard library suffice to download and package:

```sh
python3 -m unittest discover -s contrib/shareware -v
python3 contrib/shareware/shareware.py build --output /tmp/shareware
```

Each game's output directory contains one ZIP, `manifest.json`, and `SHA256SUMS`.
The ZIP contains `original/<original-filename>.exe`, `README.txt`, and
`provenance.json`. Consumers verify the ZIP against the full manifest hash before
extracting it; the provenance also records the original executable's full hash.
Obtaining the playable data from the executable requires archive tools; this
prototype does not implement the engine's eventual installer.

ZIP entries are stored without recompression, in sorted order, with fixed
timestamps and permissions. The original executables are already compressed.
No acquisition date, build date, repository name, or commit SHA enters the ZIP.
The publication tag is `data-d1-shareware-sha256-<first12>` or
`data-d2-demo-sha256-<first12>`, identifying the ZIP, not a game version.
The title and manifest state the original distribution version separately.
Changing the helper without changing its output does not create another release.

For a read-only comparison with existing releases, authenticate `gh` and run:

```sh
python3 contrib/shareware/shareware.py refresh --output /tmp/shareware \
  --repository OWNER/REPO --commit "$(git rev-parse HEAD)"
```

`--game d1` or `--game d2` selects one distribution. The default checks both.
Ordinary pushes and PRs validate and build both distributions without publishing
or retaining binary Actions artifacts. The manual workflow defaults to preview.

## Enable publication

Before first publication, enable **immutable releases** in the repository's
Settings > General > Releases, then set the Actions repository variable
`SHAREWARE_IMMUTABLE_RELEASES_ENABLED` to `true`. This is a deliberate opt-in;
neither the helper nor the workflow changes repository settings. The setting
also affects future engine releases: assemble their assets in drafts before
publishing. Existing engine release machinery needs a separate audit before
enabling it on the upstream repository.

GitHub's setting-inspection API requires administration permission, unavailable
to the built-in Actions token. The variable confirms the one-time setup without
requiring a PAT or GitHub App. The helper verifies each actual published release
is immutable and fails if the setup was incorrect. Do not disable immutability
while publication is enabled. Clear the variable to disable workflow publication.

After setup, use **Run workflow**, choose a game, and select **publish**, or push
the command tag at a reviewed commit:

```sh
git tag -f refresh-shareware-data HEAD
git push --force origin refs/tags/refresh-shareware-data
```

The command tag never has a release attached and may move. Pushing an unchanged
tag does not generate an event; use Run workflow to check the same commit again.
The generated content tags never move. The helper publishes within the same
workflow, since tags created by `GITHUB_TOKEN` do not launch other push workflows.
Local publication uses the preview command above with `--publish` after the same
repository setup. Serialize local invocations yourself; workflow runs serialize
publication across refs. GitHub may replace a pending concurrent run, so this
is reconciliation of current desired state, not a queue of every request.

With the built-in Actions token, publish from reviewed commits whose workflow
files agree with the default branch. GitHub rejects release creation when the
target commit changes `.github/workflows/` relative to that branch; this token
cannot receive workflow-write permission. The prototype branch can validate and
preview, but its new workflow must reach the default branch before publishing
through Actions. This does not require a second token.

## Recovery and maintenance

Downloads must match the reviewed size and SHA-256 in `catalogue.json`.
Unexpected upstream bytes fail the run; they never update the catalogue.
The source archive is the icculus.org D2X archive already used in INSTALL.markdown.
Review source/version/hash changes together before updating the catalogue.

Before any release mutation, the helper checks all selected publications.
Matching immutable releases are downloaded and verified, then left unchanged.
Matching partial drafts resume missing uploads. All three assets are downloaded
and verified before publication, and checked again afterwards. Existing releases
can point at an earlier commit if their tagged commit and exact assets agree.
Release notes are descriptive; the uploaded manifest is the immutable metadata.
Data releases explicitly opt out of the repository's Latest designation.

Conflicting bytes, unexpected assets, a full-hash collision under a short tag,
or a moved release tag fail rather than overwrite anything. An interrupted tag
creation can resume at the same commit; an orphan tag at another commit needs
inspection. Failed uploads that leave a non-uploaded asset need manual cleanup
of that draft asset, followed by a rerun. A failure between the two games may
leave one published and one pending; rerunning converges without duplication.

Recovery before publication is limited to inspecting/removing the exact draft
or incomplete draft asset. Published assets are never replaced. A changed
distribution or wrapper creates a new content identity and URL; older consumers
remain pinned until deliberately updated. Keep engine-generated changelog
comparisons anchored to engine tags when resuming engine releases.

This prototype omits scheduling, engine downloads, mirror discovery and engine
CI integration. The same preview command can later be scheduled without
introducing another acquisition or publication path.
