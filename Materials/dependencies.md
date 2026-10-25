# Third-party components

- **nlohmann/json 3.11.3** — MIT; fetched by CMake into its build cache, with the header and license checked against `Configurations/Release/dependencies.json`. Header SHA-256: `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`.
- **DM Sans** — copied from the macOS X-Lab reference; SIL Open Font License included at `Examples/XLab/assets/DMSans-OFL.txt`.
- **Qt 6.8.3** — example only; dynamically linked, supplied by the developer's Qt installation. Preserve Qt and its bundled multimedia/image-format notices when packaging the example.
- **Agora Native Windows SDK 4.6.2** — optional RTC adapter dependency; obtained from the download URL locked in `Configurations/Release/dependencies.json`. Preserve the upstream SDK notices. SDK archive SHA-256: `44d0caabc2b5232b0416df63845d88e45de45681b9d32d99617dbd41ba8e6523`.
- **System platform APIs** — WinHTTP, BCrypt, Media Foundation and GDI on Windows; libcurl, CommonCrypto and AVFoundation for the macOS development build.

The Xmax wordmark and preset catalog originate from the user-supplied macOS XmaxSDK project. Reference image URLs remain the same as that project's catalog.

`Materials/images/` and `Materials/videos/` reuse the shared branding and model demonstration media from that project. `Materials/images/xlab/` contains iOS example references; it does not depict the Windows runtime. Windows installation and usage guides are maintained separately in `Materials/`.

Qt 6.8.3 Demo packages retain the upstream qtbase, qtdeclarative, qtmultimedia, qtimageformats and qtsvg license texts and corresponding source references. The local packaging tools cache these notices outside the tracked source tree. CMake installation also includes the downloaded nlohmann/json MIT license in `licenses/nlohmann-json-MIT.txt`.
