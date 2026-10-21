# JoyStream: Stremio Client for Nintendo Switch

<p align="center">
  <b>JoyStream</b> is a native Nintendo Switch homebrew media streaming client powered by the <b>Stremio Addon Protocol (v3)</b>.
</p>

---

## Features

- **Stremio Addon Protocol v3 Integration:**
  - Browse official **Cinemeta** catalogs (Popular Movies, Series, New Releases, IMDb Ratings).
  - Search movies and TV series across addons with real-time metadata.
  - Stream aggregation across multiple addons (Torrentio, Real-Debrid / AllDebrid streams, Cyberflix, etc.).
- **Hardware-Accelerated Playback:**
  - Powered by embedded **`libmpv`** with Tegra X1 hardware video decoding (NVDEC) and FFmpeg.
  - Smooth 1080p / 720p playback for H.264, H.265 (HEVC), VP9, and direct HTTPS / Debrid links.
  - On-Screen Display (OSD) HUD with elapsed time, duration, buffer percentage, and interactive progress scrubber.
- **Switch Native UI & Input:**
  - Full Joy-Con and Pro Controller navigation with intuitive focus boxes.
  - Native Switch Software Keyboard (`swkbd`) integration for searching titles and entering custom addon URLs.
  - Official Nintendo Switch system font rendering (`plGetSharedFontByType`).
  - Dark modern TV-style layout.
- **Addon & Settings Persistence:**
  - Add, remove, or toggle addons via the in-app Addon Manager.
  - Saves your configuration to `sdmc:/switch/joystream/addons.json`.

---

## Installation on Nintendo Switch

1. Download [`joystream.nro`](https://github.com/Andrewx82/JoyStream/releases/download/v0.1/joystream.nro) from the [Releases](https://github.com/Andrewx82/JoyStream/releases) tab.
2. Place `joystream.nro` on your microSD card:
   ```text
   sdmc:/switch/joystream/joystream.nro
   ```
3. **Important - Title Override Launch:**
   * On Atmosphere CFW, **hold the `[R]` button while launching any installed retail or digital game**.
   * *Do not* open Homebrew Menu through the Album (Applet Mode). Applet Mode is limited to ~400 MB of RAM, whereas Title Override provides the full **~3.2 GB of RAM** required for video caching, high-bitrate streaming, and UI textures.
4. Launch **JoyStream** from the Homebrew Menu.

---

## Joy-Con Controls

### Catalog & Navigation
| Button | Action |
| :--- | :--- |
| **D-Pad / Left Stick** | Navigate movie / series cards |
| **(A)** | Select / View details / Open stream |
| **(B)** | Back to previous screen |
| **(X)** | Open on-screen keyboard for **Search** |
| **(Y)** | Toggle between **Movies** and **Series** |
| **(-)** | Open **Addon Manager** |
| **(+)** | Exit JoyStream |

### Video Player
| Button | Action |
| :--- | :--- |
| **(A)** | Toggle Play / Pause |
| **(B)** | Stop playback and return to details |
| **D-Pad Left / Right** | Seek backward / forward 10 seconds |
| **D-Pad Down / Up** | Seek backward / forward 60 seconds |
| **(+)** | Toggle On-Screen Display (HUD) |

---

## Configuring Custom Addons (Torrentio, Debrid, etc.)

### Method 1: Using the On-Screen Keyboard
1. Press **`(-)`** from the main menu to open the Addons screen.
2. Press **`[X]`** to trigger the Switch on-screen keyboard (`swkbd`).
3. Enter your addon manifest URL (e.g. `https://torrentio.strem.fun/realdebrid=YOUR_API_KEY/manifest.json`).
4. Press OK. JoyStream will verify the manifest and add it to your active providers.

### Method 2: Direct Configuration File
Create or edit `sdmc:/switch/joystream/addons.json` on your SD card:
```json
{
    "addons": [
        {
            "name": "Cinemeta",
            "url": "https://v3-cinemeta.strem.io",
            "enabled": true
        },
        {
            "name": "Torrentio",
            "url": "https://torrentio.strem.fun/realdebrid=YOUR_API_KEY",
            "enabled": true
        }
    ]
}
```

---

## Building from Source

### Switch Homebrew Executable (`.nro`):
Using Docker with devkitA64:
```bash
docker run --rm -v $(pwd):/workspace -w /workspace \
  -e PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:/opt/devkitpro/portlibs/switch/bin:/usr/bin:/bin \
  devkitpro/devkita64:latest bash -c "make clean && make -j$(nproc)"
```
Output: `joystream.nro`.

### Desktop Simulator (Linux):
```bash
mkdir -p build-desktop && cd build-desktop
cmake ..
make -j$(nproc)
./joystream-desktop
```

---

## License
JoyStream is open source under the MIT License.
