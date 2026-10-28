# RetroVerse PS5 Alpha 0.1 — First boot

This branch is isolated from the PS5Upload project's main branch.

The PS5 ELF hosts a **read-only first-boot web page** at `http://PS5-IP:8765/` and returns health status from `/api/health`.

This alpha does **not** include the full RetroVerse CoinOPS UI, ROM download engine, storage access, or controller-native PS5 app. It tests that the SDK-built payload loads and serves a page on a trusted local network.

Build: GitHub Actions `Build RetroVerse PS5 Alpha ELF` produces artifact `RetroVerse-PS5-Alpha-ELF`.

You need a compatible, working PS5 ELF loader. Do not forward port 8765 to the internet. Sources are GPL-3.0-or-later; no ROMs or proprietary SDK material included.