# Cascade Singletrack

**Open-source MTB tools and experiments from a normal rider's garage in Bend, Oregon.**

Cascade Singletrack is a mountain bike channel about Central Oregon trails, honest gear takes, and building my own bike tech. When a project from the channel is worth sharing, the design files and code end up here, free to build, modify, and learn from.

▶️ **YouTube:** [Cascade Singletrack](YOUTUBE_CHANNEL_LINK)
📸 **Instagram:** [@cascadesingletrack](https://www.instagram.com/cascadesingletrack/)

---

## Projects

| Project | What it is | Status | Video |
|---|---|---|---|
| [**Trace Static**](trace-static/Trace_S_README.md) | DIY sag measurement tool. Clip it on, sit on the bike, and read front or rear sag in mm and % on your phone. XIAO ESP32-C6, 3D-printed parts, FreeCAD source. | ✅ v2 released | [Watch](YOUTUBE_VIDEO_LINK) |
| **Trace** | Suspension logger that records what your fork and shock actually do on the trail. | 🔧 In development | Coming soon |

More projects get added as they come off the bench. [Subscribe](YOUTUBE_CHANNEL_LINK) to catch the build videos.

## Repo layout

Each project lives in its own folder and is self-contained:

```
cascade_singletrack/
├── README.md            ← you are here
└── trace-static/        ← Trace Static sag tool
    ├── cad/             FreeCAD source files
    ├── stl/             Print-ready parts
    ├── firmware/        Arduino sketch for the XIAO ESP32-C6
    ├── docs/            Schematic, wiring diagram, change log
    └── media/           Images used in the docs
```

Start with a project's own README. It covers parts, printing, wiring, and setup for that build.

## Building one?

- **Read the project README first.** Some builds have a companion video that shows steps the docs can't.
- **Check the change log** in the project's `docs/` folder. Parts and wiring get revised between versions.
- **Share it.** If you build or improve something from this repo, send me a pic or open an issue. I'd love to see it, and it might end up in a video.

## Issues and contributions

- **Found a bug or a mistake in the docs?** Open an [issue](https://github.com/pullenrc/cascade_singletrack/issues).
- **Made an improvement?** Pull requests are welcome. Keep changes inside the relevant project folder and say what you changed and why.
- **Questions about your own bike setup?** Ask in the YouTube comments on that project's video. Other riders can see the answer there too.

## License

Each project sets its own license in its README. The default for this repo:

- **Hardware** (CAD, STL, docs): [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/). Free to build, modify, and share with credit. Commercial use needs permission.
- **Firmware/code:** see each project's README.

**Want to sell kits or units, or build something commercial on top of a project here?** Just reach out first. I'm happy to work something out. Credit and a link back to Cascade Singletrack are always appreciated.

## Disclaimer

These are hobby projects built by a regular rider, not certified tools. Double-check your setup, follow your suspension manufacturer's recommendations, and ride at your own risk.
