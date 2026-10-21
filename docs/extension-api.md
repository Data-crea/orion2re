# The Extension API

The Extension API lets a separate program — a frontend such as OrionLayer —
read the running game's state and send it input, over a local TCP socket.
It is built only when the build is configured with `-DORION2RE_EXT=ON`;
without it, not one line of it is compiled.

- Port: `localhost:17362`, TCP, no encryption. Only local clients.
- Byte order: little-endian everywhere.
- Protocol version: **1** (`PROTO_VERSION`). Capabilities beyond it are
  announced in `HELLO_REPLY` (see below), so an older client keeps working
  with a newer engine and the other way round.

The code is `src/ext/`: `ext_server.*` (the socket and the frames),
`ext_api.*` (what the snapshot holds and how input reaches the game) and
`ext_layout.*` (the layout of the records, see "LAYOUT"). The game calls
three functions:

```cpp
namespace ext {
    bool Init(uint16_t port = 17362);   // start the server
    void Tick(int16_t current_screen);  // send state, apply queued input
    void Shutdown();                    // stop the server
}
```

`Tick` runs from the game's input loop. When no client is connected it
returns at once; nothing is serialised.

## Frames

Every message is one frame:

| offset | type   | field                     |
|-------:|--------|---------------------------|
| 0      | uint32 | magic `0x4F325845` ("O2XE") |
| 4      | uint32 | length of what follows (8 + payload) |
| 8      | uint16 | message type              |
| 10     | uint16 | flags (0)                 |
| 12     | uint32 | sequence number           |
| 16     | ...    | payload                   |

## Connecting

1. The client sends `HELLO` (0x01): `uint16 version, uint16 subscriptions`.
   Subscriptions are a bit mask: `0x01` state, `0x02` field lists,
   `0x04` the 640x480 picture and palette, `0x08` events.
2. The server answers `HELLO_REPLY` (0x01): `uint16 protocol version,
   uint16 capabilities`. Capability bit `0x0001` (`CAP_LAYOUT`) means a
   `LAYOUT` message follows at once; bit `0x0002` (`CAP_DEMOLISH`) means
   the server takes `DEMOLISH`.
3. The server sends `LAYOUT` (0x15), then the current field list, and from
   then on whatever the client subscribed to.

## LAYOUT — the records, member by member

The snapshot carries the game's own records as raw bytes (the players, the
stars, the ships, the colonies, ...). Their layout belongs to the engine
and changes when the engine changes: orion2re 2.0 grew `s_player` from
3,854 to 11,654 bytes. A client that read fixed offsets would break on
every such change. So the server describes every record it sends raw, once
per connection, and a client looks each member up **by name**:

```
uint16 layout version (1)
uint16 record count
per record:
    str8   name              e.g. "s_player"
    uint32 size              sizeof
    uint16 member count
    per member:
        str8   name          e.g. "total_pop"
        uint32 offset        offsetof
        uint32 size          sizeof the whole member (an array: all of it)
        uint32 element size  sizeof one element (an array's innermost)
        uint8  kind          0 signed, 1 unsigned, 2 text (char),
                             3 record, 4 other (a pointer, a float)
        str8   record name   for kind 3, the member's record type; else empty
uint16 constant count
per constant: str8 name, int32 value
```

(`str8`: a uint8 length, then that many ASCII bytes.)

The records are those the wire copies whole — `s_settings`, `s_player`,
`s_star_data`, `s_ship_data`, `s_colony`, `s_planet_data`, `s_nebula`,
`s_leader_data`, `s_antaran`, `s_ship_icon`, `s_combat_data`,
`s_missile`, `s_ship_move_info` — and every record type they contain.
The constants are `MAX_PLAYERS`, `MAX_LEADERS`, `MAX_STARS`, `MAX_SHIPS`,
`MAX_PLANETS` and `MAX_NEBULAS`.

**What this promises.** When a record grows, shrinks or moves a member,
nothing changes on either side: the compiler computes the offsets
(`offsetof`, `sizeof`) and the client reads them. A client changes only
when a member it uses is removed or renamed.

**How it is built.** `tools/ext_layout_gen.py` reads `src/game/orion2.h`
and writes the member list, `src/ext/ext_layout_table.inc`; the offsets
and sizes in it are the compiler's. After a record changes in the header,
run the script (`--check` says whether the table is current). A member
that is removed or renamed stops the build at its line in the table — the
one place that has to follow.

## The snapshot (`STATE`, 0x10)

In this order:

