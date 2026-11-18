# orion2re for OrionLayer

This is [orion2re](https://github.com/orion2re/orion2re) **v2.0.1**, with
upstream's `main` of 17 November 2026 merged in, and the
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

Linux — on Debian or Ubuntu the build tools and what SDL3 (vendored in
`vendors/SDL`) needs to build, as SDL's own `docs/README-linux.md` lists
them:

```sh
sudo apt install build-essential cmake ninja-build pkg-config git \
    python3-venv xdotool libasound2-dev libpulse-dev libaudio-dev \
    libfribidi-dev libjack-dev libsndio-dev libx11-dev libxext-dev \
    libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev \
    libxtst-dev libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev \
    libgles2-mesa-dev libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev \
    libudev-dev libthai-dev libusb-1.0-0-dev libpipewire-0.3-dev \
    libwayland-dev libdecor-0-dev liburing-dev
```

(`python3-venv` and `xdotool` are OrionLayer's; a player follows
OrionLayer's README, which carries all of this.)


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
"Normalize tracked text to LF", which the ports need). Upstream's v2.0.1 and
its `main` of 17 November 2026 are merged into the branch as two merge
commits (their own changes are not listed here; where upstream moved code, the
hooks below were put back in the new place); each commit's message
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
| `c255dc67` | Open Fix 95, amended: the comments name the second total as the shields' share |
| `be79dadf` | Open Fix 96: the galactic council on the wire |
| `360b8d0c` | Open Fix 97: the Planets screen's state on the wire |
| `2a09cb76` | Open Fix 98: the colony screen's troop popup on the wire |
| `29047f2d` | Open Fix 99: a bitmap line too short for its picture is not drawn (a SIGFPE on a clipped tractor beam) |
| `35486477` | Open Fix 101: a combat command reaches the original's refusal boxes (BOARD at an own unit, FIRE at a phased enemy) |
| `f93344f6` | Open Fix 102: the galaxy map's merge-relocation mode on the wire |
| `96c39753` | Open Fix 103: colonists to any colony of the player — the transport to another star, the Colonies list's move, the original's questions (MSG_SETTLE_POPS long form, STLR) |
| `a9a1f725` | Open Fix 103, amended: the long form refuses an empty colony slot |
| `4b77ed0b` | Open Fix 104: the slider popup on the wire (the freighter scrap's count, the leader cheat; TPOP kind 20, id 80) |
| `435627ff` | Open Fix 104, amended: the comments cite the slider's steps in the patched tree |
| `29c61acd` | Open Fix 105: the Antaran room on the wire (its ANTARMSG message; TPOP kind 22, id 82) |
| `c7205769` | Open Fix 105, amended: the comments say the message set follows the Antarans' invasion stardate |
| `86842dfe` | Open Fix 106: the ship detail view on the wire outside a battle (id 81, SHPV: the view's ship, fields, name and derived combat record) |
| `ec66171a` | Open Fix 107: the evolutionary mutation on the wire (TPOP kind 23, id 83: the picks, the selections and the field ids, re-read every snapshot) |
| `39c5cc6d` | Open Fix 108: the Space Dragon's bribe on the wire (TPOP kind 24, id 84: the star, the player, the bribe, the mode, the two buttons, the message, and in mode 1 every star's field of the popup's galaxy map) |
| `90656654` | Open Fix 109: the diplomacy system picker on the wire (TPOP kind 25, id 85: the giver, the receiver, offer or demand, CANCEL, the ambassador, every star's field, and per system of the giver's what its hover shows) |
| `c024495c` | Open Fix 110: the Colonies list's numbers on the wire (CSUM on screen 20: the list window's first row, and per colony of the game's list the product, its cost, the cost to buy, the turns and whether it can be bought; the ten BUY fields) |
| `da87ff51` | Open Fix 111: the colony screen's demolish addressed by id (DEMOLISH 0x8C, CAP_DEMOLISH: a colony and a building start the original's own click on it, with its refusals, its confirmation box and its refund) |
| `ca4ea2e5` | Open Fix 112: the films on the wire, and their skip (id 86 and FILM while SMACK::Play_Cinematic plays: the file name, the frame, the total; a throttled tick in its frame loop sends the picture and lets an injected click or key end the film by the original's own test) |
| `3096e0ee` | Open Fix 112, amended: the right button ends a film as the left does (its release held until the film ends); FILM's frame named as the frames drawn so far |
| `f6177a05` | Open Fix 111, amended: refused while a box, popup, list or film runs over the colony screen, or when the field is no longer the colony screen's; the arm dropped by the next Tick; the comments name the satellites, the Capitol and the buildings with no field |
| `5a09e0ba` | Open Fix 115: the Turn Summary's messages carry their engine type (the TPOP block of kind 2 is version 2: per message the MSG_TYPE_*, the MSG_COLONY_SUBTYPE_* or 255, a colony message's planet or -1, and two numbers: a scrapped building's id and refund, a finished product's id and 0) |
| `72959b62` | Open Fix 114: the map's two event banners on the wire (the block "EVTB" on every screen: a version byte, the warp beast's event active, the hyperspace flux's event active, as `EVENTS::Event_Check_*` answer them) |
| `fff33608` | Open Fix 118: the auto-turn banner on the wire (the block "AUTO" while the chain of turns that run by themselves is armed: `in_auto_next_turn_mode`, the whole-screen exit field's index, the box Do_Begin_Of_Turn drew, its text "<race> stardate X.Y") |
| `9557b23f` | Open Fix 119: the multiplayer bar's lights on the wire (the block "PLRL" in a hotseat or network game: the game type, whether the lights are bright, the diplomacy request mask, and per player whether done and the colour) |
| `1d308edc` | Open Fix 120: the end-of-game score screen on the wire (id 87 and the block "SCOR" while `Hi_Score_Screen` takes input: the nine scores, the subtotal, the modifier, the total, each line as the game words it, the defeated players and the field that ends it) |
| `9e436604` | Open Fix 121: the Fleets screen's move preview on the wire (the block "FMOV" on SCREEN_FLEET too: the verdict `Ships_Try_To_Move_To` gives the selected big icons' ships at every star, or no star at all when nothing is selected or in RELOCATE mode) |
| `62af0dac` | Open Fix 122: the reply to a human player's diplomacy request on the wire (id 88 and TPOP kind 26 while `Do_Reply_To_Diplomacy_Request` takes input: the requester, the accept, ignore and wait fields, the requester's colour and the message the game prints) |

---

# orion2re

This project is a restoration effort for the Master of Orion II game engine. Check our [documentation portal](https://moo2mod.com/redone/doc/) too.

The game is fully playable and includes all bug fixes and optional community improvements up to the latest version of the unofficial 1.50 patch.

All rights to Master of Orion II, its assets, trademarks, and related intellectual property belong to their corresponding owners.

This repository does not include the original game data. To play, you must own a legitimate copy of Master of Orion II and provide the original game files from a supported release such as GOG or Steam.

## Download and play

To play, download a prebuilt ZIP. You do not need CMake, Ninja, or a compiler.

1. Install your copy of Master of Orion II from GOG or Steam.
2. Open [GitHub Releases](https://github.com/orion2re/orion2re/releases) and expand
   Assets for the release you want. Download the game ZIP for your platform:
   `orion2re-win-win32-<tag>.zip` (Windows 32-bit),
   `orion2re-win-win64-<tag>.zip` (Windows 64-bit), or
   `orion2re-osx-arm64-<tag>.zip` (macOS on Apple Silicon).
   The Source code (zip) and Source code (tar.gz) downloads are for building the
   game yourself, not for installing it.
3. If you're updating, back up `data/orion2re.cfg` first. Release ZIPs include
   the default configuration, which will replace your custom settings.
4. Extract the ZIP's contents into your existing Master of Orion II game folder,
   alongside the original game files, merging the included `data/` folder.
   Packages include runtime data but no commercial game files.
5. Launch `orion2re.exe` on Windows or `Orion2 Redone.app` on macOS
   from that folder, rather than the original game's launcher.

On macOS, use Archive Utility or `ditto` to extract the ZIP, and keep the app,
executable, and `data/` folder together so the app can find the game. The app is
not signed or notarized by Apple, so you may need to allow it in
System Settings / Privacy & Security.

Linux is supported, but we don't currently provide prebuilt Linux packages.
To run the game natively on Linux, you'll need to [build it from source](#build).
The ZIP instructions above are for Windows and macOS.

### Development builds

To try a development build, select a successful `main` run under
[Actions / CI and packages](https://github.com/orion2re/orion2re/actions/workflows/ci.yml)
and download the artifact for your platform. GitHub requires you to sign in to
download Actions artifacts, even from public repositories. Release downloads
don't require sign-in. Development downloads are kept for 30 days.

The artifact is a ready-to-use ZIP, such as `orion2re-win-win32-main.zip`,
with the executable and `data/` folder directly inside it.
The workflow run shows which commit it was built from.

Install these ZIPs in the same way as releases. Development (`main`) packages omit
`data/orion2re.cfg`, so your configuration survives intermediate updates.

On all platforms, the game uses the executable's directory for data, configuration,
and save files, regardless of the launcher's working directory. Linux desktop
launchers do not need a `Path=` entry pointing to the installation directory.

## License

Original orion2re code and documentation are free software licensed under
the GNU General Public License, version 3 or any later version. See
[`LICENSE`](LICENSE) for the full license text.

This license does not apply to Master of Orion II, its assets, trademarks,
or other intellectual property. Third-party components retain their own
licenses and copyright notices in their source files and vendor directories.

## AI-assisted development

This project was built with assistance from AI agents operating under human direction. Human contributors wrote code, reviewed, tested, and approved AI-assisted changes, and remain responsible for the final work.

## Build

These steps build the game from source. The examples use native Linux, with
Windows and macOS alternatives below. To play, you'll also need the original
Master of Orion II game files from your GOG or Steam installation.

### 1. Install build dependencies

You need Git, CMake 3.21 or newer for the presets used below, Ninja, and a C/C++
compiler with C++20 support. These are separate tools, not included in the game
download. Python 3 is also used for the build-tool and packaging regression tests.

#### Linux (Ubuntu, Xubuntu, or Debian)

Open a terminal and install the tools and development libraries used by our Linux CI:

```sh
sudo apt update
sudo apt install git build-essential cmake ninja-build pkg-config python3 \
  libasound2-dev libpulse-dev libaudio-dev libjack-dev libsndio-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
  libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
  libwayland-dev libdecor-0-dev libdrm-dev libgbm-dev \
  libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev \
  libudev-dev libdbus-1-dev libibus-1.0-dev libpipewire-0.3-dev
```

`build-essential` includes GCC and G++. Check `cmake --version` before continuing;
if your distribution provides an older version, install a newer one from
[CMake](https://cmake.org/download/).

#### macOS (Apple Silicon)

Install Apple's command-line tools for the Clang compiler and macOS SDK:

```sh
xcode-select --install
```

Then, with [Homebrew](https://brew.sh/) installed, install the remaining tools:

```sh
brew install git cmake ninja python
```

#### Windows (Visual Studio)

Install Visual Studio 2022 or its Build Tools with the Desktop development with
C++ workload, including MSVC, a Windows SDK, and C++ CMake tools for Windows
(CMake and Ninja). Also install [Git for Windows](https://git-scm.com/downloads/win)
and [Python 3](https://www.python.org/downloads/windows/) and add them to `PATH`.
For the sanitizer tests below, include the MSVC AddressSanitizer component.

Run the following steps in an x64 Native Tools Command Prompt for VS 2022 when
using `win64-*` presets. For `win32-*`, use an x86 Native Tools Command Prompt
instead. The selected prompt sets up the compiler for the matching architecture.

### 2. Clone the source and vendored submodules

The project uses Git submodules for third-party libraries such as SDL. Clone
them along with the project:

```sh
git clone --recurse-submodules https://github.com/orion2re/orion2re.git
cd orion2re
```

If you already cloned the repository, open a terminal in its directory and run:

```sh
git submodule update --init --recursive
```

Run all remaining commands from this `orion2re` directory, which contains
`CMakeLists.txt` and `CMakePresets.json`. GitHub's source-code ZIP does not include
the submodules, so use a Git clone for these instructions.

### 3. Build release or debug

For a Linux release build, replace the path below with the absolute path to your
existing Master of Orion II installation. Choose a folder you can write to.

```sh
cmake --preset linux-release -DORION2RE_GAME_DIR="/absolute/path/to/Master of Orion II"
cmake --build --preset linux-release --parallel
```

The first command configures the build. The second compiles the game and copies
the executable and runtime data into your game folder. Existing
`data/orion2re.cfg` files are preserved; a default is copied only if one is missing.
Start the Linux game from that folder:

```sh
cd "/absolute/path/to/Master of Orion II"
./orion2re
```

For a debug build, return to the source directory and use:

```sh
cmake --preset linux-debug -DORION2RE_GAME_DIR="/absolute/path/to/Master of Orion II"
cmake --build --preset linux-debug --parallel
```

Release builds are optimized for playing; debug builds are useful for diagnosing
problems. They have separate build directories under `out/build/Linux/`, but
building either with the same game directory replaces the executable there.

For other platforms, replace the preset in both the configure and build commands:

| Platform | Release preset | Debug preset |
| --- | --- | --- |
| Linux | `linux-release` | `linux-debug` |
| macOS | `osx-release` | `osx-debug` |
| Windows x64 | `win64-release` | `win64-debug` |
| Windows x86 | `win32-release` | `win32-debug` |

Use a game directory appropriate for that platform, such as
`-DORION2RE_GAME_DIR="C:/Games/Master of Orion II"` on Windows. Launch
`orion2re.exe` on Windows or `Orion2 Redone.app` on macOS after a release build.
macOS debug builds use the `orion2re` executable directly.

For additional profiles, including sanitizer and router-only builds, see
[`CMakePresets.json`](CMakePresets.json). The `linux-router` preset builds only
`orion2re-router`, not the game.

#### Runtime data and mods

Normal game builds package `data/base/` as `data/base.zip` and each supported mod
under `data/mods/` as a ZIP, then copy them into `ORION2RE_GAME_DIR` when it is set.
Without that option, the executable and packaged data stay in the build directory,
such as `out/build/Linux/linux-release/`.

To build only the mod archives, or copy them into the configured game directory:

```sh
cmake --build --preset linux-release --target package-supported-mods
cmake --build --preset linux-release --target deploy-supported-mods
```

The second command requires `ORION2RE_GAME_DIR` to be set. Mod archives are written
under the build directory at `data/mods/<package>.zip`.

Mods are not enabled automatically. Put personal settings and enabled mods in
`data/user.cfg`, which is loaded after `data/orion2re.cfg`. For example, add
`enable v150;` for standard 1.50, `enable v150i;` for the single-player 1.50
Improved profile, or `enable v150i-mp;` for its multiplayer overrides.
See [Configuration](docs/content/manual/legacy-modding/configuration.md).

### 4. Run tests

Tests are opt-in and are not built by the normal release and debug presets.
From the source directory on Linux or macOS, configure, build, and run the unit
tests with:

```sh
tools/run-tests.sh
```

This uses a separate debug build in `out/build/test-debug/` and reports failures
through CTest, CMake's test runner.
macOS test builds require a Debug configuration with ASan and UBSan;
explicitly disabling sanitizers is rejected.

To run the sanitizer-enabled tests used by CI on Linux:

```sh
cmake --preset linux-debug-asan-tests
cmake --build --preset linux-debug-asan-tests --parallel
ctest --preset linux-debug-asan-tests
```

On macOS, use `osx-debug-asan-tests` in all three commands. On Windows, use
`win64-debug-asan-tests` from the x64 Native Tools Command Prompt. Linux and
macOS run AddressSanitizer and UndefinedBehaviorSanitizer; Windows runs
AddressSanitizer only.

The Python build-tool and packaging regression tests run separately:

```sh
python3 -m unittest discover -s tests/build_tools -v
python3 -m unittest discover -s tests/packaging -v
```

On Windows, use `python` instead of `python3`.
