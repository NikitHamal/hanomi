# HANOMI — status tracker (read this first)

Goal: public `NikitHamal/hanomi` repo + GitHub Pages web original (repo root) + a native C++ Windows rebuild that is **the same game, same sprites, same world**, only lighter and smoother.

## v5 (latest) — locomotion / animation / AI overhaul (this session)
**Problem reported:** animals/humans get stuck, "walk in place", flicker between facings/animations.
**Root causes found (measured with the new `audit` QA command, `native/qa/motion.txt`):** facing recomputed raw every frame from the goal vector (flips at 45°); walk anim keyed to a boolean `moving` that stayed true while pinned on a wall; walk frame = `t*fps` (feet slide, jump at state changes); A* paths ignored `noWater` so animals routed into water then treadmilled; cornered flee loop never ended (one animal stuck 120 s); separation was frame-rate dependent and fought the steering; villagers retreated facing *toward* the zombie.
**Fix (native/src/game.h, logic.h, render.h):**
- `Ent::drive()/seek()`: smoothed velocity, whisker obstacle avoidance that commits to a side, wall sliding, corner-assist `move()` (slides ≤5 px round tile corners), hysteretic facing with turn cooldown, displacement-based stuck watchdog (sidestep → repath → abandon goal), eased arrival, stale-path invalidation, water-aware A*.
- `sampleMotion()`: `walking` (hysteresis + off-delay) and `ph` (gait phase advanced by **real distance**) drive animation: feet plant, no idle/walk flicker, never animates against a wall. Knockback/teleports excluded.
- AI: animals alert → flee with herd alarm, rabbits/chickens zig-zag, cornered animals freeze instead of treadmilling, babies follow adults, livestock stay near their pasture, wild foxes hunt wild rabbits/chickens (rest after kill), dogs follow with start/stop hysteresis and puff teleport, wolves stalk then lunge and drift as packs, villagers flee facing away.
- Render: sprite frames from gait phase; dogs never "run" in place; rabbit/chicken hop arcs; goal chip HUD.
- Perf: allocation-free `query2/nearest`, grid buffers reused, O(1) `sp()`, dt-scaled separation (avg 2.9 ms/frame headless incl. software raster).
**Measured (8 animals + 6 villagers + dogs, 2 min, `audit`):** animals walking-in-place 40 % → ~2 % (longest stuck 120 s → 0.1 s); villager moving-toggles 60/min → 7/min; facing flips animals 17 → 8/min.
**Visual QA:** web reference shots (Playwright/Chromium) vs native shots compared: scene layout, house, player/dog sprites, cobble, lanterns, hotbar identical; differences are the web seed (random) and the web's sun/moon clock dial + circular minimap + intro banner (backlog).
**Backlog (next):** sun/moon clock dial & round minimap styling, intro "Welcome home" banner, ground sparrows, chunk bake worker thread, 8-direction walk frames, sleeping animals at night, satchel tabs.

## v2 — 1:1 port of the web game
V1 used invented procedural art and looked nothing like the web game. V2 is a faithful port:

