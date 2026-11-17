# Third-party notices

Engine code is GPLv3 because of AGDS reuse. Original game files are not licensed
by this repository and are never included in a public builder.

| Component | Version | License and source |
| --- | --- | --- |
| SDL3 | 3.4.20 | zlib; https://github.com/libsdl-org/SDL |
| nlohmann/json | 3.12.0 | MIT; https://github.com/nlohmann/json |
| stb | pinned commit | MIT or public domain; https://github.com/nothings/stb |
| dr_libs | pinned commit | MIT-0 or public domain; https://github.com/mackron/dr_libs |
| doctest | 2.4.12 | MIT; test-only dependency |
| Real-ESRGAN ncnn runner | 20220424 | MIT; official v0.2.5.0 binary release |
| AnimeVideo v3 model | v0.2.5.0 | BSD-3-Clause; https://github.com/xinntao/Real-ESRGAN |
| ncnn | bundled by official runner | BSD-3-Clause; https://github.com/Tencent/ncnn |
| MoltenVK | statically bundled by official Mac runner | Apache-2.0; https://github.com/KhronosGroup/MoltenVK |
| FLAC | 1.5.0 | GPL encoder, BSD libFLAC; https://github.com/xiph/flac |
| libogg | bundled by Windows FLAC | BSD; https://github.com/xiph/ogg |
| llvm-mingw | 20261104 UCRT | Apache-2.0 with LLVM exception for libc++/libunwind; runtime license included |
| DejaVu fonts | 2.37 | Bitstream Vera / DejaVu free license; embedded in the builder |
| XChaCha20-Poly1305, HKDF | builder/crypto.cpp | own implementation, verified against libsodium; Poly1305 after poly1305-donna (public domain) |

Explicit fetching uses the versions, URLs and SHA-256 values in
`dependencies.lock.json` and `builder/pins.json`. CMake never downloads.
Third-party source license files are copied into the builder payload and the
finished game. Keep them with any redistribution. The
same pins identify upstream corresponding source. No third-party legal notices
are replaced with project authorship.
