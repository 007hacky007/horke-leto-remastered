# Developer notes

## Layout

- `src/`: the engine (AGDS script interpreter, renderer with GPU and software
  paths, CD audio, saves).
- `builder/`: the native builder. One statically linked executable (SDL3, stb,
  nlohmann/json, embedded DejaVu fonts) with its own HKDF and
  XChaCha20-Poly1305. `builder/pins.json` pins the supported disc, its game files,
  the upscaler and the art release.
- `tools/`: the asset importer and exporter (`hl-tool`) and the distribution
  scripts in `tools/distribution/`.
- `packaging/`: the game launcher and the macOS disk image layout.
- `tests/`: engine unit tests. `builder/tests.cpp` covers the builder's crypto.

## Building

You need CMake 3.25 or newer, Ninja, Python 3 and a C++20 compiler (Xcode command
line tools on a Mac). Dependencies are fetched explicitly with pinned SHA-256
values from `dependencies.lock.json` and `builder/pins.json`; CMake never
downloads anything.

macOS (Apple silicon):

```sh
python3 tools/distribution/fetch.py --platform macos
cmake --preset macos-release
cmake --build --preset macos-release
ctest --test-dir build/macos-release --output-on-failure
python3 -m pip install dmgbuild==1.6.7
python3 tools/distribution/package.py --platform macos --bundle --version 1.0.0
```

Windows x64 is cross-compiled with the pinned llvm-mingw, on Linux or a Mac:

```sh
python3 tools/distribution/fetch.py --platform windows --toolchain
cmake --preset windows-release
cmake --build --preset windows-release
python3 tools/distribution/package.py --platform windows --bundle --version 1.0.0
```

The results are `dist/Horke-Leto-Builder-macOS.dmg` and
`dist/Horke-Leto-Builder-Windows.exe`. `package.py` stages the payload (engine,
game launcher, `hl-tool`, the Real-ESRGAN runner and model, FLAC, license notices),
checks that it contains no game data, and packs it into a deterministic ZIP. On a
Mac the ZIP goes into `Contents/Resources/payload.zip` of the ad hoc signed app.
On Windows it is appended to the executable, followed by its size (8 bytes,
little endian) and the marker `HLPAYLD1`. `--self-check` verifies the embedded
payload and exits.

## Builder command line

```sh
"Horke Leto Builder" --cli --image /path/to/game.cue --output /path/to/folder
```

`--drive E:` reads a CD drive on Windows, `--art-archive file.zip` uses a
downloaded art archive (its hash must still match the pin), and
`--keep-temporary` keeps the `.builder` work folder.

For screenshots, `HB_DEMO="page stage"` with `HB_DEMO_WORK` pointing at a kept
`.builder` folder shows a page with real frames, and `HB_SCREENSHOT=file.bmp`
(with `HB_SCREENSHOT_MS`) saves one frame and quits.

## How a build works

1. The disc is recognised by the ISO9660 volume and the hash of `HL.EXE`. The
   track layout is read from the image itself, so images with or without stored
   gaps both work. Every game file is checked against `builder/pins.json` after
   import.
2. `hl-tool` imports the game, extracts the CD audio to FLAC and exports all 595
   FLC resources (4,903 frames).
3. The art archive is downloaded and checked. Each redraw is decrypted with a key
   derived from the matching original picture in `GFX.GRP`.
4. Original: every frame is upscaled 3x with Real-ESRGAN AnimeVideo v3 (ncnn and
   Vulkan; MoltenVK on a Mac) and packed into atlases.
5. Live-palette variants: 59 pictures that the game draws under another palette
   (the menu cursor, inventory icons under a scene palette, night scenes) are
   recoloured and upscaled again into `packs/<mode>/variants`. The list is
   `palette_variants` in `builder/pins.json`; the engine looks them up as
   `RESOURCE@palette` and refuses to draw a frame under a palette it was not
   built for.
6. Remastered: redraws replace their static scenes, animation frames get a fixed
   colour grade, and known backgrounds are swapped for their redraws. Animation
   variants get the same grade; static redraws fall back to the Original variant.
7. The game is assembled with both packs and its icon is drawn from the player's
   own game files.

Every stage is checkpointed with file hashes, so an interrupted build resumes.

## Releases

Pushing a tag `v*` runs `.github/workflows/release.yml`: it builds and tests both
platforms with `ci.yml` (macOS on `macos-15`, Windows cross-built on Ubuntu and
tested on `windows-latest`) and publishes the `.dmg`, the `.exe`, their SHA-256
files and the corresponding source archive.

A new art release in
[horke-leto-remastered-art](https://github.com/007hacky007/horke-leto-remastered-art)
needs a new builder release with the updated `art` pin.

## Verification status

The engine was compared with the original DOS game running in a patched DOSBox-X,
scene by scene, and the native Mac build was played to the ending. Windows chapter
checks pass on Windows 11. The DMG background is rendered from
`packaging/macos/dmg/background.html` with `render_background.sh`.
