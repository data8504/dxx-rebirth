# Building DXX-Rebirth from source

D1X-Rebirth and D2X-Rebirth (generically DXX-Rebirth) are built by an SConstruct script.  DXX-Rebirth runs on Microsoft Windows, Linux (x86, amd64, and arm64), recent Mac OS X, and Raspberry Pi.  Other targets may also work, but are not tracked by the core team.  If you maintain a working target not listed here, please file an issue to have it included in this list.

The DXX-Rebirth maintainers have no control over the sites linked below.  The maintainers are not responsible for the safety or correct operation of the prerequisites listed below.  Unless specified otherwise, the maintainers of DXX-Rebirth have not verified that the links are current, safe, or produce a working environment.

## Prerequisites

* [Python 3.x](https://www.python.org/) to run [scons](https://www.scons.org/), the processor for SConstruct scripts.
[Python 3.13](https://www.python.org/downloads/release/python-31313/) is recommended.
* C++ compiler with support for selected C++23 features.  One of:
    * [gcc](https://gcc.gnu.org/) 14, 15, or 16
    * [clang](https://clang.llvm.org/) 21.0 or later
    * Microsoft Visual Studio is **not** supported at this time.
	  Visual Studio 2022 release notes indicate it has sufficient C++ support
	  that it should be able to compile Rebirth.  However, due to limitations
	  of the Visual Studio installation environment, the core team does not
	  use, test, or support Visual Studio.
* [SDL 3.2 or later](https://www.libsdl.org/).
* [PhysicsFS](https://icculus.org/physfs/).
PhysFS 3.x or later is required.

SDL3 is the sole supported SDL version on all platforms. There is no SDL
version-selection build option; `sdl2=...` is an unknown option.

Optional, but recommended:

* [SDL\_image 3.2 or later](https://www.libsdl.org/projects/SDL_image/).
* [SDL\_mixer 3.2 or later](https://www.libsdl.org/projects/SDL_mixer/).
* [libpng](http://www.libpng.org/).

Unless otherwise noted, using the newest release available is recommended.  For example, prefer gcc-16 to gcc-15, even though both should work.

DXX-Rebirth can be built on one system to run on a different system, such as using Linux to build for Windows (a "cross-compiled build").  The sections below specify where to get prerequisites for a build meant to run on the system where it is built (a "native build").

For each prerequisite, its development headers (files ending in **.h**) and its libraries (ending in **.so** for Linux, **.dylib** for Mac OS X, and **.dll** for Windows) must be found by the compiler.  You can do this by placing these files in an existing directory which the compiler will search, or by placing these files in a directory which you instruct the compiler to search.

In general, avoid installing prerequisites to paths which contain embedded spaces.  Although Rebirth quotes paths to handle this, some prerequisites may use `pkg-config` files that do not handle embedded spaces well.

### Placing files in a directory which you instruct the compiler to search (preferred)

If you choose to install the headers and/or libraries in a new path, you must instruct the build system to search that path.  To do this for development headers, add to the scons command line **"CPPFLAGS=-isystem** _/absolute/path/to/header/directory_**"**.  To do this for libraries, add to the scons command line **"LINKFLAGS=-L** _/absolute/path/to/library/directory_**"**.

### Adding files to an existing directory which the compiler will search

To find these paths for gcc:
* Get the compiler's header search path by running **gcc -Wp,-v -S -x c++ /dev/null** and looking at the lines under the label **#include <...> search starts here:**.  Any of these lines will work.  The paths _without_ a compiler version number in them are preferable.  Windows users may need to write **gcc -Wp,-v -S -x c++ nul** instead, due to Windows using a different name for the null device.
* Get the compiler's library search path by running **gcc -print-search-dirs** and looking at the line labeled **libraries:**.  This line is a list of directories that will be searched.  Any of the directories will work.  Again, prefer directories that are do not have a compile version number in them.

### Windows
Where possible, Windows users should try to obtain a compiled package, rather than locally compiling from source.  To build from source, read on.

If you are not sure whether your system is Windows x86 or Windows x64, use the packages for Windows x86.  Systems running Windows x64 support running Windows x86 programs, but Windows x86 systems do not run Windows x64 programs.

* [Python x86 installer](https://www.python.org/ftp/python/3.13.13/python-3.13.13.exe) |
[Python x64 installer](https://www.python.org/ftp/python/3.13.13/python-3.13.13-amd64.exe)
* [SCons](https://github.com/SCons/scons/archive/refs/tags/4.10.1.zip)
* C++ compiler
    * mingw-gcc: [Getting Started](http://www.mingw.org/wiki/Getting_Started) |
	[Direct download](https://sourceforge.net/projects/mingw/files/latest/download)
* [SDL3 development packages](https://github.com/libsdl-org/SDL/releases).
* No published PhysFS package for Windows is known.
You must [build it](https://hg.icculus.org/icculus/physfs/raw-file/bf155bd2127b/INSTALL.txt)
from [source](https://github.com/icculus/physfs/archive/refs/tags/release-3.2.0.zip).
You may be able avoid building from source by copying PhysFS DLLs from a
previous Rebirth release and installing current PhysFS headers.
However, building from source is recommended to ensure a consistent
environment.

### MSYS2/mingw-w64 (Windows alternate method)
* `pacman -S git ${MINGW_PACKAGE_PREFIX}-{gcc,pkgconf,scons,sdl3,sdl3-image,sdl3-mixer,libpng,physfs}`

### Linux
Install the listed prerequisites through your system package manager.
* Arch PKGBUILD files are in `contrib/arch/`
* An RPM spec file is in `contrib/rpm/`
* Gentoo ebuild files are in `contrib/gentoo/`

The Arch and RPM recipes require a local source archive. Each recipe contains
the `git archive` command and archive prefix it expects. The Gentoo live
recipe selects its source through `EGIT_REPO_URI` and `EGIT_BRANCH`; override
both variables together to use another SDL3 source branch.

#### Arch
* **pacman -S
 base-devel
 scons
 sdl3
 sdl3\_image
 sdl3\_mixer
 physfs**

#### Fedora
* **yum install
 gcc-c++
 scons
 SDL3-devel
 SDL3\_image-devel
 SDL3\_mixer-devel
 physfs-devel**

#### Gentoo
* **emerge --ask --verbose --noreplace
 dev-util/scons
 media-libs/libsdl3
 media-libs/sdl3-image
 media-libs/sdl3-mixer
 dev-games/physfs**

#### Ubuntu
* **apt-get install
 build-essential
 scons
 libsdl3-dev
 libsdl3-image-dev
 libphysfs-dev**

Ubuntu releases without SDL3_mixer packages can build it using `contrib/ci/build-sdl3-mixer.sh /absolute/install/prefix /absolute/build/directory`. Install CMake, Ninja, curl, and development packages for the desired music decoders first. Add the installed `lib/pkgconfig` directory to `PKG_CONFIG_PATH` and `lib` to `LD_LIBRARY_PATH` when configuring and running a build. See the Linux workflow for the complete decoder dependency list.

### Mac OS X
Install the listed prerequisites through your preferred package manager.

PhysicsFS must be installed as a separate dynamic library.  It is not supported as an OS X Framework.  The required files will be installed into the correct locations by the PhysicsFS installer; refer to the relevant instructions.

The Mac OS X Command Line Tools are required.  Install them by running

    xcode-select --install

from the Terminal.  This may need to be done after each major OS upgrade as well.

DXX-Rebirth can be built from the Terminal (via SCons) without Xcode; to build using Xcode requires Xcode to be installed.

**Note:** If intending to cross-compile from Linux to macOS, information about this process can be found at [macOS_Cross_Compilation.markdown](macOS_Cross_Compilation.markdown).

#### [Homebrew](https://github.com/Homebrew/homebrew/)
The project includes a Brewfile for installing all required dependencies, if you use Homebrew.  You can install them with:

* **brew bundle**

**Note:** Because Homebrew only installs libraries and not frameworks, when building for Mac OS X with Homebrew-provided dependencies, you must provide **macos_add_frameworks=False** as a SCons command parameter in order for the build system to look for libraries rather than frameworks.

## Building
Once prerequisites are installed, run **scons** *options* to build.  By default, both D1X-Rebirth and D2X-Rebirth are built.  To build only D1X-Rebirth, run **scons d1x=1**.  To build only D2X-Rebirth, run **scons d2x=1**.

If unspecified, **SConstruct** uses $CXX for the compiler, $CPPFLAGS for preprocessor options, $CXXFLAGS for C++ compiler options, and $LDFLAGS for linker options.  You may override any or all of these choices by setting options of the corresponding name: **scons CXX=/opt/my/c++ 'CXXFLAGS=-O0 -ggdb'**.  **SConstruct** supports numerous options for adjusting build details.  Run **scons -h** to see them.  Option names are case sensitive.  Commonly used options include:

* **CXX=**_path_ - path to C++ compiler
* **CPPFLAGS='**_flags_**'** - flags for C preprocessor
* **CXXFLAGS='**_flags_**'** - flags for C++ compiler
* **LDFLAGS='**_flags_**'** - flags for linker
* **lto=1** - enable Link Time Optimization
* **sdlmixer=1** - enable support for SDL\_mixer
* **builddir=**_path_ - set directory for build outputs; defaults to `build/`.
* **builddir\_prefix=**_path_ - Developer option; set builddir to builddir\_prefix plus a path derived from build options.
Use this to build multiple targets without picking specific paths for each one.
The generated path is stable across multiple runs with the same options.
Packaging scripts should use **builddir** with manually chosen directories.
* **verbosebuild=1** - show commands executed instead of short descriptions of the commands
* **prefix=**_path_ - (Linux only); equivalent to **--prefix** in Autoconf
* **sharepath=**_path_ - (Linux only); sets system directory to search for game data

Use separate build directories for different configurations. When **builddir**
is an absolute path, name the executable targets explicitly; the default `.`
target does not descend into directories outside the source tree. For example:

```sh
scons builddir=/absolute/build/path \
    /absolute/build/path/d1x-rebirth/d1x-rebirth \
    /absolute/build/path/d2x-rebirth/d2x-rebirth
```

The build system supports building multiple targets in parallel.  This is primarily useful for developers, but can also be used by packagers to create secondary builds with different features enabled.  To use it, run **scons** *game*=*profile[,profile...]*.  **SConstruct** will search each profile for the known options.  The first match wins.  For example:

        scons dxx=gcc16,e, d2x=gcc15,soft, \
            gcc16_CXX=/path/to/gcc-16 \
            gcc15_CXX=/path/to/gcc-15 \
            e_editor=1 soft_opengl=0

This tells **SConstruct** to build both games (**dxx**) with the profiles **gcc16**, **e**, *empty* and also to build D2X-Rebirth (**d2x**) with the profiles **gcc15**, **soft**, *empty*.  Profiles **gcc16** and **gcc15** define private values for **CXX**, so the default value of **CXX** is ignored.  Profile **e** enables the **editor** option, which builds features used by players who want to create their own levels.  Profile **soft** sets **opengl=0**, which selects the software renderer. Both renderers use SDL3.  Profile *empty* is the default namespace, so CPPFLAGS, CXXFLAGS, etc. are found when it is searched.  Since these values were not assigned, they are drawn from the corresponding environment variables.

The build system supports specifying a group of closely related targets.  This is mostly redundant on shells with brace expansion support, but can be easier to type.  For example:

        scons builddir_prefix=build/ \
			dxx=gcc15+gcc16,prof1,prof2,prof3,

This is equivalent to the shell brace expansion:

        scons builddir_prefix=build/ \
			dxx={gcc15,gcc16},prof1,prof2,prof3,

or

        scons builddir_prefix=build/ \
			dxx=gcc15,prof1,prof2,prof3, \
			dxx=gcc16,prof1,prof2,prof3,

Profile addition can be stacked: **scons dxx=a+b,c+d,e+f** is equivalent to **scons dxx=a,c,e dxx=a,d,e dxx=b,c,e dxx=b,d,e dxx=a,c,f dxx=a,d,f dxx=b,c,f dxx=b,d,f**.

### Configuration
**SConstruct** runs tests to check for common problems and available functionality.  **SConstruct** will automatically enable available compiler features.  **SConstruct** will not automatically enable optional features which require an external library.  If the feature is enabled, either by default or by the provided options, and the library is present, it will be used.  If the feature is enabled, and the library is absent, the build fails.  If the feature is disabled, either by default or by the provided options, the library check is skipped and the feature is not used.

If required functionality is missing, **SConstruct** will stop the build and print a diagnostic message.  A stop at this stage indicates an environment problem.  If any target fails to configure, no target will be built.

Before reporting an issue, clear any applicable caches (**ccache -C**; **rm .sconsign.dblite**) and reproduce the failure.  The DXX-Rebirth maintainers may be able to help you resolve environment problems if you cannot solve them on your own.  If you need help, please post the full output of running **scons** and the contents of **config.log** from your build directory.

### Compiling
After **SConstruct** finishes the configure tests, **scons** will compile and link the program.  Failures at this stage may indicate a bug that should be reported.  Broken environments are usually caught by **SConstruct** checks before the build began.  If the compilation succeeds, the output files will be found in game-specific directories.  If the profile specified editor features, then **-editor** is appended to the filename.

The output path can be overridden with the **SConstruct** option **program\_name**.  If **program\_name** is set, it overrides the game directory prefix and the optional **-editor** modification.  It does not override the output suffix (**.exe** for Windows, *empty* for Linux).

Windows output with **program\_name** unset:

* *build-directory*/d1x-rebirth/d1x-rebirth*[-editor]*.exe
* *build-directory*/d2x-rebirth/d2x-rebirth*[-editor]*.exe

Windows output with **d1x=1 program\_name**=**d1x-local**:

* *build-directory*/d1x-local.exe

Linux output with **program\_name** unset:

* *build-directory*/d1x-rebirth/d1x-rebirth*[-editor]*
* *build-directory*/d2x-rebirth/d2x-rebirth*[-editor]*

#### Compiling with MSYS2
MSYS2 offers its users three terminal environments: msys2, for building with POSIX compatibility (linking to runtime `/usr/bin/msys-2.0.dll`); and mingw32 and mingw64, for building portable native Windows apps (linking to `C:\WINDOWS\System32\msvcrt.dll`), on i686 and x86_64 respectively.
* Install [MSYS2](https://www.msys2.org), following the directions on the main page.
* In either a mingw32 or mingw64 (not msys2) terminal:

      pacman -Syuu  # update MSYS2, as needed
      pacman -S --needed git ${MINGW_PACKAGE_PREFIX}-{gcc,pkgconf,scons,sdl3,sdl3-image,sdl3-mixer,libpng,physfs}
      git clone https://github.com/dxx-rebirth/dxx-rebirth.git
      cd dxx-rebirth
      scons
        # Or (for example) to build d1x only, with SDL3, with lower process priority, on all cores:
      time nice scons -j$(nproc) d1x=1

* A locally built executable will run anywhere if it's invoked from inside the appropriate mingw32 or mingw64 terminal. To run it instead directly in Windows, either:
    1. The linked mingw-w64 libraries will need to be added to PATH (See [contrib/msys2](contrib/msys2) for working example batch files); or
    2. Dependency DLLs from MSYS2 will need to be copied to the same directory as the executable.

### Installing the game engine
For Windows and Linux, DXX-Rebirth installs only the main game binary.  The binary can be run from anywhere and can be installed by copying the game binary.  The game does not inspect the name of its binary.  You may rename the output after compilation without affecting the game.

As a convenience, if **register\_install\_target=True**, **SConstruct** registers a pseudo-target named **install** which copies the compiled files to *BINDIR*, as modified by the SCons option **--install-sandbox**.  By default, **register\_install\_target=True**, the sandbox prefix path is empty, and *BINDIR* is *PREFIX*__/bin__, which expands to **/usr/local/bin**.

## Testing

Install Boost.Test headers and the `boost_unit_test_framework` library, then
enable and run the native regression suites:

```sh
scons register_runtime_test_link_targets=1 check
```

The suites cover engine logic, virtual-device input and saved binding indices,
text and modifier handling, fractional mouse motion, PhysicsFS IO streams,
indexed surfaces and SDL3 audio stream/mixer lifetimes. The SDL input, video
and audio tests use dummy drivers and do not require game data or a display
server. The input suite requires joystick support (`max_joysticks` greater
than zero). Mixer-specific audio cases require `sdlmixer=1`.

Compile each header independently with:

```sh
scons check_header_includes=1 \
    build/common/check_header_includes \
    build/d1x-rebirth/check_header_includes \
    build/d2x-rebirth/check_header_includes
```

Adjust the header target paths when using a different **builddir**. The `check`
target executes native test programs; it does not run cross-compiled targets.

## Runtime libraries

Both the OpenGL and software renderers use SDL3. SDL3_image supplies PCX
decoding; with `sdlimage=0`, PCX artwork uses a blank fallback. `sdlmixer=0`
selects builtin audio. The `emulate_sdl1` resampler is an internal audio
algorithm, not an SDL1 dependency.

SDL3_mixer MIDI playback requires instrument configuration. Timidity uses
`TIMIDITY_CFG` or its system configuration; FluidSynth requires a soundfont.
Native Windows HMP and CD playback use separate platform backends.
`adlmidi=runtime` enables the dynamically loaded ADLMIDI path; playback
requires its library to be available at runtime.

Packages must include decoder libraries loaded dynamically as well as linked
SDL libraries. The Windows packaging script collects mixer codec DLLs and
their dependencies. For linker-based AppImage or macOS bundle collection,
build SDL3_mixer with `SDLMIXER_DEPS_SHARED=OFF` so its decoder dependencies
appear as library imports. The Linux CI mixer helper uses this configuration.