| Layer | How it matches the web game |
|---|---|
| Sprites (trees, crops, structures, icons, animals, dogs, banners, stalls, houses, HUD glyphs) | `tools/export_assets.mjs` runs the **original `js/art/*.js` + `shared/pixel-dog.js`** under Node with a canvas shim and packs 813 sprites into `native/res/assets.bin` (550 KB, LZ4) which is embedded in the exe. Houses are exported colour-independent with a class mask so nation colours apply at runtime. |
| Terrain / walls / water glints | `terrain.h` = line-by-line port of `art/terrain.js` (same noise tables, jitter, voronoi cobble, turf lips, foam, ore walls). |
| Characters | `chars.h` = port of `art/chars.js` (layered, sel-outlined 24x32 chibi; villager/soldier/zombie/player looks use the same seeds + RNG). |
| World | `world.h` = port of `defs.js`, `items.js`, `world.js` (identical hash/noise/RNG => same world for a seed, same villages, same recipes). |
| Rules | `game.h` + `logic.h` = port of `game.js`, `entities.js`, `player.js`, `fx.js` (villager routines, soldiers, zombies, Blood Moon, wolves, farming, fishing mini-game, mining, building, nations/war/ally/vassal, interiors, goals, shrines, treasure digging, tool swing arcs, bow/spear/torch props). |
| Renderer | `render.h` = port of `render.js` (chunk layers, y-sorted drawables, shadows, dusk multiply, night light holes + warm glow + window glow, rain/snow, fish shadows, birds, petals, fireflies, koi pond). |
| UI | `ui.h`/`ui2.h`: full-resolution HUD + panels using the original CSS palette, ink/wood frames and the **Pixelify Sans** font (stb_truetype, embedded). Minimap, hotbar, hearts/bowls, toasts, banner, bubbles, fishing bar, satchel+crafting, chest, market, dialogue, chief tasks, realm, map, journal, settings, title screen with koi pond. |
| Audio | V1 synth (`audio.h`): SFX + generative music + ambience. |

## Visual QA (HTML vs native, 1280x720)
Reference shots from the web game were captured with headless Chromium (Playwright); native shots with `hanomi_headless` (`native/qa/*.txt`). World, villages, characters, dog, hats, houses, night lighting and panels match; remaining visible differences listed below.

## v3 (this session) — save system overhaul, "game not loading" fixed
**Root cause:** `drawNew`/`drawLoad` ended with `if (modal == M_NONE) modal = M_TITLE`, which undid `startNewGame` / Load's `modal = M_NONE` in the same frame, so New / Continue / Load always bounced back to the title. Also a title click could fall through onto the widget of the panel it opened.
**Fixes / new (native/src/saves.h, ui2.h):**
- Multi-world saves: `%APPDATA%\Hanomi\worlds\*.hsav` (header metadata + body + end marker), atomic write via `.tmp`, `.bak` rollback, auto-recover from backup, corrupt/truncated detection, count sanity limits; legacy `hanomi.sav` is imported once. Interiors (`inside`, return points) are now saved.
- World browser (title > Load / manage worlds): portrait, day, difficulty, seed, play time, last played; **Play, Rename, Copy, Delete (with confirm)**, scroll wheel/scrollbar; Continue = most recent playable world; error banner on failed loads.
- Pause: Save reports failures; Save & quit works; autosave goes to the current world slot.
- Windows: window is shown with a loading splash before init, save folder falls back to %TEMP% if APPDATA is unwritable, crash handler writes `crash.txt` + message box.
- Verified: headless script `native/qa/ui_flow.txt` (new -> play -> pause -> save&quit -> continue -> load screen -> delete) **and the real cross-compiled hanomi.exe under Wine + Xvfb with real mouse clicks** (New world, Begin, Save&quit, Continue, Load list, Delete).

## Known gaps / backlog
- Koi pond is a simplified sim (shape, koi, lilies, ripples, feeding) — the 1685-line `koipond.js` is not ported.
- Web audio engine (`audio.js`, 996 lines) is approximated by the V1 synth, not ported note-for-note.
- Satchel UI: no category tabs/split-stack/durability tooltip yet; trade/chief dialogues are simplified.
- Terrain chunk bake hitches (~40 ms) when entering new chunks; move to a worker thread.
- Windows exe now runs under Wine in CI-less QA; not yet tested on real Windows hardware.
- Web reference screenshots were not re-captured in v3 (no Chromium in sandbox); v2 parity shots still apply.

## Build
`sh tools/export.sh` (regenerate sprites) · `make -C native win` (hanomi.exe) · `make -C native headless` (QA; `python3 tools/ppm2png.py shots/*.ppm`).
Releases: https://github.com/NikitHamal/hanomi/releases
