# RetroVerse — PS5 Home-Screen Foreground Video Test

This is a **separate experimental installable PS5 application**, built with the clean-room [PS5 native app boilerplate](https://github.com/drakmor/ps5-native-app-boilerplate) and ShadowMountPlus-compatible folder packaging.

- App ID: `PPSA99873` (preliminary; check for collisions before installation)
- Visible name: **RetroVerse PS5**
- Intended application path: `/data/homebrew/PPSA99873/`
- Files include `eboot.bin`, `sce_module/libc.prx`, `sce_sys/param.json`, and an original RetroVerse `sce_sys/icon0.png`.
- UI: RetroVerse-branded full-screen VideoOut foreground **display test**, intended to establish that a home-screen app can show pixels.
- **Not yet** the CoinOPS store, a working PS5 on-TV downloader, or a gameplay-video player. Keep the v0.6 web version as the working store.

### Installation procedure (requires compatible jailbroken PS5)

1. Ensure your PS5 has an appropriate kstuff / homebrew compatibility layer and **ShadowMountPlus** running. Check whether `PPSA99873` is already occupied by an unrelated app; don't overwrite it.
2. Download the `RetroVerse-PS5-HomeScreen-Display-Test.zip` artifact from the GitHub Action.
3. Extract; the archive contains a top-level `PPSA99873/` folder.
4. Via FTP or a PS5 file manager, copy that folder to `/data/homebrew/`. Keep folder contents together.
5. Have ShadowMountPlus rescan/restart as appropriate. The icon *may* appear on the home screen, depending on your loader and firmware.
6. Select **RetroVerse PS5** and confirm whether the TV shows a RetroVerse-branded display test.
7. If it does not launch, **do not** install other system components blindly. Record the error, PS5 firmware, loader, and ShadowMountPlus version.

This test does not modify `/data/retroverse/roms/` and does not delete or overwrite the existing browser-based RetroVerse. It is a stage toward a complete foreground native application. Do not assume an ordinary background ELF can simply be renamed to `eboot.bin`; the build creates a proper native FSELF and its runtime module.
