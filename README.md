# PS5Upload v7.0.22 — unsigned native iOS build kit

**Status: build kit, NOT a built or tested IPA.** Prepared for an iPhone on iOS **16.3.1** using Dopamine (rootless jailbreak). The proposed output is an unsigned `arm64` `.ipa` suitable for installation with a compatible installer such as TrollStore. **The first CI run may expose iOS-specific compilation errors** that need fixes in upstream source; this workflow has not been run on a macOS host or tested on a phone.

## What it is

This is not a Linux `.deb` to `.ipa` converter, nor a web wrapper. It builds the actual Tauri/React client and in-process Rust engine found in [phantomptr/ps5upload v7.0.22](https://github.com/phantomptr/ps5upload/tree/v7.0.22), along with its real PS5 helper payload. In PS5Upload, `client/src-tauri/src/engine_mobile.rs` and the iOS `cfg` dependencies already exist. That makes an experimental native build possible without designing a new protocol implementation.

No Windows server should be necessary **if the iOS build and its networking functions work on the device**, but that is not yet tested. The PS5 must be jailbroken and have an appropriate ELF loader/helper in order to perform PS5Upload functions.

## Windows-only setup (requires a GitHub account)

1. Download and extract this ZIP on your Windows PC, preserving hidden `.github` folder.
2. On GitHub, create a **new** repository, e.g. `ps5upload-ios-builder`. Keep it **public** if you want normal free public-repo macOS runner allowance; check GitHub Actions usage and pricing on your account. Do *not* upload a modified copy of the official PS5Upload source: the workflow checks out v7.0.22 on its own.
3. Commit this kit's three files into the new repository with the exact paths:
   - `.github/workflows/build-ps5upload-ios.yml`
   - `ios/Info.ios.plist`
   - `ios/tauri.ios.conf.json`
   You can use GitHub Desktop: **File → Add local repository → Create a repository here** for the extracted folder, commit, then publish. Alternatively initialize Git with PowerShell (commands below).
4. In your GitHub repository, open **Actions → Build PS5Upload iOS 16 (unsigned) → Run workflow**.
5. If both jobs finish green, open the completed run's **Artifacts** section and download `PS5Upload-v7.0.22-iOS16-unsigned-IPA.zip`. Extract it to get the `.ipa`.
6. Transfer the `.ipa` to your iPhone and open it with **TrollStore**, which supports iOS 16.3.1. Installation is separate from successful compilation; testing with your PS5 is still necessary.

### Alternative: initialize/push with Git for Windows (PowerShell)

First create an empty repo on GitHub (without a README), install Git for Windows, and in a PowerShell window inside this kit folder execute:

```powershell
git init -b main
git add .
git commit -m "Add PS5Upload v7.0.22 experimental iOS build"
git remote add origin https://github.com/YOUR_GITHUB_USERNAME/ps5upload-ios-builder.git
git push -u origin main
```

Replace `YOUR_GITHUB_USERNAME` with your GitHub username. GitHub will require your own login/authorization.

## What the workflow does

- On Ubuntu: checks out the exact upstream v7.0.22 tag, installs the upstream checksum-pinned PS5 Payload SDK + LLVM 22, compiles the real `ps5upload.elf` and installer daemon, and transfers their gzip outputs to the next job.
- On a GitHub-hosted macOS machine: checks out upstream v7.0.22 again; installs Node, Rust `aarch64-apple-ios`, CocoaPods, and XcodeGen; overlays this kit's iOS-only configuration (minimum iOS 16.0 and local-network permission); calls `tauri ios init` and `tauri ios build --no-sign`; checks that a device-arm64 IPA was created, and uploads it as an Actions artifact.
- No signing keys are requested. The `.ipa`, *if built*, is **unsigned**, intended for jailbreak/TrollStore use. It is not an App Store app or an official release from the developer.

## Troubleshooting and limitations

- **A red iOS job** means the upstream iOS target failed to compile, link, or package. Open that run, copy the error near the end of the failed step (especially the first Rust error) and share it for a targeted source fix. Do not mistake the existence of a build workflow for proof of iOS compatibility.
- **`No matching .ipa`**: inspect the Tauri command output and Xcode archive logs. The kit deliberately doesn't upload simulator builds and doesn't manufacture an IPA from desktop binaries.
- **iOS file access**: Android uses special all-files permissions and paths, which don't translate directly to iOS. Phone file pickers, large-file transfers, install streaming, and long background transfers may need separate iOS adaptations.
- **Local network**: iOS may prompt to allow local network access. The iPhone and PS5 need to be reachable on the same LAN. Don't expose the HTTP engine to the public Internet.
- **Backgrounding**: iOS may suspend the app and its in-process engine; keep the app in the foreground for transfers until tested.
- **License**: PS5Upload's source is GPL-3.0; if you redistribute modified binaries, comply with the upstream license and provide corresponding source and attribution.

## What is and is not verified

Verified by source inspection: upstream has `engine_mobile.rs`, iOS Cargo dependencies and a Tauri mobile entry point; Tauri 2 supports `--no-sign` builds and unsigned IPA output. This kit's YAML, plist, and JSON were checked for syntax locally. **No macOS/GitHub Actions build was run, and no working IPA or PS5 connection is claimed.**
