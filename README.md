# PS5Upload for iOS — Unofficial Community Port

> **This is an unofficial iOS build of [PS5Upload by phantomptr](https://github.com/phantomptr/ps5upload).** The original PS5Upload application, its features, Rust engine, and PS5 payload are the work of **phantomptr and the upstream contributors**. This repository does **not** claim ownership or authorship of their software and is **not an official upstream iOS release**.

## Original project and credit

**All credit for creating PS5Upload goes to [phantomptr](https://github.com/phantomptr) and the contributors to [phantomptr/ps5upload](https://github.com/phantomptr/ps5upload).**

- **Original application and source:** https://github.com/phantomptr/ps5upload
- **Official upstream releases:** https://github.com/phantomptr/ps5upload/releases
- **Upstream license:** [GNU GPL v3](https://github.com/phantomptr/ps5upload/blob/main/LICENSE)

PS5Upload lets users transfer games, apps, and homebrew to a compatible jailbroken PS5, as well as manage the console. For the original project's functionality, documentation, and updates, please visit the upstream repository.

## What this repository actually provides

This project is a **community-maintained iOS build/port**, not an independent rewrite or a new PS5Upload application. It checks out the original PS5Upload source code and adds the build setup needed to package its existing Tauri/React frontend and Rust mobile engine as a native, **unsigned arm64 iPhone IPA**.

The work in *this* repository is limited to:
- GitHub Actions automation for producing an iOS IPA from upstream source.
- iOS-specific app configuration, including a minimum iOS version of 16.0 and local-network permissions.
- Build compatibility adjustments and the PS5 helper payload build.
- A custom pixel-art iOS home-screen icon.

No credit for the original program, its core features, or its PS5 integration is claimed here. This is an independent community effort and is not endorsed or maintained by the original developer.

## Current build

- **Upstream source tag:** [v7.0.33](https://github.com/phantomptr/ps5upload/releases/tag/v7.0.33)
- **Target:** iPhone / iOS 16.0+, arm64
- **Package:** unsigned `.ipa` (not an App Store distribution)
- **Build result:** The v7.0.33 GitHub Actions build [#10](https://github.com/epstivalet/DockterScyentist/actions/runs/39007668989) completed successfully and produced an IPA.
- **Device testing:** An earlier v7.0.22 build was tested successfully on an iPhone running iOS 16.3.1 with Dopamine. A successful v7.0.33 build **does not by itself establish that every feature works on-device**.

### Download

See this repository's [Releases](https://github.com/epstivalet/DockterScyentist/releases) for available unsigned iOS packages. The [v.33 release](https://github.com/epstivalet/DockterScyentist/releases/tag/v.33) contains `PS5Upload-v7.0.33-iOS16-unsigned-IPA.zip`; extract the ZIP to obtain the IPA.

The unsigned app requires a compatible installation method, such as TrollStore or an appropriate jailbreak setup. It is **not** an officially signed App Store application.

### Build it yourself

1. Open [Actions](https://github.com/epstivalet/DockterScyentist/actions).
2. Choose **Build PS5Upload iOS 16 (unsigned)**.
3. Select **Run workflow** on the `main` branch.
4. When the workflow succeeds, download the iOS IPA ZIP from **Artifacts**.

The workflow builds the upstream PS5 payload on Ubuntu, then uses macOS, Xcode, Rust, and Tauri to compile and package the actual iOS application. See [the workflow](.github/workflows/build-ps5upload-ios.yml) and the [iOS configuration](ios/) for implementation details.

## Notes and limitations

- An iPhone and a compatible PS5 must be able to communicate over the local network. Do not expose the app's local engine directly to the public internet.
- File access, backgrounding, and transfer behavior can differ on iOS; not all upstream platform features are necessarily verified here.
- The app and helper payload originate from the upstream project; redistribution and modified builds must respect the [upstream GPLv3 license](https://github.com/phantomptr/ps5upload/blob/main/LICENSE) and applicable source-distribution obligations.
- This community repository is unaffiliated with the upstream maintainer or Sony.

**If you appreciate PS5Upload, please support and credit [the original developer and upstream project](https://github.com/phantomptr/ps5upload).**
