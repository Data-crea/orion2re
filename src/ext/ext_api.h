#pragma once
// orion2re Extension API
// Public interface. Only ext_api.cpp includes game headers.

#include <atomic>  // OrionLayer, open fix 43. The window flag is shared by two threads.
#include <cstdint>

namespace ext {

/// Initialize the extension server. Call once at startup.
/// Returns true on success.
bool Init(uint16_t port = 17362);

/// Called from Screen_Control() on every screen dispatch.
/// Serializes state, sends to clients, reads input queue.
void Tick(int16_t current_screen);

/// Shut down the server and close all client connections.
void Shutdown();

/// If true, suppress SDL_ShowWindow (headless mode).
/// OrionLayer, open fix 43. Set at start from ORION2RE_HIDE_WINDOW, and by a client (MSG_SHOW_WINDOW) from the game thread; read by the main thread, which owns the window.
extern std::atomic<bool> g_hide_window;

/// Pending field activation from external client.
/// Set by ProcessInput(), consumed by Get_Input() early return.
extern int16_t g_pending_field;

/// The screen id REPORTED to clients, or -1 for the game's own.
///
/// OrionLayer needs the two turn-start research dialogs — the science
/// room and SELECT NEW RESEARCH — to be distinguishable on the wire; both
/// run under SCREEN_MAIN and the game draws them from that value, so
/// writing MOX::current_screen (the path the screen-id patch takes for
/// race selection) would MOVE them: textbox.cpp:44 picks the description
/// box's x from it and textbox.cpp:284 its colour group. This changes what
/// is SENT and nothing the game reads.
///
/// OrionLayer, open fix 45. The Ship Designer's three sub-dialogs (desbox.cpp) run
/// under SCREEN_DESIGN as well and report 54 (shield or computer), 55
/// (weapon) and 56 (special system), so a client can tell which is open.
extern int16_t g_screen_override;

/// Sets `g_screen_override` while it lives and restores the previous value
/// when it goes out of scope — so a function with several returns needs one
/// line. An id below 0 means "no override" and the guard does nothing.
struct ScreenOverride {
    int16_t previous;
    explicit ScreenOverride(int16_t id);
    ~ScreenOverride();
};

/// OrionLayer, open fix 46. The diplomacy audience has no screen id and runs under its
/// caller's (6 the Races screen, 0 a turn-start report, 12 a sneak attack
/// in turn processing): it reports 57 when the player opened it
/// (DIP_SCRN::Diplomacy_Screen) and 58 when an AI asked for it
/// (DIP_SCRN::Npc_Diplomacy_Screen), so a client can tell it is up.

/// OrionLayer, open fix 47. The list fields::Get_List_Field is running: its title,
/// its items (item_size apart), their count and their enable flags exist
/// only as that call's arguments — the diplomacy audience's menus are such
/// lists, and a client cannot tell them apart by their fields. Set by
/// ListFieldGuard while the list takes input, restored when it returns;
/// read by SerializeState. What is SENT, nothing the game reads.
struct ListFieldView {
    const char* title;
    const char* items;
    int32_t item_size;
    int16_t count;
    const uint8_t* flags;
};
extern ListFieldView g_list_field;

struct ListFieldGuard {
    ListFieldView previous;
    ListFieldGuard(const char* title, const char* items, int32_t item_size,
                   int16_t count, const uint8_t* flags);
    ~ListFieldGuard();
};

/// OrionLayer, open fix 50. True while SCORE::Hall_Of_Fame_Screen takes input:
/// set once it has read the file and sorted it, cleared when it leaves —
/// SCORE::hi outlives the screen and the first tick at 14 comes before the
/// file is read, so the pointer alone cannot say the table is current.
extern bool g_hof_live;

/// OrionLayer, open fix 52. A tactical battle: its own id and the gate every combat block is written under.
///
/// Nothing in cmbt*/comb*/combat* writes MOX::current_screen, so a client
/// saw 12 (37 in a network game) for the whole battle and could not tell it
/// from turn processing. CombatGuard lives in COMBINIT::Tactical_Combat from
/// the ships' deployment to the end of the turn loop (before End_Of_Combat
/// writes the losses back): it reports SCREEN_EXT_COMBAT, sets g_combat_live
/// and counts the battle in g_combat_serial. The two modals the battle opens
/// report their own ids while it is live — the scan view
/// (CMBTDRW1::Detailed_View_Ship, also opened from the fleet screen, the map
/// and the Leaders screen, which keep theirs) and the board popup
/// (CMBTDRW1::Board_Popup). What is SENT, nothing the game reads.
constexpr int16_t SCREEN_EXT_COMBAT = 65;
constexpr int16_t SCREEN_EXT_COMBAT_SCAN = 66;
constexpr int16_t SCREEN_EXT_COMBAT_BOARD = 67;
extern bool g_combat_live;
extern uint16_t g_combat_serial;

/// OrionLayer, open fix 57. How a battle ended, recorded just before End_Of_Combat writes the losses back.
///
/// The end has no box and no input (combinit.cpp: End_Of_Combat), so a
/// client saw the result only afterwards, as a changed ship[]. Recorded by
/// Record_Combat_Result from COMBINIT::Tactical_Combat with the winner it
/// hands End_Of_Combat (1 attacker, -1 defender — also after the 50-turn
/// timeout — 666 both gone; 0 when the battle never started), and KEPT until
/// the next battle starts: the stardate moves on in the same Next_Turn
/// (nextturn.cpp), possibly before any tick, so "until the turn moves on"
/// could send it never. `number` counts results; a client tells them apart
/// by it.
struct CombatResultUnit {
    int16_t ship_idx;
    int8_t owner;
    uint8_t previous_owner;
    int8_t unit_status;
    int8_t is_retreating;
    int8_t is_captured;
    int16_t structure_damage;
    int16_t armor_remaining;
    int16_t structure_max;
};
struct CombatResultView {
    bool valid;
    uint16_t number;
    uint16_t serial;       // g_combat_serial of the battle; 0 when not fought
    uint8_t fought;
    int16_t winner;
    int16_t turn;
    int16_t star;
    int16_t colony;
    int16_t attacker;
    int16_t defender;
    int16_t colony_pop_lost;
    int32_t stardate;
    int16_t units;
    CombatResultUnit unit[210];
};
extern CombatResultView g_combat_result;
void Record_Combat_Result(int16_t star_idx, int16_t colony_pop_lost,
                          int16_t winner, bool fought);

/// OrionLayer, open fix 56. What happens in a battle, in the order the engine does it.
///
/// The original plays every effect inside a blocking loop and tells nobody;
/// ext::Tick runs only where the battle waits for input or between AI steps,
/// so a client watching snapshots sees almost none of it (work order 193
/// §6: 1 beam picture of 9). Each hook in the combat code calls
/// Combat_Event with values that already exist at that point — it computes
/// nothing, draws no random number and never ticks — and the event goes into
/// a ring the next snapshot drains ("CMEV"), numbered, so a client can play
/// the battle after the fact and see a gap as a gap. Recorded only while a
/// battle is live (open fix 52's gate): the bombardment that reuses
/// Fire_Ship outside a battle (COMBAT::Tactical_Bombardment) records
/// nothing.
enum CombatEventType : uint8_t {
    CEV_BEAM_SHOT = 1,      // source, target, flag (1 missile), slot, weapon, result bits, past shields, absorbed
    CEV_REFLECT = 2,        // reflector, shooter, past shields, absorbed (damage reflected onto the shooter)
    CEV_BOMB = 3,           // source, target, slot, weapon, hits
    CEV_SPECIAL = 4,        // source, target, flag, slot, weapon (the effect is the state that follows)
    CEV_MISSILE_LAUNCH = 5, // missile, source, target, anti-missile, slot, type, quantity, speed
    CEV_MISSILE_MERGE = 6,  // missile, into, quantity after
    CEV_MISSILE_HIT = 7,    // missile, source, target, type, quantity, past shields, absorbed, anti-missile
    CEV_FIGHTER_PASS = 8,   // missile, source, target, type, passes left, past shields, absorbed
    CEV_MISSILE_GONE = 9,   // missile, type, quantity, x, y (every end: hit, shot down, merged, dissipated)
    CEV_MOVE = 10,          // unit, from x, from y, to x, to y, cost, teleport (recorded as the move starts)
    CEV_ROTATE = 11,        // unit, from facing, to facing, step
    CEV_DESTROY = 12,       // unit, death state, status before, owner, previous owner, x px, y px
    CEV_RETREAT = 13,       // unit, x, y, owner, status (5: died while turning away)
    CEV_BLAST_HIT = 14,     // source, unit, blast kind, past shields, absorbed
    CEV_WEB_DAMAGE = 15,    // unit, past shields, absorbed
    CEV_CAPTURE = 16,       // attacker, defender, result (0x32 taken, 0x31 destroyed, 0x33 failed), marines, defenders left, owner after
    CEV_RAID = 17,          // attacker, target, raiders sent, raiders back, defenders left, damage
    CEV_COMMAND = 18,       // op, result (0 taken, else why refused), a, b — OrionLayer, open fix 58.
    CEV_SOUND = 19,         // sound id, handle, ticks since the last other event — OrionLayer, open fix 79.
    CEV_SOUND_CHANGE = 20,  // handle (-1 every sound), change (1 fade, 2 loop, 3 pitch), value, ticks — OrionLayer, open fix 79.
};

/// OrionLayer, open fix 58. One command per battle action (decision 52), consumed where the human's input is read.
///
/// A client that sends MOVE as a click on the battle map depends on the
/// original's view origin and on its cursor image (combat1.cpp: the map
/// branch shifts the cell by `Get_Pointer_Image_Number`), and a right click
/// (FACE) or a missile field outside the original's view cannot be sent at
/// all. A command names the action and its cell or target. It is consumed
/// in COMBAT1::Do_Combat_Turn right after the human's Get_Input: the
/// command's input takes the click's place, and the map and face branches
/// take the command's cell instead of the pointer's — everything after that
/// is the original's own code (the network message, Move_Ship, Fire_Ship,
/// Turn_To_Face, the bookkeeping, the confirm box of a stasis release, the
/// board popup). Checked before: the battle's serial and the acting unit
/// (a stale command is dropped), orders allowed now, and what the click
/// branch itself relies on (a legal cell, a live enemy at its anchor, ...).
/// Taken or refused, CEV_COMMAND says so.
enum CombatCommandOp : uint8_t {
    CCMD_MOVE = 1,          // a, b: the cell (Set_Legal_Moves marked it 2000)
    CCMD_FIRE = 2,          // a: the target unit; b: slot mask, -1 = as switched (single player only)
    CCMD_FIRE_MISSILE = 3,  // a: the missile (its field in the original's list)
    CCMD_FACE = 4,          // a, b: the cell to face
    CCMD_SELECT = 5,        // a: an own unit (not in initiative mode)
    CCMD_REMOVE_STASIS = 6, // a: an enemy in stasis (the original's confirm box follows)
    CCMD_BOARD = 7,         // a: the target (board mode, then the target; the board popup follows)
};
int16_t Take_Combat_Command(int16_t input_val);
void Combat_Command_Cell(int16_t* x, int16_t* y);
void Combat_Event(uint8_t type, int16_t a0 = 0, int16_t a1 = 0, int16_t a2 = 0,
                  int16_t a3 = 0, int16_t a4 = 0, int16_t a5 = 0,
                  int16_t a6 = 0, int16_t a7 = 0);

/// OrionLayer, open fix 79. The battle's sounds as events, the engine silent while a client draws.
///
/// The hidden engine played every battle sound at its own time, before the
/// client's picture of it (work order 210). While a battle is live, the
/// window hidden and a client connected, ERICNET::Mox_Play_Sound records
/// the sound (CEV_SOUND) instead of playing it and hands back a handle of
/// its own (16-127, never a channel); a fade, loop or pitch bend of such a
/// handle is recorded too (CEV_SOUND_CHANGE) and touches no channel. Each
/// carries the 55 ms ticks the engine waited (timer::Release_Time) since the
/// event before it, so the client starts it at that point of its own
/// animation. Only what the original would play is recorded (sound effects
/// on, not super fast); music streams are not touched.
bool Battle_Sound(int16_t sound_id, int32_t* handle);
bool Battle_Sound_Change(int32_t handle, uint8_t change, int32_t value);
void Battle_Wait(int32_t ticks);

struct CombatGuard {
    ScreenOverride screen;
    bool previous;
    CombatGuard();
    ~CombatGuard();
};

/// OrionLayer, open fix 29. The generic message box that is up: which kind,
/// its title and text as the caller handed them (raw, FMTPARA codes and
/// all), the field id of each answer and a timed box's ticks — the text
/// otherwise reaches a client only as pixels. Set by MessageBoxGuard once
/// the box's own fields are added (TEXTBOX::Do_Text_Box,
/// GENDRAW::Message_Box_Exploding, GENDRAW::Confirmation_Box), restored
/// when the box returns, so a box opened over another box shows and then
/// gives back the one below. What is SENT, nothing the game reads.
struct MessageBoxView {
    uint8_t kind;          // 0 none, 1 text, 2 timed text, 3 message, 4 warning, 5 confirmation
    const char* title;     // Do_Text_Box's, or nullptr
    const char* text;
    int16_t field_a;       // the dismiss field, or Yes
    int16_t field_b;       // No, or -1
    int16_t ticks;         // a timed box's input ticks, else 0
};
extern MessageBoxView g_message_box;

struct MessageBoxGuard {
    MessageBoxView previous;
    MessageBoxGuard(uint8_t kind, const char* title, const char* text,
                    int16_t field_a, int16_t field_b, int16_t ticks);
    ~MessageBoxGuard();
};

/// OrionLayer, open fix 49. The turn-time popup that is up: which one (kind),
/// up to 16 numbers it was opened with or keeps (args, -1 = unset; the
/// layout per kind is the table above SerializeState's "TPOP" block in
/// ext_api.cpp), and its title and text where the game formats them into a
/// buffer that outlives the popup's input loop. Set by TurnPopupGuard, which
/// can also report a synthetic screen id while it lives (like
/// ScreenOverride), restored when the popup returns — or earlier, by end(),
/// where the popup opens another dialog on its way out. What is SENT,
/// nothing the game reads.
struct TurnPopupView {
    uint8_t kind;          // 0 none, 1 science room ... 9 colony landing (ext_api.cpp)
    int16_t args[16];
    int16_t n_args;
    const char* text;
    const char* title;
};
extern TurnPopupView g_turn_popup;

struct TurnPopupGuard {
    TurnPopupView previous;
    int16_t previous_screen;
    bool active;
    /// screen_id < 0: keep the reported id as it is.
    TurnPopupGuard(int16_t screen_id, uint8_t kind, const char* title,
                   const char* text);
    ~TurnPopupGuard();
    /// Sets args[i] of the view (and n_args to at least i + 1).
    void arg(int16_t i, int16_t v) const;
    void set_text(const char* text) const;
    /// Restores the view and the reported id now; the destructor then does nothing.
    void end();
};

/// OrionLayer, open fix 51. The multiplayer step that takes input: the
/// screens behind the main menu's Multiplayer button report 15, 16, 17, 21,
/// 22, 37 and 41, and one id covers several steps (15 the setup and its
/// Online dialog, 21 every host step, 22 every join step, 41 the load list
/// and hosting a loaded game). Set by MultiplayerGuard at each step's input
/// loop (the phase table is above SerializeState's "MPLY" block in
/// ext_api.cpp), restored when the step ends — a step opened inside another
/// (the Online dialog, the load list) gives the outer one back. A function
/// with several steps keeps one guard and moves it on with set().
/// What a phase shows is read at send time from where the game keeps it
/// (MOX::mp_screen, NETMOX::net, ...); `fields` and `text` hold what only
/// the step's own locals hold. What is SENT, nothing the game reads.
enum MultiplayerPhase : uint8_t {
    MP_NONE = 0,
    MP_SETUP = 1,               // 15 MULTPLAY::Multi_Player_Screen
    MP_ONLINE_SETUP = 2,        // 15 MULTPLAY::Online_Setup_Screen_
    MP_NET_GAME_NAME = 3,       // 41 MULTPLAY::Change_MP_Game_Name
    MP_LOAD_GAME = 4,           // 41 MULTPLAY::Load_Multi_Player_Game_Screen
    MP_HOTSEAT_SETUP = 5,       // 16 HOTPOP::Hotseat_Screen
    MP_HOTSEAT_SELECT = 6,      // 17 MULTPLAY::Hotseat_Select_Player
    MP_HOST_INIT = 7,           // 21/41 NETSTART::Start_Net_Screen / Load_Net_Screen
    MP_HOST_WAIT_JOINERS = 8,   // 21 Start_Net_Screen
    MP_HOST_WAIT_RACES = 9,     // 21 Start_Net_Screen
    MP_HOST_SENDING = 10,       // 21/41 Start_Net_Screen / Load_Net_Screen
    MP_HOST_PICK_POSITION = 11, // 41 Load_Net_Screen (Choose_Network_Plyrs_Screen)
    MP_HOST_WAIT_LOADED = 12,   // 41 Load_Net_Screen
    MP_JOIN_INIT = 13,          // 22 NETSTART::Join_Net_Screen
    MP_JOIN_GAME_LIST = 14,     // 22 MULTPLAY::Choose_Multi_Network_Game_Screen
    MP_JOIN_WAITING = 15,       // 22 Join_Net_Screen
    MP_JOIN_GENERATING_MAP = 16,// 22 NETSTART::Net_Select_Race
    MP_JOIN_PICK_POSITION = 17, // 22 NETSTART::Net_Pick_Position (Choose_Network_Plyrs_Screen)
    MP_JOIN_GETTING_DATA = 18,  // 22 Net_Pick_Position
    MP_NET_NEXT_TURN = 19,      // 37 MULTPLAY::Chat_Box_Input_Loop (NETTURN, COMBFIND)
};

struct MultiplayerView {
    uint8_t phase;          // MultiplayerPhase
    int16_t fields[3];      // MP_HOTSEAT_SETUP: join, accept, cancel; else -1000
    const char* text;       // MP_NET_GAME_NAME: the prompt; else nullptr
};
extern MultiplayerView g_multiplayer;

struct MultiplayerGuard {
    MultiplayerView previous;
    explicit MultiplayerGuard(uint8_t phase, int16_t field_a = -1000,
                              int16_t field_b = -1000, int16_t field_c = -1000,
                              const char* text = nullptr);
    ~MultiplayerGuard();
    /// The function's next step (or MP_NONE while it opens a screen of its
    /// own, like race selection): sets the phase, clears fields and text.
    void set(uint8_t phase) const;
};

/// The field id of the input Get_Input() is returning THIS CALL when it
/// came from a client's ACTIVATE_FIELD, and 0 when it came from the mouse.
///
/// Every handler that reads the game POINTER rather than the returned
/// field id disagrees with an activation, because an activation moves no
/// pointer. The tech selection is where that costs the most: it commits
/// whatever `Draw_Tech_Select` last put under the cursor, so an
/// activation on a research row chose a different row, or dereferenced
/// null and took the process with it (open fix 23). A handler that wants
/// to honour the id it was handed can ask this, and leave the mouse path
/// untouched — which is the whole point of asking rather than changing
/// what the pointer means.
extern int16_t g_activated_input;

/// True while an injected mouse event is queued and not yet
/// pumped. Sync_Mouse_State_From_SDL stands aside while this is
/// set, so the injected position survives to whoever reads
/// mouse::Pointer_X(). Cleared by Pump_Game_Input_Queue().
extern bool g_injected_mouse_pending;

} // namespace ext
