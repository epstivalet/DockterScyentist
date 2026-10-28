# RetroVerse PS5 Store — Integrated Browser Alpha

The PS5 ELF embeds a themed standalone storefront with A-Z game browsing, Libretro cover art and screenshots, the curated Internet Archive source picker, an optional Vimm's Lair handoff, a PS5-local download manager, and safe file uploads from a PC/phone into a **dedicated** RetroVerse folder.

**This is a browser-served PS5 app, NOT a controller-native .ffpkg.** Use the PS5 IP at http://YOUR-PS5-IP:8765/ while the ELF is running. The browser can run on a phone/PC with an accessible local network. USB and native launcher are later goals.

## Features and limits

- A-Z games and art require an active internet connection and Libretro's artwork HTML directory listing.
- RetroVerse queries only whitelisted HTTPS URLs for catalog and curated archive sources.
- The preconfigured curated source is the user-provided Internet Archive item \`ultimate-rom-collection\`. Entries in certain ZIP archives may be inaccessible or unindexed and some platforms have no configured collection.
- Downloads are limited to the allowed archive item and supported file extensions. The existing title and filename match is enforced by the source picker, but all content rights must be verified by the user.
- PS5 native HTTPS depends on firmware libraries / loader compatibility and has not yet been tested on hardware.
- ROMs are written under \`/data/retroverse/roms/<PLATFORM>/\`; uploads and downloads do not browse or overwrite system paths.
- One transfer at a time, no resume yet. Downloads offer progress and cancel; incomplete files are deleted.
- Cover art loads from Libretro on the browser. Original CoinOPS visual assets are served by GitHub and require browser internet; credits included. No commercial ROMs or copyrighted images are bundled.
- Do **not** expose PS5 port 8765 on the public Internet. This alpha has no login; run only on a trusted private LAN. No TLS is used between browser and PS5.
- The source is GPLv3-or-later. Original CoinOPS theme assets are CC BY-NC-SA 2.0 and remain externally linked with attribution.

## Build and load

The GitHub Actions workflow \`Build RetroVerse PS5 Store ELF\` creates the artifact \`RetroVerse-PS5-Store-ELF\`. Extract the ELF, load using your working jailbreak ELF loader, and navigate to \`http://PS5-IP:8765\` from a trusted device on the same LAN.

This update can replace the previously tested RetroVerse first-boot payload. Restart/stop that existing server first to free TCP port 8765.

## v0.6 - Download transport repair and test

On-console diagnostics showed the PS5 storage test PASS, while the native
sceHttpSendRequest failed before an HTTP response. The native HTTPS diagnostic
remains available for comparison, but the downloads and small XML test now
use PS5-targeted libcurl linked from the pinned PacBrew v0.40.2 release.

The build verifies that release's SHA-256, compiles a trusted CA bundle from
the CI runner into the ELF, and uses strict TLS peer/hostname verification.
Only HTTPS URLs are allowed, including redirects.

After installing, open DOWNLOADS and test:
1. PS5 STORAGE (should still pass)
2. SECURE CURL (new transport)
3. NATIVE HTTPS (known previously failing transport; comparison)
4. FULL FILE TEST (downloads a small XML metadata file, checks stored data,
   then deletes the test copy)

Only after CURL and FULL FILE both pass should the user attempt downloading
an authorized game file from HOSTS. The host listing is an index, not proof
every file is reachable. If CURL fails, record the full error text and avoid
retrying large files until investigated.

**Status:** GitHub compile is a build-level check. HTTPS and downloads still
require testing on the user's actual PS5. No automatic fallback disables TLS
certificate validation.