| type | field |
|---|---|
| int16 | current screen |
| int8 | previous screen |
| int32 | stardate (tenths) |
| int16 | player number, number of players, stars, ships, colonies |
| uint8, int8 | number of nebulas, game type |
| int16 | map scale, map x, map y, `MAP_MAX_X`, `MAP_MAX_Y` |
| `s_settings` | the settings |
| `s_player` x `MAX_PLAYERS` | the players |
| int16 n, `s_star_data` x n | the stars |
| int16 n, `s_ship_data` x n | the ships |
| int16 n, `s_colony` x n | the colonies |
| int16 n, `s_planet_data` x n | the planets (five per star) |
| uint8 n, `s_nebula` x n | the nebulas |
| `s_leader_data` x `MAX_LEADERS` | the leaders |
| `s_antaran` | the Antarans |
| int16 n, `s_ship_icon` x n | the map's ship icons |
| int16 x 8 | the New Game screen's eight settings |

Then optional blocks, each starting with a four-letter tag, written only
while what they describe is on screen (see "OrionLayer's changes" below
for each). A client reads a block whole or not at all and skips a tag it
does not know only by stopping there: blocks are in a fixed order.

## Field list (`FIELDS`, 0x11)

`uint16 count`, then per field 13 bytes: `int16 index, x, y, x_end, y_end,
field type`, `uint8 hot key`. Sent when the list's count, its contents,
the screen or a client's connection changed. Slot 0 is never a real field.
orion2re 2.0 stores one-letter hot keys in lower case.

**Never remember an index.** The same index is a different field in every
other list — orion2re 2.0 adds five cheat hot keys before the galaxy map's
buttons. Find a field in the list you just received, by its type and
rectangle, and send its index at once, or send nothing.

## Picture (`VISUAL`, 0x12)

307,200 bytes (640x480, one palette index per pixel), then 768 bytes of
palette (256 x RGB).

## Events (`EVENT`, 0x13)

`uint16 flags, int16 screen`: `0x01` screen changed, `0x02` turn ended,
`0x04` game started, `0x08` game ended.

## Input (client to server)

| type | payload | what it does |
|---|---|---|
| `ACTIVATE` 0x80 | int16 field index [, int16 x, y, x_end, y_end] | activates a field as if clicked; with the rectangle, only if the field there still is the one the client saw |
| `INJECT_KEY` 0x81 | int16 key | a key press |
| `INJECT_CLICK` 0x82 | int16 x, y | a left click at a point of the 640x480 screen; while a film plays, the release follows when the film ends |
| `CANCEL_FIELD` 0x83 | int16 field index | the right-button form of `ACTIVATE` |
| `SET_JOBS` 0x84 | int16 colony, uint8 n, n x (uint8 pop, uint8 job) | sets colonists' jobs in one colony |
| `SELECT_SHIP` 0x85 | int16 ship, uint8 selected | selects a ship in the open fleet box |
| `SHOW_WINDOW` 0x86 | uint8 show | shows or hides the engine's own window |
| `INJECT_RIGHT_CLICK` 0x87 | int16 x, y | a right click at a point |
| `COMBAT_COMMAND` 0x88 | uint16 serial, int16 unit, uint8 op, int16 a, b, c | one battle action |
| `SET_SPIES` 0x89 | uint8 n, n x (player, spies, mission), uint8 pool | the Races screen's spies |
| `INJECT_RAW_KEY` 0x8A | uint16 code | F1-F10, Alt+F1-F10, Alt+letter |
| `SETTLE_POPS` 0x8B | int16 from, int16 to, uint8 n, n x pop | colonists to another colony at the same star |
| `DEMOLISH` 0x8C | int16 colony, int16 building | the colony screen's demolish of one building: the original's own click on it, its refusals, confirmation box and refund |

An input is applied the next time the game reads its input, never in the
middle of its logic. The commands that change game data directly
(`SET_JOBS`, `SELECT_SHIP`, `SET_SPIES`, `SETTLE_POPS`, `COMBAT_COMMAND`, `DEMOLISH`)
check the game's own conditions first and write nothing when one fails.

## Environment variables

| variable | effect |
|---|---|
| `ORION2RE_HIDE_WINDOW=1` | the engine's own window starts hidden (a frontend draws instead); `SHOW_WINDOW` shows it |
| `ORION2RE_NO_VSYNC=1` | present without waiting for VSync |
| `ORION2RE_LANGUAGE` | the language the game's texts are in |

## Versioning

- `PROTO_VERSION` changes only if a client of version 1 could no longer
  read the frames. New messages and new capability bits do not change it.
- `LAYOUT` carries its own version; it changes only if the message's own
  format changes — not when a record changes.
- A block's tag and its first byte (where it has one) carry the block's
  version.

## OrionLayer's changes

This fork carries, one commit each, what OrionLayer needed from the engine.
Each commit's message says what it changes and why; each place it changes
carries a one-line comment `OrionLayer, open fix N.` The list, with the
commit subjects, is in the README.
