# Micropolis 3DS — SimCity classic on the New 3DS, dual screen + stereoscopic

A native 3DS frontend over the open-source Micropolis engine (the original
SimCity simulation, GPLv3 — 21k lines of dependency-free C++, vendored in
`engine/` unchanged except one header guard).

**Status: runs on a New 3DS XL.** Every frontend change is also checked in a
headless desktop build that loads the bundled scenarios, simulates, renders
both screens and the stereo pair to PPM, and replays scripted button and
stylus input (`TOUCH`, `DRAG`, `PRESS` tokens).

## Layout

The DS-strategy layout (SimCity DS, Civ Rev, Anno DS): **you build on the
touch screen; the top screen is for looking**.

**Bottom (touch) screen** — a flat 2D map of the city at 16 px/tile (20×12
tiles), or 8 px/tile zoomed out (Y). Tap to use the tool; **drag** to paint
roads, rail, power lines, parks or the bulldozer in one stroke (4-connected,
so fast strokes stay joined); drag a zone or building and its footprint box
follows the stylus — yellow when it can be built, red when blocked or
unaffordable — and lifting the stylus builds it. Press and hold still to
query a tile. L or R + drag scrolls; dragging near an edge auto-scrolls.

- **Top bar:** funds (tap: budget) · date · speed (tap to cycle) · overlay
  (tap to cycle) · **!** jump to the latest alert · ☰ menu.
- **Tool strip:** Bulldozer, Road, Rail, Power line, Residential,
  Commercial, Industrial, plus an eighth slot holding your last pick from the
  full 4×4 grid of all 16 tools with prices (tap the slot again to open it).

**Top screen** — a 2× stereoscopic close-up (32 px/tile) that glides after
the cursor, so it always shows what you are building. Rendered in seven
depth layers: ground, roads and water on the screen plane, buildings rising
with **zone density** (downtown towers over the suburbs), vehicles nearest;
the cursor sits on the surface it points at. A HUD over it shows funds,
date, population, speed and R/C/I demand; a whole-city minimap framing the
touch map's area (and showing the active overlay); the newest city
message; and the selected tool with its price — or, while dragging, what the
stroke has cost so far.

Overlays (ZL/ZR or the top bar): City / Power / Pollution / Crime / Land
value / Traffic / Density / Fire cover / Police cover / Growth, auto-scaled,
tinted over both maps and the minimap.

Every menu, the budget (−/+ buttons), load-city list and the in-game manual
(Start > How to play; tap a chapter on the contents page) work by touch as
well as buttons.

## Controls

| Input | Action |
|---|---|
| Stylus | tap = use tool · drag = paint line / place zone · hold = query |
| L or R + drag | scroll the map |
| D-pad | move the cursor (map follows) |
| Circle pad | scroll the map |
| A | apply tool (hold A + D-pad to paint) |
| B | bulldozer; B again returns to the previous tool |
| L / R | previous / next tool in the strip |
| Y | zoom the touch map out / in (top close-up follows) |
| X | budget (tax rate, road/police/fire funding) |
| Start | menu: new city, the 8 scenarios, load, disasters, save, manual |
| Select | cycle speed (pause/slow/medium/fast) |
| ZL / ZR | previous / next overlay map |
| 3D slider | stereoscopic depth strength |

Saves go to `sdmc:/3ds/micropolis/autosave.cty` and are resumed at launch.
Load city lists the autosave first, then every `.cty` in `sdmc:/3ds/micropolis/cities/`.

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
is `kParallaxPx` (0.9 px per step at full slider, ×1.5 in the 2× close-up) — up to
~4.5 px between a road and a tower. Behind every elevated tile the ground
pass draws the nearest ground-level neighbour so the eye shift never exposes
black. If it still feels strong, lower `kParallaxPx` or the slider.
