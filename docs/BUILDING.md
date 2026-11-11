# Building Starship

## Co-op fork notes (added by Starship Co-op; the rest of this page is upstream's)

The fork wraps the steps below in one script, written to work the same on Windows, Linux and macOS (macOS not tried here):

```sh
git clone --recursive https://github.com/Clodmony/starship-coop.git && cd starship-coop
python tools/coop/build.py --assets --quiet        # needs your ROM as baserom.z64 in the repository folder
```

`build.py` fetches the submodules if they are missing, configures (Visual Studio 2022 with `-T v143 -A x64` on Windows,
Ninja elsewhere), builds, and runs the repository checks (`tools/coop/check.py`). `--assets` also builds `sf64.o2r`
from your ROM and `starship.o2r` from `port/`. Without `--quiet` it prints everything; with it, a summary line such as
`build: OK (Linux, Debug, 2m23s)`. Run `python tools/coop/build.py --help` for the rest (`--config Release`,
`--build-dir`, `--clean`, `--jobs`, `--ninja`).

### Minimum versions the build asks for

| What | Minimum | Source |
|---|---|---|
| CMake | 3.24 | `libultraship/CMakeLists.txt:1` (the root file asks 3.16, `CMakeLists.txt:1`; Torch 3.12) |
| Language standards | C11, C++20 | `CMakeLists.txt:31-32` |
| Windows | Visual Studio 2022 with the C++ desktop workload and the `MSVC v143` toolset, a Windows SDK; 8 GB of RAM | `tools/coop/build.py` (`-G "Visual Studio 17 2022" -T v143 -A x64`), the Windows section below |
| Linux | GCC or Clang, Ninja, Python 3, the packages of the Linux section below | `tools/coop/build.py`, `.github/workflows/coop-build.yml` |
| macOS | Xcode, Ninja, Python 3, the packages of the macOS section below plus SDL2_net | `.github/macports.yml`, and see "macOS" below: **not tried here** |

**Unknown:** the lowest GCC, Clang, MSVC, Ninja and Python versions that work. Nothing in the CMake files or in
`build.py` checks one. CI uses the images' own tools (`windows-2022`, `ubuntu-24.04`), so those versions are known to
work and older ones are not ruled out.

### Gotchas, each with where it was found

1. **Submodules.** `libultraship` and `tools/Torch` are submodules (`.gitmodules`). A plain `git clone` leaves both
   folders empty. `build.py` runs `git submodule update --init --recursive` when either `CMakeLists.txt` is missing
   (`tools/coop/build.py`, `submodules_ready`); doing it yourself about 53 MB come down (issue #2, hand-off of
   2026-11-08).
