# Trace Static

**A DIY sag measurement tool for mountain bikes.** Sit on the bike, read your sag. No zip ties, no helper, no squinting at a buried rear shock.

▶️ **Watch the build video:** [Cascade Singletrack on YouTube](YOUTUBE_VIDEO_LINK)
🔔 **Electronics breakdown and full build guide coming soon.** [Subscribe](YOUTUBE_CHANNEL_LINK) so you catch it.

![Trace Static on the bike](media/hero.jpg)

---

## What it does

Trace Static measures how far your suspension compresses under your weight (sag) and shows it as millimeters and as a percentage of total travel. It's built for normal riders who want a real number instead of setting air pressure by feel or from a sticker chart.

- Measures front and rear sag
- Shows sag in mm and %
- <!-- TODO: add 1–2 more features, e.g. battery life, mounting method, repeatability -->

## How it works

<!-- TODO: 3–5 plain-language sentences. What sensor, where it mounts, what it reads, and how it turns that into sag %. -->

Sag % = (rider sag in mm ÷ total travel in mm) × 100

## Accuracy

Tested at the bench against a ruler and on the bike against the manual O-ring/zip-tie method.

| Test | Result |
|---|---|
| Bench accuracy vs. ruler | <!-- TODO: ± x mm --> |
| Repeatability (5 readings, same position) | <!-- TODO: ± x mm --> |
| On-bike vs. manual method | <!-- TODO --> |

## What's in this repo

| Folder | Contents |
|---|---|
| `cad/` | Source CAD files and exported STEP files |
| `stl/` | Print-ready STL files |
| `firmware/` | Device firmware |
| `docs/` | Calibration guide and notes. Wiring diagram and build guide coming with the electronics video. |
| `media/` | Photos and images used in this README |

## Getting started

### 1. Print the parts
Print files are in `stl/`.
<!-- TODO: material (PETG/PLA), layer height, infill, supports, and part count -->

### 2. Get the electronics
A full bill of materials with part links is coming with the electronics video.
<!-- TODO: optional short list of main parts for now -->

### 3. Flash the firmware
Firmware is in `firmware/`. Built with the Arduino IDE.
<!-- TODO: board selection, required libraries, and upload steps -->

### 4. Calibrate
See [`docs/calibration.md`](docs/calibration.md).

## Roadmap

- [x] V1: proof of concept
- [x] V2: current design (this release)
- [ ] Wiring diagram, BOM, and full build guide (with the electronics video)
- [ ] <!-- TODO: anything planned next -->

**Why V2?** <!-- TODO: one or two sentences on what V1 got wrong and what V2 fixes -->

## Build it, mod it, share it

Trace Static is free for personal use. Print one, improve it, and send me your changes. Pull requests and build photos are welcome.

If you'd like to sell kits or units, or build something commercial on top of this, **just reach out first.** I'm happy to work something out. Credit and a link back to Cascade Singletrack are always appreciated.

## License

- **Hardware** (`cad/`, `stl/`, `docs/`): [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/). Free to build, modify, and share with credit. Commercial use needs permission.
- **Firmware** (`firmware/`): <!-- TODO: pick one. MIT = anyone can use it, including commercially. GPL-3.0 = modified versions they distribute must stay open. -->

See [`LICENSE`](LICENSE) for details.

## Follow along

- YouTube: [Cascade Singletrack](YOUTUBE_CHANNEL_LINK)
- Instagram: [@INSTAGRAM_HANDLE](INSTAGRAM_LINK)
- TikTok: [@TIKTOK_HANDLE](TIKTOK_LINK)

Trace Static is part of the **Trace** project: real suspension numbers for normal riders. The Trace suspension logger is next.
