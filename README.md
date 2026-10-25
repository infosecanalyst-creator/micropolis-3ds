# Micropolis 3DS — SimCity classic on the New 3DS, dual screen + stereoscopic

A native 3DS frontend over the open-source Micropolis engine (the original
SimCity simulation, GPLv3 — 21k lines of dependency-free C++, vendored in
`engine/` unchanged except one header guard).

**Status: frontend fully working on desktop (headless test harness), untested
on hardware.** Unlike the DCSS and Ultima scaffolds, this one has actually
*run*: the headless build loads the bundled scenarios, simulates, renders
both screens and the stereo pair, and responds to scripted input. Only the
libctru layer (`src/3ds/platform_3ds.cpp`, ~120 lines of framebuffer and HID
glue) is unverified.

## Layout

**Top screen** — the city at 16 px/tile (25×15 tiles), or 2× zoom (Y).
Rendered in seven depth layers for the 3D slider: water sinks behind the
screen plane, ground/roads/rail sit on it, and buildings rise with **zone
density** — trees and houses one step, low-density zones two, mid three,
heavy industry/power plants/stadium four, top-density commercial towers
five — with trains/planes/helicopters/the monster flying in front. Downtown
visibly towers over the suburbs. Each layer is drawn with a per-eye horizontal shift; the ground layer
is drawn under elevated cells so parallax never exposes holes. A blinking
cursor shows the tool footprint; the newest message appears as a strip.

**Bottom screen** — funds / date / population / speed strip; a 1 px-per-tile
minimap with the viewport rectangle (tap to jump) that doubles as the
**overlay map** — ZL/ZR (or tap the label) cycle City / Power / Pollution /
Crime / Land value / Traffic / Density / Fire cover / Police cover / Growth,
auto-scaled to the map's current range, and the same overlay is tinted over
the city on the top screen; the 16 tool icons in a
4×4 grid (tap to select); speed buttons and Budget; R/C/I demand bars; the
last four messages; and a control legend.

## Controls

| Input | Action |
|---|---|
| D-pad | move the cursor (camera follows) |
| Circle pad | scroll the camera |
| A | apply tool (hold and move to drag roads/rail/wire) |
| B | bulldozer |
| L / R | previous / next tool |
| Y | toggle 2× zoom |
| X | budget (tax rate, road/police/fire funding) |
| Start | menu: new city, the 8 scenarios, disasters, save |
| Select | cycle speed (pause/slow/medium/fast) |
| ZL / ZR | previous / next overlay map |
| Touch | minimap jump, overlay label, tool icons, speed buttons |
| 3D slider | stereoscopic depth strength |

Saves go to `sdmc:/3ds/micropolis/autosave.cty` and are resumed at launch.

## Build

```bash
# desktop test (no SDL needed; dumps PPM frames):
make -f Makefile.headless
./micropolis-headless --scenario 5 --res res --frames 200 --every 50 --slider 1 --out /tmp/mp

# 3DS (devkitPro, e.g. in the devkitpro/devkitarm Docker image):
make            # -> micropolis.3dsx with romfs (scenarios + sample cities)
make cia        # needs makerom + bannertool
```

Copy `micropolis.3dsx` to `sdmc:/3ds/micropolis/` and launch from the
Homebrew Launcher. No external data needed — everything is in romfs.

## Files

| Path | What |
|---|---|
| `engine/` | Micropolis engine (SimHacker/micropolis `MicropolisEngine/src`) |
| `src/app.cpp` | the whole frontend: state, engine callbacks, rendering, input |
| `src/fb.h` | tiny software rasterizer (tiles, sprites, text) |
| `src/platform.h` | the 7-call platform abstraction |
| `src/3ds/` | libctru backend |
| `src/headless/` | desktop backend: scripted input, PPM output |
| `assets/` | generated: 960 tiles, 61 sprite frames, 18 icons, 5×10 font (one shared 256-colour palette) |
| `tools/gen_assets.py` | regenerate `assets/` from the Micropolis PNGs |
| `res/` | scenarios (`snro.*`), terrain data (`hexa.*`), 24 sample cities |

## Ideas for later

- Graphs and the evaluation screen (engine callbacks `history`, `evaluation`).
- Sound: the engine emits `makeSound` callbacks with the original sound names.

## In-game manual, city files, tooltips

- **Start → How to play**: five pages (controls, starting a city, zones and
  demand, services and budget, overlays/disasters/files) on the bottom screen.
- **Start → Load city…** lists `.cty` files from `sdmc:/3ds/micropolis/cities/`.
  That is the "mod" surface Micropolis has: the format is the original
  SimCity (1989) city file, so classic cities and anything saved by
  Micropolis/SimCity Classic load as-is. There is no scripting or tileset
  mod system in the engine.
- The selected tool shows a one-line description under its name.

## Stereo tuning

Water, roads, rail and bare land sit on the screen plane; buildings rise
with density (low → tower, 4 steps), vehicles and the cursor on top. Parallax
is `kParallaxPx` (0.9 px per step at full slider, ×1.5 when zoomed) — up to
~4.5 px between a road and a tower. Behind every elevated tile the ground
pass draws the nearest ground-level neighbour so the eye shift never exposes
black. If it still feels strong, lower `kParallaxPx` or the slider.
