# RetroVerse PS5 TV — Native SDL2 Alpha

This is the **on-console, native TV alpha** of RetroVerse. It runs as a PS5 ELF payload. You don't need a phone or PC browser after the ELF has been loaded. This is not yet an installed home-screen title.

The ELF renders a 1920x1080 fullscreen interface using SDL2, reads DualSense button events, displays an A–Z console catalog, draws cover artwork fetched securely from Libretro, and embeds the existing RetroVerse HTTP backend (running on port 8765 in the same payload). The backend downloads permitted source files using verified HTTPS libcurl to `/data/retroverse/roms/<CONSOLE>/`.

Controls: **D-pad Up/Down** scroll games; **D-pad Left/Right** switch alphabet letter; **L1/R1** switch console; **Triangle** changes tab (Store / Hosts / Library / Downloads); **Cross** selects, then starts a source download from details; **Circle** cancels details or closes the payload; **Options** exits. A keyboard can also control it with arrows, Q/E, Tab, Enter, Esc.

The **Hosts** tab is limited to filenames confirmed in the curated Archive collection index. It is not proof that individual download URLs work. **Store** displays the much larger Libretro game list and checks whether a curated file name matches before enabling a download.

This is an experimental native alpha and has not been tested on actual PS5 video output. The previous web interface remains available as a backup at `http://PS5-IP:8765` while the native ELF is running, on a trusted private LAN.

**Known limitations:** This does not directly launch games or run an emulator. Gameplay video previews, full CoinOPS XML theme rendering, native installation/home-screen packaging, and game metadata beyond titles are future work. Cover-art lookups can be slow or missing. Some controllers/firmware combinations may map buttons differently. Only download content you are legally authorized to use.

Only run on a trusted LAN; the backup HTTP service has no authentication. Do not port-forward it.
