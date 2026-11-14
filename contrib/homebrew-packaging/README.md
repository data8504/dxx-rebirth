# SDL2 compatibility runtime packaging repair

This development branch evaluates a targeted repair for DXX-Rebirth #863. It
contains prerequisite patches from pending PRs #885 (the `<array>` include) and
#886 (obsolete Homebrew tap removal), each in its own commit. No PR or release
artifact is created by this experiment.

The repair uses the exact pkg-config command selected during SDL2 configuration.
The selected `sdl2.pc` identifies compatibility SDL2 with `Name: sdl2_compat`;
classic SDL2 uses `Name: sdl2`. It does not inspect installed package presence or
compare SDL version numbers. SDL3 comes from the selected `sdl3` pkg-config
module's `libdir`. `macos_sdl3_library=/absolute/path/to/library.dylib` explicitly
overrides discovery, including custom builds without pkg-config.

For automatic detection, the selected SDL2 library is checked against the
executable's absolute linked SDL2 path before packaging. Custom installations
using other install-name conventions can use the explicit override. Missing
SDL2 metadata preserves the existing manual-build path; detected compat with
missing SDL3 produces an actionable error.

The existing bundler handles ordinary dependencies first. SDL3 is then copied
beside sdl2-compat as `Contents/libs/libSDL3.dylib` and processed by the same
`dylibbundler` for linked dependencies, relocation, and ad-hoc signing, before
any existing distribution signing/notarization. The helper introduces no
Homebrew traversal, version threshold, custom resolver, staging layer, or cache.
SCons tracks the source SDL3 file and selected metadata as normal dependencies;
its explicit SDL3 output detects deletion. The raw executable supplies content
dependencies; the installed executable is an order-only prerequisite because
bundling rewrites it.

## Reproduce on a disposable macOS 26 ARM runner

```
brew bundle --verbose
python3 -m unittest discover -s contrib/homebrew-packaging -p 'test_*.py'
scons -j4 opengl=1 sdl2=1 macos_add_frameworks=0 macos_bundle_libs=1 build/D{1,2}X-Rebirth.app
python3 contrib/homebrew-packaging/evaluate.py
python3 contrib/homebrew-packaging/incremental.py
```

Evaluation temporarily renames the entire Homebrew prefix using sudo, restoring
it in finally blocks. Use a disposable runner, not a personal workstation.
Evidence includes measurements for original and targeted packaging, actual
SCons output validation, no-op/deleted-output/changed-override rebuild checks,
installed metadata and dynamic-loader logs. Application ZIPs are measured then
deleted. The experiment workflow uploads evidence only, not applications.

Startup checks run `-help` with `DYLD_PRINT_LIBRARIES=1` and cleared SDL/DYLD
overrides. They establish startup and library loading, not gameplay, audio,
rendering, input, Gatekeeper, Developer ID signing, or notarization. Ad-hoc
signatures are verified with `codesign --verify --deep --strict`.

## Frozen generic comparison

`generic_reference.py` preserves the 101-line generic prototype at commit
94d63894e (unchanged production helper on the separate
`prototype/homebrew-runtime-packaging` branch). It exists here only for
comparison, not as an available production packaging mode. It recursively
combines absolute Mach-O paths and installed Homebrew runtime metadata,
selecting top-level dylibs and aliases from reached packages. It rejects
unsupported @rpath traversal rather than adding a resolver. The actual macOS
run reached `brotli/1.2.1/lib/libbrotlienc.1.2.1.dylib`, which depends on
`@rpath/libbrotlicommon.1.dylib`, and stopped. That limitation is retained.

The comparison harness records the generic error and elapsed time until
failure; no finished generic package size or successful generic startup is
claimed. Homebrew metadata can preserve the missing SDL3 edge without describing
which libraries, plugins, helper programs or data should be distributed or where
applications search for them. Generalizing those policies is outside this repair.