2. **`baserom.z64` must be in z64 byte order.** `--assets` stops at once without `baserom.z64` in the repository
   folder (`build.py` prints `needs your ROM as baserom.z64`), but it does not check the byte order. A dump named
   `.n64` can be in v64 order (16-bit swapped, first bytes `37 80 40 12`); its SHA-1 then matches no entry of
   `config.yml` (the build's own check does not look at byte order). The right file starts `80 37 12 40`, and for US 1.0 its SHA-1 is
   `d8b1088520f7c5f81433292a9258c1184afa1457` (`config.yml:34`). Swap a v64 dump on any OS with
   `python -c "d=open('in.n64','rb').read(); open('baserom.z64','wb').write(b''.join(d[i:i+2][::-1] for i in range(0,len(d),2)))"`
   (Linux: `dd conv=swab`). A 4-byte-swapped dump needs groups of 4. Source: NOTES.md, 2026-11-04, #1, Windows and
   Linux builds; the one-liner was run on a v64 dump and gave the US 1.0 hash.
3. **Two generated archives, and the game does not start without both.** `sf64.o2r` (target `ExtractAssets`, needs
   the ROM) and `starship.o2r` (target `GeneratePortO2R`, shaders and HUD textures from `port/`). With only
   `sf64.o2r` the game logs `Failed to load default fragment shader` and then looks like a hang (NOTES.md, 2026-11-06,
   #26). Never commit them or the ROM; git ignores them.
4. **`Failed to process ast_audio/audio_setup` in the build log is harmless.** Torch logs every empty result as failed
   (`tools/Torch/src/Companion.cpp:319-320`); the audio tables are in the game (NOTES.md, 2026-11-04, #1, Linux).
5. **`--jobs N`** passes `--parallel N` to CMake (`build.py`); without it the build uses every core. Use it on a PC you
   are also working on: upstream notes that 4 GB machines have seen compiler failures (Windows section below).
6. **One build folder holds one config on Linux and macOS.** `build.py` refuses a Debug folder for `--config Release`
   (`build: FAILED, <dir> holds a Debug build`): give it `--build-dir build-release` or `--clean`. Visual Studio keeps
   `build/x64/Debug` and `build/x64/Release` side by side (NOTES.md, 2026-11-08, #145). The issue that found this said
   libultraship archives landed in the source tree, so a second config linked the wrong ones (#145); the script's
   help now says a second folder shares nothing, and **I did not re-check that here**.
7. **A Debug and a Release build of one commit pass the handshake as the same build.** Both have the same build id
   and the handshake cannot tell them apart (issue #2, hand-off of the decision 0002 proposal). On Windows the two
   gave equal per-frame state hashes in Corneria (NOTES.md, 2026-11-08, #145, PR #176). Two builds of one commit with
   different compiler versions count as different platforms (NOTES.md, 2026-11-07, #2) and warn, per decision 0010.
   For a package to send to a friend use `python tools/coop/package.py --config Release` after a Release build.
8. **A second checkout inside the repository folder (Windows).** The source list is a recursive glob and its bare
   `Resource.rc` pattern matches in any subfolder, so a worktree under `local/` gave the link a second `VERSION`
   resource: `CVT1100: duplicate resource` and `LNK1123` (issue #197). The fix, a filter for `^local/`, is PR #198
   (`CMakeLists.txt:174-176` on the test branch, not yet on `coop`). Until it is, keep a second checkout outside the
   first one.
9. **`gamecontrollerdb.txt` is read from the working directory.** The build copies it next to the executable; a test
   folder needs a copy, or the log says `Failed add SDL game controller mappings` (NOTES.md, 2026-11-04, #1, Windows).
10. **Where a game keeps its config and saves.** On Windows the working directory (`libultraship/src/Context.cpp:458-500`);
    on Linux `SHIP_HOME` if set, else the working directory (`:481-486`, `:500`). So a test instance is a folder with
    the two `.o2r` files and `gamecontrollerdb.txt` and `SHIP_HOME` pointing at it.
11. **Linux dependencies differ between the lists.** This page's Debian/Ubuntu list has no `libglew-dev`, the CI job
    installs it (`.github/workflows/coop-build.yml`). **Unknown** whether a build needs it. The co-op transport needs
    `libsdl2-net-dev` (Debian/Ubuntu) or `sdl2_net` (Arch); the macOS list below lacks SDL2_net, and
    `.github/macports.yml` adds it with a `COOP:` comment.
12. **The Linux CI package needs Ubuntu 24.04's libraries** (`libzip.so.4`, `libtinyxml2.so.10`, `libspdlog.so.1.12`,
    `libfmt.so.9`) and does not start on a newer distribution (NOTES.md, 2026-11-06, #26; issue #121). A package made
    on your own PC has no such limit.
13. **With a controller connected, the title screen comes after the attract sequence**, about 100 s after boot
    (`src/overlays/ovl_menu/fox_title.c:204-211`, issue #1 hand-off of the Linux build). A boot test should wait for it.
14. **Windows against Linux is a platform difference.** It warns and is allowed (decision 0010); the one full campaign
    between the two ran without a desync on the test build (issue #2, 2026-11-10). Nobody has compared them on a
    plain `coop` build.

### How the build was checked

| OS | Result | Source |
|---|---|---|
| Windows 11, VS 2022 (v143), Debug | built, booted to the title sequence on DirectX 11 | NOTES.md, 2026-11-04, #1, Windows build |
| Windows, Release | built; `build/x64/Release/Starship.exe` ran the hash checks | NOTES.md, 2026-11-08, #145 |
| Linux, GCC, Debug | built, reached the title screen on OpenGL | NOTES.md, 2026-11-04, #1, Linux build |
| Linux, GCC, Release | built and booted headless | issue #2, hand-off of 2026-11-08 |
| macOS | **not tried here** (nobody on this project has a Mac) | see below |

### macOS (not tried here)

Only upstream's notes are known. The macOS section below lists the packages, asks for Xcode and Ninja, and builds
with the same four commands; upstream's workflow builds a universal binary (`-DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"`)
on a `macos-14` runner with MacPorts (`.github/workflows/mac.yml`, `.github/macports.yml`). That runner image was to be
retired on 2026-11-02, so upstream's macOS job needed a fix (`docs/coop/MAINTAINING.md`, "Upstream's workflows"). The
co-op transport adds SDL2_net. **Open (issue #1):** nobody has built this fork on macOS or booted it to the title
screen.

---

_Upstream's text starts here._

## Windows

Requires:
  * At least 8GB of RAM (machines with 4GB have seen complier failures)
  * Visual Studio 2022 Community Edition with the C++ feature set
  * One of the Windows SDKs that comes with Visual Studio, for example the current Windows 10 version 10.0.19041.0
  * The `MSVC v143 - VS 2022 C++ build tools` component of Visual Studio
  * Python 3 (can be installed manually or as part of Visual Studio)
  * Git (can be installed manually or as part of Visual Studio)
  * Cmake (can be installed via chocolatey or manually)

During installation, check the "Desktop development with C++" feature set:

![image](https://user-images.githubusercontent.com/30329717/183511274-d11aceea-7900-46ec-acb6-3f2cc110021a.png)
Doing so should also check one of the Windows SDKs by default.  Then, in the installation details in the right-hand column, make sure you also check the v143 toolset. This is often done by default.

It is recommended that you install Python and Git standalone, the install process in VS Installer has given some issues in the past.

1. Clone the Starship repository

_Note: Be sure to either clone with the ``--recursive`` flag or do ``git submodule update --init`` after cloning to pull in the libultraship submodule!_

2. After setup and initial build, use the built-in OTR extraction to make your sf64.o2r file.

_Note: Instructions assume using powershell_
```powershell
# Navigate to the starship repo within powershell. ie: cd "C:\yourpath\starship"
cd starship

# Setup cmake project
# Add `-DCMAKE_BUILD_TYPE:STRING=Release` if you're packaging
& 'C:\Program Files\CMake\bin\cmake' -S . -B "build/x64" -G "Visual Studio 17 2022" -T v143 -A x64

# Generate sf64.o2r
& 'C:\Program Files\CMake\bin\cmake.exe' --build .\build\x64 --target ExtractAssets

# Generate starship.o2r
& 'C:\Program Files\CMake\bin\cmake.exe' --build .\build\x64 --target GeneratePortO2R

# Compile project
# Add `--config Release` if you're packaging
& 'C:\Program Files\CMake\bin\cmake.exe' --build .\build\x64

# Now you can run the executable in .\build\x64 or run in Visual Studio
```

### Developing Starship
With the cmake build system you have two options for working on the project:

#### Visual Studio
To develop using Visual Studio you only need to use cmake to generate the solution file:
```powershell
# Generates starship.sln at `build/x64` for Visual Studio 2022
& 'C:\Program Files\CMake\bin\cmake' -S . -B "build/x64" -G "Visual Studio 17 2022" -T v143 -A x64
```

#### Visual Studio Code or another editor
To develop using Visual Studio Code or another editor you only need to open the repository in it.
To build you'll need to follow the instructions from the building section.

_Note: If you're using Visual Studio Code, the [cpack plugin](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools) makes it very easy to just press run and debug._

_Experimental: You can also use another build system entirely rather than MSVC like [Ninja](https://ninja-build.org/) for possibly better performance._


### Generating the distributable
After compiling the project you can generate the distributable by running:
```powershell
# Go to build folder
cd "build/x64"
# Generate
& 'C:\Program Files\CMake\bin\cpack.exe' -G ZIP
```

### Additional CMake Targets
#### Clean
```powershell
# If you need to clean the project you can run
C:\Program Files\CMake\bin\cmake.exe --build build-cmake --target clean
```

## Linux
### Install dependencies
#### Debian/Ubuntu
```sh
# using gcc
apt-get install gcc g++ git cmake ninja-build lsb-release libsdl2-dev libpng-dev libsdl2-net-dev libzip-dev zipcmp zipmerge ziptool nlohmann-json3-dev libtinyxml2-dev libspdlog-dev libboost-dev libopengl-dev libogg-dev libvorbis-dev

# or using clang
apt-get install clang git cmake ninja-build lsb-release libsdl2-dev libpng-dev libsdl2-net-dev libzip-dev zipcmp zipmerge ziptool nlohmann-json3-dev libtinyxml2-dev libspdlog-dev libboost-dev libopengl-dev libogg-dev libvorbis-dev
```
#### Arch
```sh
# using gcc
pacman -S gcc git cmake ninja lsb-release sdl2 libpng libzip nlohmann-json tinyxml2 spdlog sdl2_net boost libogg libvorbis

# or using clang
pacman -S clang git cmake ninja lsb-release sdl2 libpng libzip nlohmann-json tinyxml2 spdlog sdl2_net boost libogg libvorbis
```
#### Fedora
```sh
# using gcc
dnf install gcc gcc-c++ git cmake ninja-build lsb_release SDL2-devel libpng-devel libzip-devel libzip-tools nlohmann-json-devel tinyxml2-devel spdlog-devel boost-devel libogg-devel libvorbis-devel

# or using clang
dnf install clang git cmake ninja-build lsb_release SDL2-devel libpng-devel libzip-devel libzip-tools nlohmann-json-devel tinyxml2-devel spdlog-devel boost-devel libogg-devel libvorbis-devel
```
#### openSUSE
```sh
# using gcc
zypper in gcc gcc-c++ git cmake ninja SDL2-devel libpng16-devel libzip-devel libzip-tools nlohmann_json-devel tinyxml2-devel spdlog-devel libogg-devel libvorbis-devel

# or using clang
zypper in clang libstdc++-devel git cmake ninja SDL2-devel libpng16-devel libzip-devel libzip-tools nlohmann_json-devel tinyxml2-devel spdlog-devel libogg-devel libvorbis-devel
```

### Build

_Note: If you're using Visual Studio Code, the [CMake Tools plugin](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools) makes it very easy to just press run and debug._

```bash
# Clone the repo and enter the directory
git clone https://github.com/HarbourMasters/starship.git
cd starship

# Clone the submodules
git submodule update --init

# Generate Ninja project
# Add `-DCMAKE_BUILD_TYPE:STRING=Release` if you're packaging
# Add `-DPython3_EXECUTABLE=$(which python3)` if you are using non-standard Python installations such as PyEnv
cmake -H. -Bbuild-cmake -GNinja

# Generate sf64.o2r
cmake --build build-cmake --target ExtractAssets

# Generate starship.o2r
cmake --build build-cmake --target GeneratePortO2R

# Compile the project
# Add `--config Release` if you're packaging
cmake --build build-cmake

# Now you can run the executable in ./build-cmake/mm/2s2h.elf
# To develop the project open the repository in VSCode (or your preferred editor)
```

### Generate a distributable
After compiling the project you can generate a distributable by running of the following:
```bash
# Go to build folder
cd build-cmake
# Generate
cpack -G DEB
cpack -G ZIP
cpack -G External (creates appimage)
```

### Additional CMake Targets
#### Clean
```bash
# If you need to clean the project you can run
cmake --build build-cmake --target clean
```

## macOS
Requires Xcode (or xcode-tools) && `sdl2, libpng, glew, ninja, cmake, nlohmann-json, tinyxml2, libzip, vorbis-tools` (can be installed via homebrew, macports, etc)

**Important: For maximum performance make sure you have ninja build tools installed!**

_Note: If you're using Visual Studio Code, the [cpack plugin](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools) makes it very easy to just press run and debug._

```bash
# Clone the repo
git clone https://github.com/HarbourMasters/starship.git
cd starship
# Clone the submodule libultraship
git submodule update --init

# Generate Ninja project
# Add `-DCMAKE_BUILD_TYPE:STRING=Release` if you're packaging
cmake -H. -Bbuild-cmake -GNinja

# Generate sf64.o2r
cmake --build build-cmake --target ExtractAssets

# Generate starship.o2r
cmake --build build-cmake --target GeneratePortO2R

# Compile the project
# Add `--config Release` if you're packaging
cmake --build build-cmake

# Now you can run the executable file:
./build-cmake/Starship
# To develop the project open the repository in VSCode (or your preferred editor)
```

### Generating a distributable
After compiling the project you can generate a distributable by running of the following:
```bash
# Go to build folder
cd build-cmake
# Generate
cpack
```

### Additional CMake Targets
#### Clean
```bash
# If you need to clean the project you can run
cmake --build build-cmake --target clean
```

# Compatible Roms
See [`supportedHashes.json`](supportedHashes.json)

## Getting CI to work on your fork

The CI works via [Github Actions](https://github.com/features/actions) where we mostly make use of machines hosted by Github; except for the very first step of the CI process called "Extract assets". This steps extracts assets from the game file and generates an "assets" folder in `mm/`.

To get this step working on your fork, you'll need to add a machine to your own repository as a self-hosted runner via "Settings > Actions > Runners" in your repository settings. Make sure to add the 'asset-builder' tag to your newly added runner to assign it to run this step. To setup your runner as a service read the docs [here](https://docs.github.com/en/actions/hosting-your-own-runners/configuring-the-self-hosted-runner-application-as-a-service?platform=linux).

### Runner on Windows
You'll have to enable the ability to run unsigned scripts through PowerShell. To do this, open Powershell as administrator and run `set-executionpolicy remotesigned`. Most dependencies get installed as part of the CI process. You will also need to separately install 7z and add it to the PATH so `7z` can be run as a command. [Chocolatey](https://chocolatey.org/) or other package managers can be used to install it easily.

### Runner on UNIX systems
If you're on macOS or Linux take a look at `macports-deps.txt` or `apt-deps.txt` to see the dependencies expected to be on your machine.
