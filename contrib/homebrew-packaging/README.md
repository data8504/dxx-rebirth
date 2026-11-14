# Homebrew runtime packaging experiment

Development experiment for DXX-Rebirth #863. No release workflow or artifact
should use this branch. `macos_homebrew_runtime_deps` defaults to false.

## Reproduce on a disposable macOS 26 ARM runner

This branch includes the exact changes from pending upstream PRs #885 and #886
as prerequisite commits: the `<array>` include and obsolete Homebrew tap removal.
The packaging changes are separate commits after those prerequisites.

```
brew bundle --verbose
python3 contrib/homebrew-packaging/test_discovery.py
scons -j4 opengl=1 sdl2=1 macos_add_frameworks=0 macos_bundle_libs=0 build/D{1,2}X-Rebirth.app
python3 contrib/homebrew-packaging/evaluate.py
python3 contrib/homebrew-packaging/incremental.py
```

`evaluate.py` temporarily renames the entire Homebrew prefix using sudo and
restores it in a finally block. Use an expendable runner, not a personal Mac.
It compares original dylibbundler packaging, the opt-in generic helper, and a
small SDL-specific explicit seed. It measures ZIP sizes then deletes the ZIPs.
Apps remain under `packaging-evidence/{original,generic,targeted}` for local
inspection. The experiment workflow uploads logs and JSON only, never apps.

Startup uses `-help` with `DYLD_PRINT_LIBRARIES=1` and cleared SDL/DYLD overrides.
This tests library loading and command-line startup, not gameplay, audio,
rendering, input, Gatekeeper, Developer ID signing, or notarization. Signatures
are ad-hoc and verified using `codesign --verify --deep --strict`.

## Production scope

The helper is in `common/arch/cocoa/homebrew_bundle.py`; tests and evaluation
scripts are separate. It inspects absolute third-party paths from `otool -L`,
resolves symlinks to Cellar rack/version pairs, consumes installed entries from
`brew info --json=v2 --installed --formula`, and recursively selects top-level
`lib/*.dylib` and their aliases. Selected dylibs re-enter linked inspection and
are copied beside existing bundled libraries, then passed as writable `-x`
inputs to the existing dylibbundler. No installed library is modified.

Top-level formula dependencies are deliberately not used: they describe current
formula definitions. Installed runtime lists preserve declared non-linked edges
but may record dependency versions removed by normal upgrades. The experiment
uses the exact installed version if available, otherwise the matching active opt
keg. It records requested/selected substitutions in comparison evidence; their
ABI compatibility is an assumption tested only for the actual runner inputs.
Build/test-only formula dependencies are not traversed.

Supported envelope: one conventional Homebrew installation; current-name Cellar
racks; dylibs with absolute non-system load paths; flat, collision-free top-level
dylib layouts; architecture-compatible installed dependencies. Filename collisions
and unsupported relative dependency paths fail before copying. Ordinary aliases
and prefix/opt symlinks resolving to a canonical current-name keg are supported;
historical renamed racks are not. Non-Homebrew absolute linked libraries stay
in normal dylibbundler processing. The default packaging path is unchanged.

Unsupported: resolving @rpath/inherited loader contexts, frameworks, plugins,
helpers, runtime data, multiple Homebrew prefixes, automatic deployment-target
or architecture selection, or general runtime lookup repair. Selecting every
top-level dylib may overbundle. Package metadata does not specify which files
are needed, where applications search for them, or redistribution obligations.
Existing signing/notarization remains responsible for final distribution signing.

The SCons integration deliberately uses AlwaysBuild and restores the unmodified
compiled executable before repackaging. Thus dependency changes/removals are
re-evaluated without an incremental cache, and unchanged builds also pay the
packaging cost. The existing signing target depends on the packaging operation.
This is a measured limitation, not a proposed release optimization.

Disable `macos_homebrew_runtime_deps`, remove the experimental output bundles,
and rebuild to return to original packaging. No production deployment is changed.
