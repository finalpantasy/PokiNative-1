# Pokemon Compass (Violet v2.1.1.1) in the recompiled game

## How it loads

Play it as a mod pack: `karpur.exe --mod compass` (or `TOTK_MOD_PACK=compass`) uses `mods/compass/` next to
`romfs-index.bin`: `romfs/` (the overlay below, `--out build/game/mods/compass/romfs`), `prelude.lua` (a copy of
`tools/compass_prelude.lua`) and `patch-sets.txt` (one line, `compass`: the code patch set with the plugin's hooks,
turned on when the pack loads). A pack keeps its saves apart (`<profile>/save-compass`, `save-compass-tx`,
`save-compass-backup`) and turns `TOTK_UI_PATCH` off. Without `--mod` the game runs unchanged on its own `save/`.
The pieces underneath, usable on their own:

- RomFS: `tools/mod_overlay.py <mod> --romfs game/romfs --out build/game/romfs-mods` turns the mod's `romfs/` into an
  overlay folder (manifest.tsv with size and SHA-256 of every file). The runtime loads `romfs-mods` next to
  `romfs-index.bin` when `TOTK_ROMFS_MODS` is unset (`TOTK_ROMFS_MODS=<folder>` points elsewhere, `0` turns it off).
  Compass: 2032 files, 5 replace RomFS files (`arc/data.trpfd`, `audio/wwise/bin/Switch/Patch00.pck`,
  `event/trevt/bin/event.trevt`, two `system/prefetch/data/global-resident_*.trpre`), 2027 are added.
- Compass's own `arc/data.trpfd` lists the archive without the 1483 files it changes and the files it adds, so the game
  opens those itself as loose files (`data:/<path>`), which the overlay serves. No loader is needed for the data.
- `TOTK_UI_PATCH=0` for Compass: our text patches apply when the game unpacks archive files, and Compass brings
  `message/` and `xmenu` as loose files.
- Lua: Compass's `main_dynamic.blua` calls globals that its exefs plugin registers. Without them the second main
  script stops with `field 'S0C1D2E3F4A5B6789' is not callable` and the game stays on a black screen.
  `TOTK_LUA_PRELUDE=tools/compass_prelude.lua` (runtime/lua_mod.c) defines them in each Lua state before the game's
  main script runs.
- A Compass save needs the trainer seed made at new game; a save started without the prelude fails Compass's seed check
  (`error_fatal_save_01`) and the game aborts on load. Start a new game with the prelude on.

## exefs: what the plugin does

`exefs/subsdk9` is an exlaunch module ("compass_exlaunch", 93,829-byte NSO, LZ4 segments) plus `main.npdm`. It is
AArch64 Switch code that patches the game's code at run time; it cannot run against the lifted x86-64 code. Its hooks
are ported as the code patch set `compass` (tools/fps_patches.py, host functions in runtime/compass.c, off unless a mod
pack's `patch-sets.txt` names it or `TOTK_COMPASS_HOOKS=1`). Decoded from its disassembly (llvm-objdump):

| # | Kind | Target in main | What it does | State here |
|---|---|---|---|---|
| H1 | hook | main+0x3b420 | keeps the lua_State the function returns (used by H3, H6, H7) | ported: its only call, main+0x1107af4, goes to host +0xf00 |
| H2 | hook | main+0x4c550 | after the original, registers the six Lua globals below in each Lua state | done by the prelude |
| H3 | hook | main+0x1781ce8 | EXP gain: EXP 0 at or above the level cap, else min(EXP x multiplier, `compass.prototype.Level_Cap_Check(cap)`). Cap and multiplier come from `compass.prototype.Update_Config(0/1)` (0 = cap, 0 means 100; 1 = index into 0.6..1.5, 0.0..0.5) | ported: its only call, main+0x1781b80, goes to host +0xf10, which runs the same Lua |
| H4 | hook | main+0x183b3e4 | after the original, a float field at +0x4c 1800.0 -> 3600.0 (a 30-minute duration doubled; likely sandwich power time, unconfirmed) | ported: the `movz w8, #0x44e1, lsl 16` that loads 1800.0 loads 3600.0 |
| H5 | inline | main+0x108370c | sets W21 = 1 before that instruction (meaning not determined) | not ported: that instruction, `orr w21, w8, w9`, sets W21 itself in 4.0.0, so the hook changes nothing |
| H6 | hook | main+0x114e98c | when the party order changes, calls `compass.update_battle_order(from, to, d)`, then the original unchanged (notification only) | ported: a call to host +0xf20 after the function's second instruction; every register is put back |
| H7 | inline | main+0x1141ba0 | installed by the Lua globals S1C95.. (W0 forced to 0) and SDE64.. (W0 = `c1A1CBE3B.fFCE06E04():f62782BA1()`) | ported: the call before it, main+0x1141b9c (bl main+0xe7e274), goes to host +0xf30, which overrides the result as the Lua switches ask |

Lua globals (registered by H2):

| Global | Plugin behavior | Prelude |
|---|---|---|
| S0C1D2E3F4A5B6789(...) | presence check: calls `compass.Yinsight()`, returns that many of its own last arguments; aborts the game if it errors | exact |
| S7E2857F9FF52F7EA(a, code) | true when `compass.GetString("Compass_TrainerSeed")` is non-empty, else `{status="error", errorCode=code, bootScene=true}`; false unless exactly 2 arguments | exact |
| S4CE0FA575C58E670() | new trainer seed: 128 keys, 10-bit records (key << 3 \| value) after a count byte, Base64 (216 chars); value ranges per key: 60, 61, 70: 0..1; 65: 0..4; up to 81: 0..2; 82..127: 0..7 | exact (Lua math.random instead of newlib rand) |
| SCD77E49FEB2D092C(seed) | decodes a seed into `{[key]=value}` | exact |
| S1C95BFA15FD0AD5D() | flag 0, inline hook H7 forcing W0 = 0 | prints `KARPUR_CMD compass h7 zero` (runtime/compass.c), returns its last argument |
| SDE64977E8D1E8DB4() | flag 1, inline hook H7 asking Lua for W0 | prints `KARPUR_CMD compass h7 query` |

Log lines to check them: `mod pack compass: code patch set compass on`, `compass: Lua state ...`,
`compass: EXP <before> -> <after> (level, level cap, EXP rate, ceiling)` after a battle that gives EXP (the first beach
battle with Nemona gives none), `compass: party order ...`, `compass: h7 zero|query`.
