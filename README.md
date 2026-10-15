# orion2re for OrionLayer

This is [orion2re](https://github.com/orion2re/orion2re) **v2.0.0** with the
changes [OrionLayer](https://github.com/Data-crea/orionlayerv3) needs: the
**Extension API** (a local TCP interface a frontend reads the game from and
sends input through) and the engine fixes OrionLayer asked for, one commit
each. The branch is `orionlayer`; `main` follows upstream.

orion2re is free software under the GNU General Public License, version 3 or
any later version ([`LICENSE`](LICENSE)); so are these changes. It needs the
original game files of Master of Orion II (GOG or Steam), which are not
included and not covered by this licence.

## Build

The Extension API is built only with `-DORION2RE_EXT=ON`. Give the folder
of your Master of Orion II installation as `ORION2RE_GAME_DIR`: the build
then puts the engine and its own data (`data/base.zip`, `data/orion2re.cfg`,
`data/mods/`) into that folder, where the engine expects them.

Linux (Debian/Ubuntu: `sudo apt install build-essential cmake ninja-build git
python3`):

```sh
git clone --recurse-submodules --branch orionlayer https://github.com/Data-crea/orion2re.git
cd orion2re
cmake --preset linux-release -DORION2RE_EXT=ON -DORION2RE_GAME_DIR="$HOME/Master of Orion 2"
cmake --build --preset linux-release
```

Windows (Visual Studio 2022 with "Desktop development with C++", CMake and
Ninja, in a "x64 Native Tools Command Prompt"):

```bat
git clone --recurse-submodules --branch orionlayer https://github.com/Data-crea/orion2re.git
cd orion2re
cmake --preset win64-release -DORION2RE_EXT=ON -DORION2RE_GAME_DIR="C:\GOG Games\Master of Orion 2"
cmake --build --preset win64-release
```

The engine is then `orion2re` (`orion2re.exe`) in the game folder; OrionLayer
starts it (`python play.py --engine <that file> --game <the game folder>`).
The other presets (debug, MinGW cross-builds, macOS) work as upstream
describes below.

## The Extension API

[`docs/extension-api.md`](docs/extension-api.md): how to connect, the
messages, the snapshot, the input commands, and the `LAYOUT` message that
names every member of the records the snapshot carries, so a client keeps
working when a record grows or moves.

## OrionLayer's changes

One commit per change on top of v2.0.0 (and upstream's own `6f86fc0e`,
"Normalize tracked text to LF", which the ports need); each commit's message
says what it changes and why, and every changed place carries a one-line
comment `OrionLayer, open fix N.` The changes sit inside
`#ifdef ORION2RE_EXT` — except three small ones that change the game
without the flag too: fix 5 (`racesel.cpp` records the previous race),
fix 12 (`colmove.cpp`: a refused colonist move says why in the help line
instead of a message box) and fix 31 (`platform.cpp`: VSync can be switched
off on request).

| commit | change |
|---|---|
| `8dce5c84` | Extension API: src/ext and its hooks in the engine (OrionLayer local) |
| `74b06b98` | Open fix 3: an injected click keeps its pointer (platform.cpp) |
| `b2fa0c79` | Open fix 5: Race_Selection_Screen_ records _old_race (racesel.cpp) |
| `3fea2fb1` | Screen IDs for Select Race and Custom Race (racesel.cpp) |
| `1982b2a1` | Open fix 12: pop-move refusals without the message box (colmove) |
| `77b1fb07` | Open fix 22: race selection reports the synthetic 51, not SCREEN_RACE (racesel.cpp) |
| `62cc8968` | Open fix 24: the two turn-start research dialogs report their own ids, on the wire only |
| `a7852545` | Open fix 25: an activated research row is the row that is chosen (tech.cpp) |
| `0aae309f` | Open fix 27: the fleet screen's view state is not on the wire (ext_api.cpp) |
| `39bfe4fb` | Open fix 28: one ship can be selected on the fleet screen (ext_api.cpp) |
| `007b4aa3` | Open fix 31: present without VSync on request (platform.cpp) |
| `fa61b768` | Open fix 30: the Leaders screen's view state on the wire (ext_api.cpp) |
| `aa59219a` | Open fix 32: the Info screen's history divisors and turn messages on the wire (ext_api.cpp) |
| `59425f3c` | Open Fix 34: send the save slots for the main menu's Load dialog too (ext_api.cpp) |
| `f8c859b4` | Open Fix 35: send which colony the colony screen and the build popup show ("COLS") |
| `45aba360` | Open Fix 36: send where the colony screen puts its buildings ("CBLD") |
| `1a3a449a` | Open Fix 37: send the colony screen's Plague and Pop Boom answers ("CEVT") |
| `9759f1e0` | Open Fix 38: send the shown colony's product cost and turns ("CPRD") |
| `67d3c9fe` | Open Fix 39: send the build popup's queue under edit and its modes ("BLDQ") |
| `380daf98` | Open Fix 40: send the build popup's two lists and its queue with their numbers ("BLDL") |
| `385d678a` | Open Fix 41: keep the engine's own window hidden from the start |
| `1e608dfb` | Open Fix 44: send the Ship Designer's design as it is being edited ("DSGN") |
| `1b39a01f` | Open Fix 45: report the Ship Designer's three pickers (54-56) and send what they offer ("DSBX") |
| `6800ee70` | Open Fix 46: report the diplomacy audience as its own screen, 57 the player's and 58 the AI's |
| `2bbc0174` | Open Fix 47: send the diplomacy audience's state, its statement, reply and menu ("DIPL") |
| `1780203a` | Open Fix 43: hide the engine's window only when the starter asks, and show it again on request (amends fix 41) |
| `adc21aed` | Open Fix 48: send the move verdict for the fleet box's selection at every star ("FMOV") |
| `707bcd96` | Open Fix 29: send the generic message box's kind, title, text and answers ("MSGB") |
| `7fbe221f` | Open Fix 49: report the turn-time popups under their own ids and send what they show ("TPOP") |
| `b1b25b88` | Open Fix 50: send the Hall of Fame's entries and report 14 on both ways in ("HOFM") |
| `e2a10520` | Open Fix 51: report which multiplayer step is up and send what it shows ("MPLY") |
| `02fa6bff` | Open Fix 42: tick the extension during the input delay, so the client hears the screen at once |
| `0b15e0b6` | Open Fix 61: a right click at a point |
| `ca441bf9` | Open Fix 52: a tactical battle reports 65 |
| `80484481` | Open Fix 53: the tactical battle's state on the wire ("CMBT") |
| `7190eb2d` | Open Fix 55: the ordnance in flight on the wire ("CMSL") |
| `9dc9601c` | Open Fix 57: how the last battle ended, kept until the next ("CRES") |
| `31f0aed4` | Open Fix 54: what the acting unit's weapons can hit now, the engine's own verdict ("CTGT") |
| `9b7f61a4` | Open Fix 59: the galaxy map fleet box's scroll row on the wire ("FBSC") |
| `774f8267` | Open Fix 56: the battle's events on the wire, numbered, drained into the next snapshot ("CMEV") |
| `feeec356` | Open Fix 58: one command per battle action, consumed where the human's input is read (MSG_COMBAT_COMMAND) |
| `4417efd2` | Open Fix 62: an activation checked against the field the client saw |
| `adfd7bb0` | Open Fix 63: CMEV reports a missile merge and a missile gone only for a live missile — the original's merge loop goes on matching empty s… |
| `0d2edae6` | Open Fix 65: the original's view kept round a commanded battle action — centred with the game's own Snap_Center_Combat_Screen_, a move lo… |
| `7a561725` | Open Fix 64: MSG_SET_SPIES — the Races screen's spies and missions set as one list through the game's own icon groups and setters, all or… |
| `55362111` | Open Fix 66: CPOP — the battle's scan view and board popup on the wire |
| `998d17e3` | Open Fix 67: MSGB version 2 — the values of the message box text's FMTPARA item codes, read through Get_Item_Value_ while the box is up |
| `f0acb73d` | Open Fix 68: COPT — the battle's OPTIONS panel on the wire |
| `ebdf5e69` | Open Fix 69: HLPL — the help list a right click is checked against on the wire |
| `624081b7` | Open Fix 70: the player's language |
| `3a015989` | Open Fix 74: the Energy Absorber's discharge as a battle event |
| `6f76077b` | Open Fix 79: the battle's sounds as events |
| `670bd95c` | Open Fix 71: a hidden battle's animation waits skipped while a client draws it |
| `91501ca3` | Open Fix 77: the fighter pass's beam and group as a battle event |
| `36f080e7` | Open Fix 71, amended: a battle command taken leaves no input poll behind, so its action's first wait is skipped too |
| `5a97facc` | Open Fix 82: a beam's aim as a battle event |
| `3d3c74b6` | Open Fix 83: the gyro destabilizer's spin as a battle event |
| `e9b3cf07` | Open Fix 84: the field list is sent when its content changes, not only its count |
| `b73185d2` | Open Fix 85: the refit ship list's first row on the wire |
| `e7e823c8` | Open Fix 86: the fleet box's order buttons on the wire |
| `b5692057` | Open Fix 87: the name box on the wire |
| `e3ae40c8` | Open Fix 88: the text fields on the wire |
| `dd384efa` | Open Fix 89: a raw key code from the client |
| `41c73182` | Open Fix 90: the star system window on the wire |
| `adbb683c` | Open Fix 91: the invasion on the wire |
| `096c97fb` | Open Fix 92: colonists of the colony screen's colony to another colony at the same star |
| `e3d81221` | Open Fix 93: the LAYOUT message — every member of the records the wire sends raw, by name, offset and size |
| `42f32654` | Open Fix 94: the Energy Absorber's charge as a battle event |
| `105c4054` | Open Fix 94, amended: the comments say where a side's charges are emptied |
| `cb2bf131` | Open Fix 95: the Stellar Converter's two totals as a battle event |

---

# orion2re

This project is a restoration effort for the Master of Orion II game engine.

All rights to Master of Orion II, its assets, trademarks, and related intellectual property belong to their corresponding owners.

This repository does not include the original game data. To play, you must own a legitimate copy of Master of Orion II and provide the original game files from a supported release such as GOG or Steam.

## License

Original orion2re code and documentation are free software licensed under
the GNU General Public License, version 3 or any later version. See
[`LICENSE`](LICENSE) for the full license text.

This license does not apply to Master of Orion II, its assets, trademarks,
or other intellectual property. Third-party components retain their own
licenses and copyright notices in their source files and vendor directories.

## AI-assisted development

This project was built with assistance from AI agents operating under human direction. Human contributors wrote code, reviewed, tested, and approved AI-assisted changes, and remain responsible for the final work.

## Requirements

- CMake 3.20 or newer
- Ninja
- Either:
  - macOS build environment with an arm64-capable Clang toolchain
  - Windows build environment with an MSVC toolchain
  - Linux build environment with optional MinGW-w64 cross-compilers
- Original Master of Orion II game files from GOG or Steam

Supported build paths:

- Native macOS arm64 builds
- Native Linux builds
- Native Windows builds with MSVC for x86 and x64
- Linux-hosted MinGW-w64 cross-builds for Win32 and Win64

macOS builds use the native host architecture.

## Vendored submodules

The project uses vendored submodules for third-party dependencies.

Clone the repository with submodules:

```sh
git clone --recurse-submodules <repo-url>
```

If you already cloned the repository without submodules, initialize and update them with:

```sh
git submodule update --init --recursive
```

## Build

Configure and build with a preset:

```sh
cmake --preset win32-debug
cmake --build --preset win32-debug
```

Native macOS example:

```sh
cmake --preset osx-debug
cmake --build --preset osx-debug
```

Router-only Linux example:

```sh
cmake --preset linux-router
cmake --build --preset linux-router
```

This skips the `orion2re` game target and is the preferred path when you only need `orion2re-router`, especially on a headless Linux machine. Prefer `-DORION2RE_BUILD_GAME=OFF` for router-only work.

Tests are opt-in and are not built by default. To configure, build, and run the current unit tests:

```sh
tools/run-tests.sh
```

For the full preset list and per-platform details, see `CMakePresets.json`.

Supported source-owned mods are stored under `data/mods/`. Normal game builds package `data/base/` as `data/base.zip`, package each source-owned mod as `data/mods/<package>.zip`, and deploy all archives when `ORION2RE_GAME_DIR` is configured.

Build only the distributable mod ZIP archives with:

```sh
cmake --build --preset win32-debug --target package-supported-mods
```

Archives are written under the build tree at `data/mods/<package>.zip`. To deploy only the mods to a configured target game directory, use:

```sh
cmake --build --preset win32-debug --target deploy-supported-mods
```

The game does not enable deployed mods automatically. Add `enable <package>;` to
`data/orion2re.cfg`, for example `enable v150;` for standard 1.50, `enable v150i;` for the
single-player 1.50 Improved profile, or `enable v150i-mp;` for its multiplayer overrides.

Random-sequence mismatches log every recorded original caller and captured local caller, including absolute addresses and executable-relative RVAs for symbol lookup.

Example release build:

```sh
cmake --preset win32-release
cmake --build --preset win32-release
```

## Cross-compiling Windows builds from Linux

Install MinGW-w64 cross-compilers and Ninja first. On Debian or Ubuntu, the required packages are typically:

```sh
sudo apt install gcc-mingw-w64 g++-mingw-w64 binutils-mingw-w64 ninja-build
```

Then configure and build one of the MinGW presets:

```sh
cmake --preset mingw-win32-debug
cmake --build --preset mingw-win32-debug
```

or:

```sh
cmake --preset mingw-win64-release
cmake --build --preset mingw-win64-release
```

See `CMakePresets.json` for the available MinGW presets.
