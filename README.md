# Docklight

Docklight is the Nintendo Switch port of
[Lighthouse](https://github.com/HarbourMasters/Lighthouse), which itself is Harbour Masters' PC port of Banjo-Kazooie.
All data compatible with Lighthouse should be kept compatible with Docklight.
Docklight is currently based on Lighthouse version Hatteras Alfa 1.1.0

Docklight does not include copyrighted game assets. You must provide a legally obtained dump of a supported retail
copy of Banjo-Kazooie and generate `bk.o2r` from it on a PC.

## Nintendo Switch

### Generate `bk.o2r` on a PC

The Switch version cannot extract assets from a ROM. Please generate `bk.o2r` with the desktop build on verison 1.0.1:

The supported ROMs list is identical to upstream.

### Option 1:

1. Run the matching Windows, Linux, or macOS Docklight build.
2. Select your supported ROM when prompted and let extraction finish.
3. Find the generated `bk.o2r` beside the desktop application.

### Option 2:

Copy `baserom.z64` to the root of the Docklight git directory:

```bash
git clone --recursive https://github.com/PalindromicBreadLoaf/Docklight.git
cd Docklight
cmake -H. -Bbuild-cmake -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --target ExtractAssets
```

This writes `bk.o2r` to the repository root and copies it into `build-cmake/`. Always regenerate it with a compatible
Docklight release if the application reports that the archive version is outdated.

### Installation

Extract the Docklight release and copy files to your SD card like so:

```text
sdmc:/switch/Lighthouse/
├── Lighthouse.nro
├── bk.o2r
├── lighthouse.o2r
└── gamecontrollerdb.txt
```

`Lighthouse.nro`, `gamecontrollerdb.txt`, and `lighthouse.o2r` come from the Docklight release. Add the `bk.o2r` you generated from your own
ROM.

### Mods, romhacks, and language packs

Docklight creates this hierarchy on first boot:

```text
sdmc:/switch/Lighthouse/mods/
├── custom_mod_files_go_here.txt
├── ~romhacks/
├── ~lang/
└── ~shared/
```

- Put ordinary prebuilt `.o2r`/`.otr` mods under `mods/`.
- Put prebuilt romhacks under `mods/~romhacks/`.
- Put prebuilt language pack `.o2r` files under `mods/~lang/`.
- Put assets shared between the base game and romhacks under `mods/~shared/`.
- Loose mod directories are also discovered recursively under `mods/`.

ROM, romhack, and language pack extraction is desktop only. Generate those archives with a matching PC build and copy
the resulting files to the appropriate directory on the SD card.

When reporting a crash, please include `Lighthouse.log`, the Docklight version, what you were doing up to the crash, and
the corresponding Atmosphère report from `atmosphere/crash_reports/`, if one was created. Open reports on the
[Docklight issue tracker](https://github.com/PalindromicBreadLoaf/Docklight/issues).

Mods require enabling in the in-game menu (which can be accessed through either the `-` button or `R`+`L`+Dpad-Up).
Sometimes it is necessary to disable and re-enable mods to get them to work properly.

## Building Docklight

Install devkitPro with devkitA64, libnx, and the Switch portlibs for SDL2, OpenGL/EGL, PNG, zlib, bzip2, FreeType,
Ogg/Vorbis, and SDL2_net. Initialize both submodules, then configure with devkitPro's toolchain:

```bash
git clone --recursive https://github.com/PalindromicBreadLoaf/Docklight.git
cd Docklight
cmake -H. -Bbuild-switch -GNinja \
  -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake
cmake --build build-switch --config Release -j$(nproc)
```

The result is `build-switch/Lighthouse.nro`.
You then will need to generate both o2r archives:

```bash
cmake -H. -Bbuild-cmake -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --target ExtractAssets
cmake --build build-cmake --target GeneratePortO2R
cmake --build build-cmake -j
```

A special thanks to Harbour Masters and all Lighthouse contributors for making this port possible in the first place.
