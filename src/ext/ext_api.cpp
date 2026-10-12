// Extension API implementation.
// This is the only ext file that includes game headers.

#include "pch.h"
#include "ext/ext_api.h"
#include "ext/ext_server.h"
#include <cstdlib>  // OrionLayer, open fix 43. std::getenv
#include "game/newgame.h"
#include "game/platform.h"
#include "game/build_queue.h"  // OrionLayer, open fix 35. autobuild_settings, sent in "COLS".
#include "game/fmtpara.h"  // OrionLayer, open fix 67. The message box text's item values.

#include <vector>
#include <cstring>

namespace ext {

// OrionLayer, open fix 43. Hidden from the start only when the starter asks (ORION2RE_HIDE_WINDOW, set by OrionLayer's tools and play.py); started on its own, the window shows as before fix 41.
static bool Hide_Window_At_Start_() {
    const char* value = std::getenv("ORION2RE_HIDE_WINDOW");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}
std::atomic<bool> g_hide_window{Hide_Window_At_Start_()};
int16_t g_pending_field = 0;
int16_t g_activated_input = 0;
// OrionLayer, open fix 62. The rect the client saw at the pending index (x, y, x_end, y_end), when it sent one.
static bool g_pending_checked = false;
static int16_t g_pending_rect[4] = {0, 0, 0, 0};
bool g_injected_mouse_pending = false;

static Server g_server;
static int16_t g_last_screen = -99;
static int32_t g_last_stardate = -1;
static int16_t g_last_fields_count = -1;

// OrionLayer, open fix 56. The battle's events (ext_api.h): a ring of the last
// 1024, numbered from 1; SerializeState drains what is new.
struct CombatEventRecord {
    uint32_t seq;
    uint16_t serial;
    uint8_t type;
    int16_t a[8];
};
static CombatEventRecord g_combat_events[1024];
static uint32_t g_combat_event_next = 1;   // the number the next event gets
static uint32_t g_combat_event_sent = 1;   // the first number not yet sent

// OrionLayer, open fix 58. The battle command waiting to be consumed (ext_api.h).
struct CombatCommand {
    bool pending;
    uint16_t serial;
    int16_t unit;
    uint8_t op;
    int16_t a, b, c;
};
static CombatCommand g_combat_cmd = {};
static CombatCommand g_board_target = {};   // BOARD's second step
static bool g_cmd_cell_valid = false;
static int16_t g_cmd_cell_x = 0;
static int16_t g_cmd_cell_y = 0;

// ── Pointer coordinates ──────────────────────────────────

/// Convert a 640x480 game point to the window point that maps back
/// to it, so an injected mouse event survives the trip the game will
/// put it through.
///
/// Every injected mouse event is read back by
/// Map_Window_Point_To_Game_Point (platform.cpp:219), which calls
/// SDL_RenderCoordinatesFromWindow: WINDOW space -> logical 640x480
/// space. Pushing game coordinates raw therefore only lands correctly
/// when the window happens to be exactly its creation size, and it is
/// SDL_WINDOW_RESIZABLE and shown, so nothing keeps it there.
///
/// SDL_RenderCoordinatesToWindow is the exact inverse of the function
/// the event will be read through, so the round trip is the identity
/// at any window size, and letterboxing and non-integer scales come
/// out right without anyone computing them. Asking the renderer at
/// the moment of the click is also the only way to be current: the
/// size can change between one click and the next, and no client can
/// see it happen.
///
/// If the renderer cannot be reached the point is passed through
/// unchanged, which is the previous behaviour rather than a silent
/// zero.
static void Game_Point_To_Window_Point_(float game_x, float game_y,
                                        float* window_x,
                                        float* window_y) {
    *window_x = game_x;
    *window_y = game_y;

    SDL_Window* window = (SDL_Window*)platform::g_main_window;
    if (window == nullptr) {
        return;
    }
    SDL_Renderer* renderer = SDL_GetRenderer(window);
    if (renderer == nullptr) {
        return;
    }
    (void)SDL_RenderCoordinatesToWindow(renderer, game_x, game_y,
                                        window_x, window_y);
}

// ── Serialization helpers ────────────────────────────────

static void Write16(std::vector<uint8_t>& buf, int16_t v) {
    buf.push_back((uint8_t)(v & 0xFF));
    buf.push_back((uint8_t)((v >> 8) & 0xFF));
}

static void Write32(std::vector<uint8_t>& buf, int32_t v) {
    buf.push_back((uint8_t)(v & 0xFF));
    buf.push_back((uint8_t)((v >> 8) & 0xFF));
    buf.push_back((uint8_t)((v >> 16) & 0xFF));
    buf.push_back((uint8_t)((v >> 24) & 0xFF));
}

static void Write8(std::vector<uint8_t>& buf, uint8_t v) {
    buf.push_back(v);
}

static void WriteBytes(std::vector<uint8_t>& buf,
                       const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    buf.insert(buf.end(), p, p + len);
}

// OrionLayer, open fix 51. A string as uint8 length + raw bytes, at most `cap` (and 255) bytes.
static void WriteStr8(std::vector<uint8_t>& buf, const char* str, size_t cap) {
    size_t len = str != nullptr ? strnlen(str, cap) : 0;
    if (len > 255) len = 255;
    Write8(buf, (uint8_t)len);
    WriteBytes(buf, str, len);
}

// OrionLayer, open fix 51. A text input field of the "MPLY" block: its id,
// its max length, the text it holds (its buffer), and — while the field is
// being edited — the text typed so far (fields::continuous_string, raw: the
// commit strips a trailing '_').
static void WriteInputField(std::vector<uint8_t>& buf, int16_t field,
                            int16_t max_length, const char* text, size_t cap) {
    Write16(buf, field);
    Write8(buf, (uint8_t)max_length);
    WriteStr8(buf, text, cap);
    const bool editing = field > 0 && fields::input_field_active != 0
                         && fields::active_input_field_number == field;
    Write8(buf, (uint8_t)editing);
    if (editing) {
        WriteStr8(buf, fields::continuous_string, sizeof(fields::continuous_string));
    }
}

// ── State snapshot ───────────────────────────────────────

static void SerializeState(std::vector<uint8_t>& buf,
                           int16_t current_screen) {
    buf.clear();
    buf.reserve(64 * 1024); // Pre-allocate

    // Game Identity
    Write16(buf, current_screen);
    Write8(buf, (uint8_t)MOX::previous_screen);
    Write32(buf, MOX::stardate);
    Write16(buf, MOX::PLAYER_NUM);
    Write16(buf, MOX::NUM_PLAYERS);
    Write16(buf, MOX::NUM_STARS);
    Write16(buf, MOX::NUM_SHIPS);
    Write16(buf, MOX::NUM_COLONIES);
    Write8(buf, MOX::NUM_NEBULAS);
    Write8(buf, (uint8_t)MOX::game_type);

    // Map state
    Write16(buf, MOX::cur_map_scale);
    Write16(buf, MOX::cur_map_x);
    Write16(buf, MOX::cur_map_y);
    Write16(buf, MOX::MAP_MAX_X);
    Write16(buf, MOX::MAP_MAX_Y);

    // Settings (fixed 0x229 bytes)
    WriteBytes(buf, &MOX::settings, sizeof(s_settings));

    // Players (8 × sizeof(s_player))
    for (int i = 0; i < MAX_PLAYERS; i++) {
        WriteBytes(buf, &MOX::player[i], sizeof(s_player));
    }

    // Stars (num_stars × sizeof(s_star_data))
    Write16(buf, MOX::NUM_STARS);
    for (int i = 0; i < MOX::NUM_STARS; i++) {
        WriteBytes(buf, &MOX::star[i], sizeof(s_star_data));
    }

    // Ships (num_ships × sizeof(s_ship_data))
    Write16(buf, MOX::NUM_SHIPS);
    for (int i = 0; i < MOX::NUM_SHIPS; i++) {
        WriteBytes(buf, &MOX::ship[i], sizeof(s_ship_data));
    }

    // Colonies (num_colonies × sizeof(s_colony))
    Write16(buf, MOX::NUM_COLONIES);
    for (int i = 0; i < MOX::NUM_COLONIES; i++) {
        WriteBytes(buf, &MOX::colony[i], sizeof(s_colony));
    }

    // Planets — count derived from stars (5 per star)
    int16_t num_planets = MOX::NUM_STARS * 5;
    Write16(buf, num_planets);
    for (int i = 0; i < num_planets; i++) {
        WriteBytes(buf, &MOX::planet[i], sizeof(s_planet_data));
    }

    // Nebulas
    Write8(buf, MOX::NUM_NEBULAS);
    for (int i = 0; i < MOX::NUM_NEBULAS; i++) {
        WriteBytes(buf, &MOX::nebula[i], sizeof(s_nebula));
    }

    // Leaders (67 × sizeof(s_leader_data))
    for (int i = 0; i < MAX_LEADERS; i++) {
        WriteBytes(buf, &MOX::leaders[i], sizeof(s_leader_data));
    }

    // Antarans
    WriteBytes(buf, &MOX::antarans, sizeof(s_antaran));

    // Ship icons (for galaxy map positioning)
    Write16(buf, MOX::ship_icon_count);
    for (int i = 0; i < MOX::ship_icon_count; i++) {
        WriteBytes(buf, &MOX::ship_icon[i], sizeof(s_ship_icon));
    }

    // Newgame screen local settings (8 × int16, 16 bytes)
    // Live values from NEWGAME:: namespace — only meaningful on
    // screen 13 (SCREEN_NEW_GAME). Written back to MOX::settings
    // by Temps_To_Globals() on Accept.
    Write16(buf, NEWGAME::diff_button_variable);
    Write16(buf, NEWGAME::gal_size_button_variable);
    Write16(buf, NEWGAME::gal_age_button_variable);
    Write16(buf, NEWGAME::opp_button_variable);
    Write16(buf, (int16_t)NEWGAME::combat_button_variable);
    Write16(buf, NEWGAME::civ_button_variable);
    Write16(buf, NEWGAME::random_events_button_variable);
    Write16(buf, NEWGAME::xenon_attacks_button_variable);

    // OrionLayer, open fix 20. Two OPTIONAL trailing blocks, LAST in the
    // snapshot so an older client stops reading before them.
    //
    // 1. Ship icon owners, one uint8 per icon, 0xFF = unresolvable —
    //    doc/ext_ship_icon_owner.patch, carried here because a client
    //    reads exactly ship_icon_count bytes after the ng_* values as
    //    owners, and block 2 has to come after them.
    for (int i = 0; i < MOX::ship_icon_count; i++) {
        int16_t node = MOX::ship_icon[i].node_idx;
        int16_t ship = (node >= 0 && node < MAX_SHIPS)
                     ? MOX::ship_node[node].ship_idx : -1;
        Write8(buf, (ship >= 0 && ship < MAX_SHIPS)
                    ? (uint8_t)MOX::ship[ship].owner : 0xFF);
    }

    // 2. The ship node table and the fleet box selection: "FSEL", the
    //    stack the fleet box shows (-1 while box 2 is closed), the number
    //    of ship nodes in use, then per node int16 ship_idx and uint8
    //    selected, then the fleet box stack's chain: int16 length and one
    //    int16 node per link, from ship_stack_start[stack] along
    //    next_node — the order FLEETPOP builds its ship cells in
    //    (fleetpop.cpp:643-676). The node table is SENT, not left to a
    //    client to rebuild: SHIPSTAK::Sort_Ships_In_Stack
    //    (shipstak.cpp:261-278) rewrites ship_idx inside each chain after
    //    a qsort that is not stable, so no client can recompute it.
    buf.push_back((uint8_t)'F');
    buf.push_back((uint8_t)'S');
    buf.push_back((uint8_t)'E');
    buf.push_back((uint8_t)'L');
    int16_t fsel_stack = MOX::fleet_box_ship_stack;
    if (!MOVEBOX::Moveable_Box_Selected(2) || fsel_stack < 0
            || fsel_stack >= MOX::ship_stack_count) {
        fsel_stack = -1;
    }
    Write16(buf, fsel_stack);
    int16_t nodes = MOX::next_free_node;
    if (nodes < 0) nodes = 0;
    if (nodes > MAX_SHIPS) nodes = MAX_SHIPS;
    Write16(buf, nodes);
    for (int i = 0; i < nodes; i++) {
        Write16(buf, MOX::ship_node[i].ship_idx);
        Write8(buf, (uint8_t)(MOX::ship_node[i].selected != 0));
    }
    int16_t fsel_chain_len = 0;
    if (fsel_stack >= 0) {
        for (int16_t n = MOX::ship_stack_start[fsel_stack];
             n >= 0 && n < nodes && fsel_chain_len < nodes;
             n = MOX::ship_node[n].next_node) {
            fsel_chain_len++;
        }
    }
    Write16(buf, fsel_chain_len);
    if (fsel_stack >= 0) {
        int16_t n = MOX::ship_stack_start[fsel_stack];
        for (int16_t k = 0; k < fsel_chain_len; k++) {
            Write16(buf, n);
            n = MOX::ship_node[n].next_node;
        }
    }

    // 3. The fleet screen's view state: "FLTS", written ONLY while
    //    SCREEN_FLEET is up, and LAST, so nothing above it moves and a
    //    client that stops after FSEL is unaffected. OrionLayer, open
    //    fix 27.
    //
    //    NOTHING IN THIS BLOCK IS REACHABLE ANY OTHER WAY. Every value
    //    is a MOX or FLT1 global (mox.cpp:109-188, flt1.cpp:5-12) that
    //    SerializeState never touched, and the one that matters most —
    //    WHICH ships the screen is showing, in the order it shows them
    //    — cannot be rebuilt from what is already sent. FSEL carries
    //    the node table but only box 2's chain, which is -1 here
    //    because the fleet screen closes that box; and walking the node
    //    table instead does not work, because Remove_Non_Detected_Ships
    //    (shipstak.cpp:200-250) unlinks nodes of foreign stacks while
    //    Delete_Ship_Node (:5-11) leaves their ship_idx in place, so a
    //    client walking the table names ships the player cannot see.
    //
    //    The list is written in DISPLAY order, which is the order
    //    Build_Fltscrn_Big_Icon_Fields left in fltscrn_big_icon
    //    (flt2.cpp:207-268, including the officer move-to-front at
    //    :263-265) — the same order Get_Fltscrn_Big_Icon_XY turns into
    //    grid positions. It is a snapshot of an array, not a rebuild.
    if (current_screen == SCREEN_FLEET) {
        buf.push_back((uint8_t)'F');
        buf.push_back((uint8_t)'L');
        buf.push_back((uint8_t)'T');
        buf.push_back((uint8_t)'S');

        int16_t stack = MOX::small_ship_stack_ptr;
        int16_t head = -1;
        if (stack >= 0 && stack < MOX::ship_stack_count) {
            head = MOX::ship_stack_start[stack];
        } else {
            stack = -1;
        }
        Write16(buf, stack);
        Write16(buf, head);
        Write16(buf, MOX::fltscrn_stack_owner);

        int16_t icons = MOX::n_fltscrn_big_icons;
        if (icons < 0) icons = 0;
        if (icons > SHIP_LIST_CAPACITY) icons = SHIP_LIST_CAPACITY;
        Write16(buf, icons);
        Write16(buf, MOX::n_fltscrn_big_icons_added);
        Write16(buf, (int16_t)MOX::n_fltscrn_rows);

        // first_visible_row lives in the scroll bar the screen points
        // MOX::scroll_bar at (flt1.cpp:1245-1248); the pointer is null
        // until the first list is built, and 0 is then the truth.
        Write16(buf, MOX::scroll_bar ? MOX::scroll_bar->first_visible_row
                                      : (int16_t)0);
        Write16(buf, MOX::n_big_icons_selected);
        Write16(buf, MOX::scanned_big_ship);
        Write16(buf, MOX::scanned_small_ship);

        // The two filter radios. Their STATUS is what the screen reads
        // (flt2.cpp:130-146); the fields carry it by pointer and it is
        // not in the field list.
        Write16(buf, MOX::fltscrn_support_button_status);
        Write16(buf, MOX::fltscrn_combat_button_status);
        Write8(buf, FLT1::relocate_button_mode);
        Write8(buf, FLT1::merging_relocations);

        Write16(buf, icons);
        for (int16_t i = 0; i < icons; i++) {
            if (MOX::fltscrn_big_icon) {
                Write16(buf, MOX::fltscrn_big_icon[i].ship_idx);
                Write8(buf, (uint8_t)(MOX::fltscrn_big_icon[i].selected != 0));
            } else {
                Write16(buf, (int16_t)-1);
                Write8(buf, (uint8_t)0);
            }
        }
    }

    // 4. The Leaders screen's view state: "OFFS", written ONLY while
    //    SCREEN_OFFICERS is up, and LAST, so nothing above it moves and
    //    a client that stops after FSEL is unaffected. It can never
    //    meet FLTS: that one is written only on SCREEN_FLEET. OrionLayer,
    //    open fix 30.
    //
    //    NOTHING IN THIS BLOCK IS REACHABLE ANY OTHER WAY. Every value
    //    is an OFFICER, MOX or MAINPUPS global the screen keeps for
    //    itself (officer.cpp:12-29, mox.cpp) and SerializeState never
    //    touched. The leader records are on the wire already and the
    //    four listed leaders can be rebuilt from them
    //    (Build_Captain_Id_List is a pure function, officer.cpp:
    //    2705-2730), so the list is sent for the client to CHECK its
    //    reconstruction against, not because it could not be had. What
    //    cannot be had: which view is up, which button mode (hire, pool,
    //    dismiss), which leader is selected and which is under the
    //    pointer, which star the colony view shows and which it has
    //    chosen, and — in the ship view — which stack, which ships in
    //    which order and which of them is picked, where the list is
    //    scrolled. And, while the hire popup runs over this screen
    //    (mainpups.cpp:784-930, reporting 29 all the while), WHICH
    //    leader it offers and in which state.
    //
    //    The big-icon list is written in DISPLAY order, the order
    //    Build_Fltscrn_Big_Icon_Fields left in fltscrn_big_icon —
    //    the same array and the same order FLTS sends.
    if (current_screen == SCREEN_OFFICERS) {
        buf.push_back((uint8_t)'O');
        buf.push_back((uint8_t)'F');
        buf.push_back((uint8_t)'F');
        buf.push_back((uint8_t)'S');

        Write8(buf, (uint8_t)MOX::officer_scrn_type);
        Write8(buf, (uint8_t)OFFICER::officer_button_mode);
        Write8(buf, (uint8_t)MOX::officer_selected);
        Write8(buf, (uint8_t)MOX::officer_scanned);
        Write16(buf, OFFICER::officer_star_displayed);
        Write16(buf, OFFICER::officer_star_chosen);

        int16_t n_officers = MOX::n_fltscrn_officers;
        if (n_officers < 0) n_officers = 0;
        if (n_officers > 4) n_officers = 4;
        Write16(buf, n_officers);
        for (int i = 0; i < 4; i++) {
            Write16(buf, MOX::officer_id_list ? MOX::officer_id_list[i]
                                               : (int16_t)-1);
        }

        int16_t stack = MOX::small_ship_stack_ptr;
        int16_t head = -1;
        if (stack >= 0 && stack < MOX::ship_stack_count) {
            head = MOX::ship_stack_start[stack];
        } else {
            stack = -1;
        }
        Write16(buf, stack);
        Write16(buf, head);
        Write16(buf, MOX::scroll_bar ? MOX::scroll_bar->first_visible_row
                                      : (int16_t)0);
        Write16(buf, MOX::offpop_big_icon_selected);
        Write16(buf, MOX::scanned_big_ship);
        Write16(buf, MOX::scanned_small_ship);
        Write16(buf, MOX::galaxy_map_scanned_star);

        // The hire popup, only while it is up over THIS screen: the
        // flag is set by OFFICER::Hire_Officer_Popup for exactly the
        // popup's lifetime (officer.cpp:3391-3400).
        bool popup = MAINPUPS::on_officer_screen_flag != 0;
        Write16(buf, popup ? MAINPUPS::g_officer : (int16_t)-1);
        Write16(buf, popup ? MAINPUPS::field_state : (int16_t)-1);

        int16_t icons = MOX::n_fltscrn_big_icons;
        if (icons < 0) icons = 0;
        if (icons > SHIP_LIST_CAPACITY) icons = SHIP_LIST_CAPACITY;
        if (!MOX::fltscrn_big_icon) icons = 0;
        Write16(buf, icons);
        for (int16_t i = 0; i < icons; i++) {
            Write16(buf, MOX::fltscrn_big_icon[i].ship_idx);
            Write8(buf, (uint8_t)(MOX::fltscrn_big_icon[i].selected != 0));
        }
    }

    // 5. The Info screen's two inputs SerializeState never sent: "INFS",
    //    written ONLY while SCREEN_INFO is up, and LAST, so nothing above
    //    it moves and a client that stops earlier is unaffected. It can
    //    never meet OFFS or FLTS (other screens). OrionLayer, open fix 32.
    //
    //    - MOX::bill_savegame[6] (mox.cpp:778): [1] is where the history
    //      rings start once a game is past 350 turns (Get_History_Params,
    //      bill.cpp:547-556) and [2..5] are the divisors the four rings
    //      were scaled by (bill.cpp:445-535). The rings themselves are in
    //      s_player, on the wire already; without these six numbers the
    //      History Graph cannot be drawn or labelled (info.cpp:1222-1329).
    //    - the Turn Summary's messages, RENDERED exactly as the screen
    //      renders them (MSG_::sprintf_msg, info.cpp:1971), in the
    //      player's slice of MSG_::msgs (msg.cpp:86-114). The formats
    //      live in RSTRING<lang>.LBX and the names in half a dozen tables,
    //      so sending the finished strings is the one form a client can
    //      use without re-implementing sprintf_msg. Raw bytes, the game's
    //      8-bit text with its FMTPARA codes; int16 length first.
    if (current_screen == SCREEN_INFO) {
        buf.push_back((uint8_t)'I');
        buf.push_back((uint8_t)'N');
        buf.push_back((uint8_t)'F');
        buf.push_back((uint8_t)'S');
        for (int i = 0; i < 6; i++) {
            Write16(buf, MOX::bill_savegame[i]);
        }
        int16_t first = MSG_::First_Msg_For_Player(MOX::PLAYER_NUM);
        int16_t n = MSG_::N_Msgs_For_Player(MOX::PLAYER_NUM);
        if (first < 0 || first >= MAX_MESSAGES || n < 0) n = 0;
        if (first + n > MAX_MESSAGES) n = (int16_t)(MAX_MESSAGES - first);
        Write16(buf, n);
        char text[256];
        for (int16_t i = 0; i < n; i++) {
            MSG_::sprintf_msg(text, sizeof(text), &MSG_::msgs[first + i]);
            size_t len = strnlen(text, sizeof(text));
            Write16(buf, (int16_t)len);
            buf.insert(buf.end(), (const uint8_t*)text,
                       (const uint8_t*)text + len);
        }
    }

    // 5b. The diplomacy audience: "DIPL", written ONLY while it reports 57
    //     or 58 (open fix 46), between INFS and COLS. OrionLayer, open fix 47.
    //
    //     The audience keeps everything in DIP_SCRN's globals and the list
    //     field's arguments; none of it was serialized, and its menus are
    //     lists of equal shape (Propose, Break and Offer are all eight rows
    //     at the same place). Sent as the game holds them: which race, the
    //     ambassador's mode (0 refused, 1 normal, 2 an AI proposal), the
    //     statement id and the reply text AS THE ENGINE FORMATTED IT (the
    //     variant is Random_, dip_scrn_main.cpp:603, so an id does not name
    //     the text), and the menu taking input now — its title, and per
    //     item the enable flag Get_List_Field honours and the words.
    if (current_screen == 57 || current_screen == 58) {
        buf.push_back((uint8_t)'D');
        buf.push_back((uint8_t)'I');
        buf.push_back((uint8_t)'P');
        buf.push_back((uint8_t)'L');
        Write8(buf, 1);                                   // block version
        Write8(buf, (uint8_t)(current_screen - 56));      // 1 player, 2 AI
        Write8(buf, DIP_SCRN::current_ambassador);
        Write8(buf, DIP_SCRN::ambassador_option);
        Write16(buf, DIP_SCRN::response_message);
        size_t len = strnlen(DIP_SCRN::diplomacy_message,
                             sizeof(DIP_SCRN::diplomacy_message));
        Write8(buf, (uint8_t)len);
        WriteBytes(buf, DIP_SCRN::diplomacy_message, len);
        const ListFieldView& list = g_list_field;
        int16_t n = (list.items != nullptr && list.count > 0) ? list.count : 0;
        if (n > 40) n = 40;
        Write16(buf, n);
        if (n > 0) {
            len = list.title != nullptr ? strnlen(list.title, 255) : 0;
            Write8(buf, (uint8_t)len);
            WriteBytes(buf, list.title, len);
            const size_t item_max = list.item_size > 255 ? 255
                                                         : (size_t)list.item_size;
            for (int16_t i = 0; i < n; i++) {
                const char* item = list.items + (size_t)i * list.item_size;
                Write8(buf, list.flags != nullptr ? list.flags[i] : 1);
                len = strnlen(item, item_max);
                Write8(buf, (uint8_t)len);
                WriteBytes(buf, item, len);
            }
        }
    }

    // 6. Which colony the colony screen and its build popup show: "COLS",
    //    written ONLY while SCREEN_COLONY or SCREEN_QUEUE_POPUP is up, and
    //    LAST. OrionLayer, open fix 35.
    //
    //    Both screens derive their colony on entry from MOX::screen_data
    //    (the star) and COLONY::orbit_temp into COLONY::colony_handle
    //    (colony_main.cpp:249-255, colbldg.cpp:462-469); none of the three
    //    was serialized, and the game enters screen 1 from eight places,
    //    some of them its own (reports, info, turn summary, landing), so a
    //    client cannot know it. The pair is set by every caller BEFORE the
    //    switch; the handle only once the screen has run, so the first tick
    //    at the new id carries the previous handle — the pair and the handle
    //    are both sent and a client uses the colony when they agree.
    //    Also the screen's own modes, which no field shows:
    //    COLONY::drawing_display (0 the screen, 2 a planet description),
    //    COLONY::field_mode, and the autobuild setting that decides the
    //    label and what [18] does (colony_main.cpp:961-974).
    if (current_screen == SCREEN_COLONY || current_screen == SCREEN_QUEUE_POPUP) {
        buf.push_back((uint8_t)'C');
        buf.push_back((uint8_t)'O');
        buf.push_back((uint8_t)'L');
        buf.push_back((uint8_t)'S');
        Write16(buf, MOX::screen_data);
        Write16(buf, COLONY::orbit_temp);
        Write16(buf, COLONY::colony_handle);
        Write8(buf, COLONY::drawing_display);
        Write8(buf, COLONY::field_mode);
        Write8(buf, BUILD_QUEUE::autobuild_settings.enabled);
    }

    // 7. Where the colony screen puts its buildings: "CBLD", written ONLY
    //    while SCREEN_COLONY is up, and LAST. OrionLayer, open fix 36.
    //
    //    Make_Bldg_Array_For_Colony (colony_main.cpp:536-631) places the
    //    colony's buildings on a 6x6 grid and its satellites in ten slots
    //    with game_random seeded by the colony index plus Random_ draws;
    //    Draw_Colony_Bldgs and Draw_Colony_Satellites draw from those two
    //    arrays. A client would have to transcribe the RNG exactly, so the
    //    arrays are sent as they are: 36 building ids, row by row, then the
    //    ten satellite ids. Valid while COLS's handle agrees with its pair.
    if (current_screen == SCREEN_COLONY) {
        buf.push_back((uint8_t)'C');
        buf.push_back((uint8_t)'B');
        buf.push_back((uint8_t)'L');
        buf.push_back((uint8_t)'D');
        for (int r = 0; r < 6; r++) {
            for (int c = 0; c < 6; c++) {
                Write16(buf, (int16_t)COLONY::colony_bldgs[r].buildings[c]);
            }
        }
        for (int i = 0; i < 10; i++) {
            Write16(buf, COLONY::colony_satellites[i]);
        }
    }

    // 8. The colony screen's status word: "CEVT", written ONLY while
    //    SCREEN_COLONY is up, and LAST. OrionLayer, open fix 37.
    //
    //    Draw_Info_Name_And_Pop prints Blockaded, Plague or Pop Boom at
    //    (0,0) (colony_main.cpp:831-853). Blockaded is the star's bit, on
    //    the wire; the other two are EVENTS::Event_Check_Plague and
    //    Event_Check_Population_Boom over EVENTS::event_data, which is
    //    not. Their two answers for the shown colony, one byte each, 0xFF
    //    while the handle is not a colony (the first tick, COLS).
    if (current_screen == SCREEN_COLONY) {
        buf.push_back((uint8_t)'C');
        buf.push_back((uint8_t)'E');
        buf.push_back((uint8_t)'V');
        buf.push_back((uint8_t)'T');
        const int16_t h = COLONY::colony_handle;
        const bool ok = h >= 0 && h < MOX::NUM_COLONIES;
        Write8(buf, ok ? (uint8_t)(EVENTS::Event_Check_Plague(h) != 0) : (uint8_t)0xFF);
        Write8(buf, ok ? (uint8_t)(EVENTS::Event_Check_Population_Boom(h) != 0) : (uint8_t)0xFF);
    }

    // 9. What the shown colony's current product costs, and how long it
    //    takes: "CPRD", written ONLY while SCREEN_COLONY or
    //    SCREEN_QUEUE_POPUP is up, and LAST. OrionLayer, open fix 38.
    //
    //    The colony screen draws the production bar over
    //    COLCALC::Colony_Producing_Product_Cost (colony.cpp:967-978) and
    //    "%d turn(s)" from Calculate_Current_Production_Turn_Count
    //    (:980-994). production_spent and bought_outright are in s_colony
    //    and on the wire; the cost is a function of source tables, the
    //    player's ship designs and config values (colcalc.cpp:2576,
    //    lander_ship_cost), and the turns follow from it. The two numbers
    //    the screen draws are sent; -1 while the handle is not a colony.
    if (current_screen == SCREEN_COLONY || current_screen == SCREEN_QUEUE_POPUP) {
        buf.push_back((uint8_t)'C');
        buf.push_back((uint8_t)'P');
        buf.push_back((uint8_t)'R');
        buf.push_back((uint8_t)'D');
        const int16_t h = COLONY::colony_handle;
        const bool ok = h >= 0 && h < MOX::NUM_COLONIES;
        Write16(buf, ok ? MOX::colony[h].producing[0] : (int16_t)-1);
        Write32(buf, ok ? COLCALC::Colony_Producing_Product_Cost(&MOX::colony[h]) : -1);
        Write16(buf, ok ? COLONY::Calculate_Current_Production_Turn_Count(h) : (int16_t)-1);
    }

    // 10. The build popup's queue as it is being edited: "BLDQ", written
    //     ONLY while SCREEN_QUEUE_POPUP is up, and LAST.
    //     OrionLayer, open fix 39.
    //
    //     COLBLDG edits a copy, current_item[7], and writes it into the
    //     colony's producing[] only on OK or `<` / `>`
    //     (Do_Exit_Screen_Cleanup, colbldg.cpp:2003-2016); until then the
    //     wire shows the queue from before the popup. With it: the
    //     selection box (active_prod, Draw_Active_Prod :1792-1851), the
    //     mode a click is read in (field_mode: 1 pick a design, 2 repeat,
    //     :1577-1582) and the autobuild radio's value (colony_auto_building).
    if (current_screen == SCREEN_QUEUE_POPUP) {
        buf.push_back((uint8_t)'B');
        buf.push_back((uint8_t)'L');
        buf.push_back((uint8_t)'D');
        buf.push_back((uint8_t)'Q');
        for (int i = 0; i < 7; i++) {
            Write16(buf, COLBLDG::current_item[i]);
        }
        for (int i = 0; i < 4; i++) {
            Write16(buf, COLBLDG::active_prod[i]);
        }
        Write8(buf, COLBLDG::field_mode);
        Write16(buf, COLBLDG::colony_auto_building);
    }

    // 11. What the build popup offers, in its own order, with the numbers
    //     it prints for each: "BLDL", written ONLY while
    //     SCREEN_QUEUE_POPUP is up, and LAST. OrionLayer, open fix 40.
    //
    //     Calculate_Building_Array keeps Trade Goods and Housing first,
    //     then every building Colony_Can_Build_Product allows, sorted by
    //     the language's building names (colbldg.cpp:161-178); the ship and
    //     other list is Calculate_Military_Array (:180-264), with
    //     COLONY_PRODUCTION_SEPARATOR rows between its groups. A client copy
    //     would be a second copy of a long rule and of a sort by name. Per
    //     entry the id, then the three numbers Draw_Cost_And_Time_Info
    //     prints (:1115-1174): the cost (as the screen casts it), the
    //     maintenance for a building (-1 otherwise) and the build time;
    //     -1, -1, -1 for a separator.
    if (current_screen == SCREEN_QUEUE_POPUP) {
        buf.push_back((uint8_t)'B');
        buf.push_back((uint8_t)'L');
        buf.push_back((uint8_t)'D');
        buf.push_back((uint8_t)'L');
        const int16_t h = COLONY::colony_handle;
        const bool ok = h >= 0 && h < MOX::NUM_COLONIES;
        auto entry = [&](int16_t id) {
            Write16(buf, id);
            if (!ok || id == COLONY_PRODUCTION_SEPARATOR) {
                Write16(buf, (int16_t)-1);
                Write16(buf, (int16_t)-1);
                Write16(buf, (int16_t)-1);
                return;
            }
            Write16(buf, (int16_t)COLCALC::Colony_Product_Cost(&MOX::colony[h], id));
            Write16(buf, COLBLDG::Colony_Production_Is_Building(id)
                         ? (int16_t)TECHDATA::buildings[id].maintenance
                         : (int16_t)-1);
            Write16(buf, COLONY::Calculate_Colony_Turn_Count_From_Scrap_For_Prod(h, id));
        };
        int16_t nb = COLBLDG::n_building_indexes;
        if (nb < 0) nb = 0;
        if (nb > 54) nb = 54;
        Write16(buf, nb);
        for (int16_t i = 0; i < nb; i++) {
            entry(COLBLDG::building_indexes[i]);
        }
        int16_t nm = COLBLDG::n_military_indexes;
        if (nm < 0) nm = 0;
        if (nm > 54) nm = 54;
        Write16(buf, nm);
        for (int16_t i = 0; i < nm; i++) {
            entry(COLBLDG::military_indexes[i]);
        }
        // The seven queue items too: the summary shows the queue's first
        // item when nothing is hovered (colbldg.cpp:825), and a queued item
        // need not be in either list (a second Colony Base is not offered
        // once the queue holds as many as there are free worlds, :185).
        for (int i = 0; i < 7; i++) {
            const int16_t id = COLBLDG::current_item[i];
            if (id == COLONY_PRODUCTION_NONE) {
                Write16(buf, id);
                Write16(buf, (int16_t)-1);
                Write16(buf, (int16_t)-1);
                Write16(buf, (int16_t)-1);
            } else {
                entry(id);
            }
        }
    }

    // 12. The Ship Designer's design as it is being edited: "DSGN", written
    //     ONLY while the designer is up, and LAST. OrionLayer, open fix 44.
    //
    //     The designer edits MOX::design, an s_current_design in the global
    //     data segment (design_main.cpp:577), and writes it into the
    //     player's ship_designs[] only on Build (Update_Player_Design,
    //     design.cpp:755-801); until then nothing of the edit is on the wire.
    //     The test is the game's own current_screen, never the reported id,
    //     so the block also rides the sub-dialogs, which report their own
    //     ids when open fix 45 is applied. With it: the slot being edited
    //     and the refit flag, the two numbers the bottom line prints, the
    //     design itself, the values Print_Current_Design computes at draw
    //     time (design_main.cpp:7-95) and, per weapon row, the damage and
    //     modification strings the engine formats (:124-171).
    if (MOX::current_screen == SCREEN_DESIGN && MOX::design != nullptr) {
        buf.push_back((uint8_t)'D');
        buf.push_back((uint8_t)'S');
        buf.push_back((uint8_t)'G');
        buf.push_back((uint8_t)'N');
        const s_current_design* d = MOX::design;
        Write8(buf, 1);                                   // block version
        Write16(buf, MOX::temp_star_handle);             // design slot
        Write8(buf, (uint8_t)MOX::refit_ship);
        Write32(buf, (int32_t)DESIGN::Printed_Design_Cost());
        Write32(buf, DESIGN::printed_space_avail);
        WriteBytes(buf, d->name, 16);
        Write16(buf, d->ship_size);
        Write16(buf, d->picture_type);
        Write16(buf, d->shield);
        Write16(buf, d->ftl_type);
        Write16(buf, d->computer_type);
        Write16(buf, d->armor_type);
        Write16(buf, d->fuel_type);
        Write32(buf, d->hull_space);
        Write32(buf, d->space_used);
        Write32(buf, d->total_cost);
        Write16(buf, (int16_t)TECHDATA::drives[d->ftl_type].warp_speed);
        Write16(buf, (int16_t)DESIGN::Current_Design_Combat_Speed(MOX::design));
        Write16(buf, DESIGN::Current_Design_Structural_Points());
        Write16(buf, DESIGN::Current_Design_Armor_Absorption());
        Write16(buf, DESIGN::Shield_Strength());
        Write16(buf, DESIGN::Current_Shield_Damage_Points_Blocked());
        Write16(buf, (int16_t)TECHDATA::computers[d->computer_type].bonus);
        s_ship_design tmpl;
        DESIGN::Build_Design_Template(&tmpl);
        int16_t atk = 0, def = 0, evade = 0;
        INITSHIP::Get_Design_Combat_Bonuses(tmpl, &atk, &def, &evade);
        Write16(buf, def);
        Write16(buf, evade);
        char text[128];
        auto write_text = [&]() {
            size_t n = strnlen(text, sizeof(text));
            Write8(buf, (uint8_t)n);
            WriteBytes(buf, text, n);
        };
        for (int16_t i = 0; i < 8; i++) {
            Write16(buf, d->weapon_type[i]);
            Write16(buf, d->weapon_count[i]);
            Write8(buf, (uint8_t)d->weapon_firing_arc[i]);
            Write16(buf, (int16_t)d->weapon_specials[i]);
            Write32(buf, d->weapon_cost[i]);
            Write32(buf, d->weapon_space[i]);
            Write8(buf, (uint8_t)d->weapon_ammo[i]);
            text[0] = '\0';
            if (d->weapon_type[i] != WEAPON_NO_WEAPONS) {
                DESIGN::Design_Weapon_Damage_String(i, text, sizeof(text), -1, -1);
            }
            write_text();
            text[0] = '\0';
            if (d->weapon_type[i] != WEAPON_NO_WEAPONS) {
                DESIGN::Weapon_Mod_String((int16_t)d->weapon_specials[i], text, sizeof(text));
            }
            write_text();
        }
        for (int16_t i = 0; i < 8; i++) {
            Write16(buf, d->special_devices[i]);
        }
    }

    // 13. The Ship Designer's open sub-dialog: "DSBX", written ONLY while one
    //     of the three reports its synthetic id (54, 55, 56 — ext_api.h), and
    //     LAST. OrionLayer, open fix 45.
    //
    //     Each picker builds its list into MOX::design_choice_items (length
    //     scroll_bar->total_rows) and keeps its state in DESBOX's globals;
    //     none of it is on the wire. Per row the numbers the picker prints,
    //     by THE SAME CALLS WITH THE SAME ARGUMENTS as its drawing:
    //     Print_Shield_Data (desbox.cpp:2395-2402), Print_Computer_Data
    //     (:2502-2504), Print_Special_System_Data (System_Added_Cost_And_
    //     Space_, :2821), Print_Main_Weapon_Box (:2203-2221, the chosen
    //     row with the current arcs, rack and mods, every other with the
    //     defaults) — and for a weapon its damage string, as the engine
    //     formats it (:2279).
    if (current_screen >= 54 && current_screen <= 56 &&
        MOX::scroll_bar != nullptr && MOX::design != nullptr) {
        buf.push_back((uint8_t)'D');
        buf.push_back((uint8_t)'S');
        buf.push_back((uint8_t)'B');
        buf.push_back((uint8_t)'X');
        const s_current_design* d = MOX::design;
        Write8(buf, 1);                                   // block version
        Write8(buf, (uint8_t)(current_screen - 53));      // 1, 2 or 3
        Write16(buf, MOX::design_screen_replacement_type);
        Write16(buf, MOX::design_screen_replacement_slot);
        Write16(buf, MOX::design_screen_replacement_item);
        Write16(buf, MOX::scroll_bar->first_visible_row);
        Write16(buf, MOX::scroll_bar->visible_rows);
        Write16(buf, DESBOX::field_item_chosen);
        Write16(buf, DESBOX::field_item_scanned);
        Write16(buf, (int16_t)DESBOX::weapon_replacement_mods);
        Write16(buf, DESBOX::weapon_replacement_arcs);
        Write16(buf, (int16_t)DESBOX::weapon_replacement_rack);
        Write8(buf, DESBOX::beam_filter_button_status);
        Write8(buf, DESBOX::missile_filter_button_status);
        Write8(buf, DESBOX::bomb_filter_button_status);
        Write8(buf, DESBOX::special_filter_button_status);
        for (int i = 0; i < WEAPON_MOD_COUNT; i++) {
            Write16(buf, MOX::weapon_mod_field_status[i]);
        }
        //     Which modifications the weapon picker offers, bit i per mod —
        //     the test Add_Replacement_Weapon_Fields adds a field by
        //     (desbox.cpp:475-478); the fields alone do not say which mod
        //     each is. OrionLayer, open fix 45.
        uint16_t offered = 0;
        if (current_screen == 55 && DESBOX::field_item_chosen > -1) {
            for (int16_t i = 1; i < WEAPON_MOD_COUNT; i++) {
                if (DESBOX::Mod_Allowed_On_Weapon_Type((uint8_t)i) &&
                    DESBOX::Should_Add_Mod(i)) {
                    offered |= (uint16_t)(1u << i);
                }
            }
        }
        Write16(buf, (int16_t)offered);
        int16_t n = MOX::scroll_bar->total_rows;
        if (n < 0) n = 0;
        if (n > 40) n = 40;
        Write16(buf, n);
        char text[128];
        for (int16_t row = 0; row < n; row++) {
            const int16_t item = MOX::design_choice_items[row];
            int32_t cost = 0, space = 0;
            int16_t extra = 0;
            text[0] = '\0';
            if (current_screen == 54 && item > 0 &&
                MOX::design_screen_replacement_type == TECH_APPLICATION_TYPE_SHIELD) {
                const int16_t lvl = DESIGN::Tech_Level(MOX::PLAYER_NUM, TECHDATA::shields[item].tech_app_id);
                cost = DESIGN::Cost_Given_M_Level(TECHDATA::shields[item].cost_per_ship_type[d->ship_size], lvl);
                space = static_cast<int16_t>(DESIGN::Space_Given_M_Level(TECHDATA::shields[item].space_per_ship_type[d->ship_size], lvl, 0));
            } else if (current_screen == 54 && item > 0) {
                const int16_t lvl = DESIGN::Tech_Level(MOX::PLAYER_NUM, TECHDATA::computers[item].tech_app_id);
                cost = DESIGN::Cost_Given_M_Level(DESIGN::Computer_Cost(item), lvl);
                extra = (int16_t)TECHDATA::computers[item].bonus;
            } else if (current_screen == 56 && item > 0) {
                int16_t c16 = 0, s16 = 0;
                DESBOX::System_Added_Cost_And_Space(row, &c16, &s16);
                cost = c16;
                space = s16;
            } else if (current_screen == 55) {
                const int16_t weapon = DESBOX::Weapon_Index(row);
                if (weapon > 0 && row > 0) {
                    const bool chosen = DESBOX::field_item_chosen > -1 &&
                                        DESBOX::field_item_chosen == row;
                    const int16_t ammo = DESIGN::Weapon_Needs_Racks(weapon) != 0
                        ? DESIGN::Missile_Rack_Quantity(chosen ? (int16_t)DESBOX::weapon_replacement_rack : 1)
                        : 0;
                    const int16_t arc = chosen ? DESBOX::weapon_replacement_arcs : (int16_t)WEAPON_FIRING_ARC_FORWARD;
                    const int16_t mods = chosen ? (int16_t)DESBOX::weapon_replacement_mods : 0;
                    cost = DESIGN::Weapon_Cost(weapon, 1, arc, ammo, mods);
                    space = DESIGN::Weapon_Space(MOX::PLAYER_NUM, weapon, 1, arc, ammo, mods);
                }
                if (weapon > 0) {
                    DESIGN::Design_Weapon_Damage_String(weapon, text, sizeof(text), DESBOX::field_item_chosen, row);
                }
                extra = weapon;
            }
            Write16(buf, item);
            Write8(buf, (uint8_t)MOX::design_choice_item_selected[row]);
            Write8(buf, (uint8_t)d->replacement_item_unlocked[row]);
            Write32(buf, cost);
            Write32(buf, space);
            Write16(buf, extra);
            const size_t len = strnlen(text, sizeof(text));
            Write8(buf, (uint8_t)len);
            WriteBytes(buf, text, len);
        }
    }

    // 14. The move verdict the fleet box's selection would get at every
    //     star: "FMOV", written ONLY on SCREEN_MAIN while the fleet box is
    //     open and a ship in it is selected, and LAST. OrionLayer, open fix 48.
    //
    //     The original computes it on HOVER (Scan_Stars_XY,
    //     mainscr.cpp:2987-2993) into g_ship_move_info and colours the
    //     preview line from it (Draw_ETA_Destination_Line, :558-567); a
    //     client hovers without the engine's pointer, so the same call is
    //     made here for every star into a LOCAL struct, the global
    //     untouched. Sent as the game fills it: the 14-byte s_ship_move_info.
    if (current_screen == SCREEN_MAIN && MOVEBOX::Moveable_Box_Selected(2) != 0 &&
        FLEETPOP::Some_Icon_Selected()) {
        static int16_t ids[MAX_SHIPS];
        HACCESS::Get_Fleet_Box_Selected_Ship_Ids(ids, MAX_SHIPS, 0x00FF);
        if (ids[0] != -1) {  // Ships_Try_To_Move_To reads ship[ids[0]] unchecked
            buf.push_back((uint8_t)'F');
            buf.push_back((uint8_t)'M');
            buf.push_back((uint8_t)'O');
            buf.push_back((uint8_t)'V');
            Write8(buf, 1);                                     // block version
            Write16(buf, MOX::NUM_STARS);
            Write8(buf, (uint8_t)sizeof(s_ship_move_info));     // 14
            for (int16_t s = 0; s < MOX::NUM_STARS; s++) {
                s_ship_move_info mi;
                SHIPMOVE::Ships_Try_To_Move_To(ids, s, &mi);
                WriteBytes(buf, &mi, sizeof(mi));
            }
        }
    }

    // 15. The generic message box that is up: "MSGB", written on ANY screen
    //     while one is (the boxes run under their caller's id), and LAST.
    //     OrionLayer, open fix 29.
    //
    //     TEXTBOX::Do_Text_Box and GENDRAW's exploding, warning and
    //     confirmation boxes print a caller's string that exists nowhere
    //     else — H_Message(n) formatted with values the engine computed —
    //     so a client saw only their pixels. Sent as the box holds it: the
    //     kind, the answer fields, a timed box's ticks, the title and the
    //     text raw (FMTPARA codes and all), each with its length first.
    if (g_message_box.kind != 0) {
        buf.push_back((uint8_t)'M');
        buf.push_back((uint8_t)'S');
        buf.push_back((uint8_t)'G');
        buf.push_back((uint8_t)'B');
        Write8(buf, 2);              // block version; 2: the item values, open fix 67
        Write8(buf, g_message_box.kind);
        Write16(buf, g_message_box.field_a);
        Write16(buf, g_message_box.field_b);
        Write16(buf, g_message_box.ticks);
        for (const char* str : {g_message_box.title, g_message_box.text}) {
            const size_t len = str != nullptr ? strnlen(str, 4000) : 0;
            Write16(buf, (int16_t)len);
            WriteBytes(buf, str, len);
        }
#ifdef ORION2RE_EXT
        // OrionLayer, open fix 67. The values the text's FMTPARA item codes
        // (0x18, a letter, one hex digit: Insert_Item, fmtpara.cpp:854)
        // print, read through the game's own Get_Item_Value while the box
        // is up, which is while its caller's variables are (the boarding
        // result, cmbtfir2.cpp:1640-1647): a count, then index and value.
        // Only an index the text names and whose pointer is set is read.
        std::vector<std::pair<uint8_t, int32_t>> items;
        const char* t = g_message_box.text;
        for (size_t i = 0; t != nullptr && t[i] != '\0' && i < 4000; ++i) {
            if (t[i] != 0x18 || t[i + 1] == '\0') continue;
            size_t k = i + 2;
            while (t[k] == ' ') ++k;
            const int c = toupper((unsigned char)t[k]);
            const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0 || FMTPARA::para_global.item_ptr[d] == nullptr) continue;
            bool seen = false;
            for (const auto& it : items) seen = seen || it.first == d;
            if (!seen) items.emplace_back((uint8_t)d, (int32_t)FMTPARA::Get_Item_Value((uint16_t)d));
        }
        Write8(buf, (uint8_t)items.size());
        for (const auto& it : items) {
            Write8(buf, it.first);
            WriteBytes(buf, &it.second, sizeof(it.second));
        }
#endif
    }

    // 16. The turn-time popup that is up: "TPOP", written on ANY screen
    //     while one is, and LAST (after MSGB). OrionLayer, open fix 49.
    //
    //     The popups the turn start and turn processing open run under the
    //     galaxy map's id (0) or the processing id (12) and keep what they
    //     show in their own locals; TurnPopupGuard reports a synthetic id
    //     for each and puts what it was opened with here:
    //
    //     kind  id  popup (function)                         args
    //      1    52  science room (SCIENCE::Science_Room)    [0] entries, [1] reason 0 unknown 1 researched
    //                                                          2 stolen 3 artifact 4 captured, [2] entries shown
    //                                                          so far, [3..] discovery_data slots raw (bit 0x4000 =
    //                                                          weapon-mod entry, the next slot its mask)
    //                                                          text = footer
    //      2    40  Turn Summary (TURNSUM::Turn_Summary_Popup) [0] cur_page [1] max_page [2] up field
    //                                                          [3] down field [4] OK field [5] first row field
    //                                                          [6] last row field [7] frame field [8] background
    //                                                          field [9] message index it opened at (absolute)
    //                                                          + the message list below
    //      3    59  leader for hire (MAINPUPS::Random_New_Officer_Popup)
    //                                                        [0] leader [1] cost [2] star [3] field state 0 offer
    //                                                          1 rejected 2 too poor 3 hired [4] hire field
    //                                                          [5] reject/close field [6] dismiss field
    //                                                          [7] over the Leaders screen (then id 29 is kept)
    //                                                          [8] maintenance
    //      4    60  planet choice (MAINPUPS::New_Colony_Selection_Popup and its
    //               New_Outpost_ / Drop_Troops_ wrappers)  [0] star [1] mode 0 colony 1 outpost 2 troops
    //                                                          [2] using colony ship [3] special shown
    //                                                          [4] cancel field [5] dismiss field
    //                                                          title = the popup's title; text = the special's
    //                                                          description while it is shown
    //                                                          + the system display below
    //      5    61  new system (MAINPUPS::New_System_Discovery_Popup)
    //                                                        [0] star [1] special shown [2] dismiss field
    //                                                          title/text as kind 4 + the system display below
    //      6    62  leader level / marooned hero (MAINPUPS::Officer_Gains_Level_Popup)
    //                                                        [0] leader [1] state 0 level 1 joins 2 fails
    //                                                          [2] star [3] dismiss field
    //      7    63  GNN (EVENTS::Drive_Event_Screen)        [0] message type (0 intro, 1 next event,
    //                                                          event+2) [1] event id or -1 [2] subtype or -1
    //                                                          [3] picture EVENTS.LBX or -1 [4..14]
    //                                                          Get_Event_Message's colony_idx, val1, val2,
    //                                                          player_idx1, tech_field_idx1, player_idx2,
    //                                                          leader_idx, player_idx3, star_idx,
    //                                                          tech_app_idx1, tech_app_idx2; text = the news
    //      8    64  combat target (MAINPUPS::Defense_Colony_Selection_Popup)
    //                                                        [0] star [1] attacker [2] cancel field
    //                                                          + the system display and COMBFIND::target below
    //      9    33  colony landing (COLLAND::Colony_Landing_Screen)
    //                                                        [0] planet [1] colony [2] star [3] dismiss field
    //                                                          title = "colony built on" line
    //
    //     Written: version, kind, n_args + args, title, text (int16
    //     length + raw bytes each), then per kind:
    //     - 2: int16 count, per message: int16 length + the line as
    //       MSG_::sprintf_msg renders it, uint8 1 if a click on it jumps
    //       to a colony (MSG_::Goto_Msg_Colony's test), int16 that colony
    //       or -1, uint8 its list page, int16 its first field on the page
    //       shown or -1, uint8 its field count there (0 if not shown).
    //     - 4, 5, 8: the system display's live fields: int16 star, int16
    //       the grid field (asteroids: picked by POINTER position), 5 x
    //       (int16 planet field, int16 planet) per orbit slot, 15 x (int16
    //       ship field, int16 ship) per ship slot; -1000 / -1 when empty.
    //     - 8 also: COMBFIND::target: 5 x int16 colony, int16 count,
    //       15 x int16 player, int16 count.
    if (g_turn_popup.kind != 0) {
        buf.push_back((uint8_t)'T');
        buf.push_back((uint8_t)'P');
        buf.push_back((uint8_t)'O');
        buf.push_back((uint8_t)'P');
        Write8(buf, 1);                                   // block version
        Write8(buf, g_turn_popup.kind);
        int16_t n_args = g_turn_popup.n_args;
        if (n_args < 0) n_args = 0;
        if (n_args > 16) n_args = 16;
        Write16(buf, n_args);
        for (int16_t i = 0; i < n_args; i++) {
            Write16(buf, g_turn_popup.args[i]);
        }
        for (const char* str : {g_turn_popup.title, g_turn_popup.text}) {
            const size_t len = str != nullptr ? strnlen(str, 4000) : 0;
            Write16(buf, (int16_t)len);
            WriteBytes(buf, str, len);
        }

        if (g_turn_popup.kind == 2) {
            const s_list_state& list = TURNSUM::g_turn_sum_header;
            int16_t first = MSG_::First_Msg_For_Player(MOX::PLAYER_NUM);
            int16_t n = MSG_::N_Msgs_For_Player(MOX::PLAYER_NUM);
            if (first < 0 || first >= MAX_MESSAGES || n < 0) n = 0;
            if (first + n > MAX_MESSAGES) n = (int16_t)(MAX_MESSAGES - first);
            Write16(buf, n);
            char text[256];
            uint8_t page = 0;
            for (int16_t i = 0; i < n; i++) {
                s_MSG& msg = MSG_::msgs[first + i];  // sprintf_msg takes it non-const, as INFS passes it
                MSG_::sprintf_msg(text, sizeof(text), &msg);
                const size_t len = strnlen(text, sizeof(text));
                Write16(buf, (int16_t)len);
                WriteBytes(buf, text, len);

                // Goto_Msg_Colony's test (msg.cpp), without its writes.
                int16_t colony = -1;
                if (msg.msg_type == MSG_TYPE_COLONY) {
                    const int16_t planet = msg.u.colony.planet_idx;
                    if (planet >= 0 && planet < MAX_PLANETS) {
                        const int16_t c = MOX::planet[planet].colony_index;
                        if (c >= 0 && MOX::colony[c].owner == MOX::PLAYER_NUM) {
                            colony = c;
                        }
                    }
                }
                Write8(buf, (uint8_t)(colony != -1));
                Write16(buf, colony);

                // The list item: its page (Init_List_Data starts a page at
                // each stop_flag) and, on the page shown, the fields
                // Add_Fields_To_List_Page gave its lines.
                int16_t first_field = -1;
                uint8_t n_fields = 0;
                if (list.items != nullptr && i < list.num_items) {
                    const s_list_item& item = list.items[i];
                    if (i == 0 || item.stop_flag != 0) page++;
                    if (page == list.cur_page && item.app_count != 0) {
                        first_field = (int16_t)item.app_ids[0];
                        n_fields = item.app_count;
                    }
                }
                Write8(buf, page);
                Write16(buf, first_field);
                Write8(buf, n_fields);
            }
        }

        if (g_turn_popup.kind == 4 || g_turn_popup.kind == 5 ||
            g_turn_popup.kind == 8) {
            Write16(buf, COLONY::sys_disp_star_handle);
            Write16(buf, MOX::system_display_grid_field);
            for (int i = 0; i < 5; i++) {
                Write16(buf, MOX::planet_buttons[i]);
                Write16(buf, MOX::sys_disp != nullptr ? MOX::sys_disp[i].orbit
                                                      : (int16_t)-1);
            }
            for (int i = 0; i < 15; i++) {
                const bool used = MOX::system_display_ships[i].ship_idx > -1;
                Write16(buf, used ? MOX::system_display_ships[i].button
                                  : (int16_t)-1000);
                Write16(buf, used ? MOX::system_display_ships[i].ship_idx
                                  : (int16_t)-1);
            }
        }

        if (g_turn_popup.kind == 8) {
            for (int i = 0; i < 5; i++) {
                Write16(buf, COMBFIND::target.colony_targets[i]);
            }
            Write16(buf, COMBFIND::target.num_colony_targets);
            for (int i = 0; i < 15; i++) {
                Write16(buf, COMBFIND::target.player_targets[i]);
            }
            Write16(buf, COMBFIND::target.num_player_targets);
        }
    }

    // 17. The Hall of Fame: "HOFM", written ONLY while it takes input (it
    //     reports 14 on both ways in), and LAST. OrionLayer, open fix 50.
    //
    //     SCORE::Hall_Of_Fame_Screen draws its ten entries from SCORE::hof
    //     in the order Init sorted them (hi->sort_list, score descending)
    //     and the difficulty words from ESTRINGS into difficulty_string —
    //     none of it was serialized. Sent in the DISPLAY order, as the file
    //     holds it: the record, the score, the race id, the difficulty and
    //     its word as the engine loaded it, the player's and the race's
    //     names; and the entry the screen flashes (a new record at game end).
    if (current_screen == SCREEN_HALL_OF_FAME && g_hof_live && SCORE::hi != nullptr) {
        buf.push_back((uint8_t)'H');
        buf.push_back((uint8_t)'O');
        buf.push_back((uint8_t)'F');
        buf.push_back((uint8_t)'M');
        Write8(buf, 1);                                   // block version
        Write16(buf, SCORE::hof.version);
        Write16(buf, SCORE::entry_to_flash);
        Write8(buf, 10);
        for (int i = 0; i < 10; i++) {
            const int16_t r = SCORE::hi->sort_list[i][1];
            const bool ok = r >= 0 && r < 10;
            Write16(buf, r);
            Write16(buf, ok ? SCORE::hof.scores[r] : 0);
            Write16(buf, ok ? SCORE::hof.race_ids[r] : 0);
            const int8_t d = ok ? SCORE::hof.difficulties[r] : 0;
            Write8(buf, (uint8_t)d);
            const char* strs[3] = {
                ok ? SCORE::hof.player_names[r] : "",
                ok ? SCORE::hof.race_names[r] : "",
                (d >= 0 && d < GAME_DIFFICULTY_COUNT) ? SCORE::difficulty_string[d] : ""};
            const size_t caps[3] = {20, 20, 15};
            for (int k = 0; k < 3; k++) {
                const size_t len = strnlen(strs[k], caps[k]);
                Write8(buf, (uint8_t)len);
                WriteBytes(buf, strs[k], len);
            }
        }
    }

    // 18. The multiplayer step that takes input: "MPLY", written on ANY
    //     screen while a MultiplayerGuard is set, and LAST (after HOFM).
    //     OrionLayer, open fix 51.
    //
    //     The screens behind the main menu's Multiplayer button keep what
    //     they show in MOX::mp_screen (MULTPLAY::g_mp_screen), NETMOX::net,
    //     the router adapter and their locals; a client saw one id for many
    //     steps (15, 21, 22, 41) and none of the content. The phase says
    //     which step takes input; the payload is what it shows. int16 LE.
    //
    //     phase  id     step                                    payload
    //      1     15     MP setup (Multi_Player_Screen)          A
    //      2     15     Online setup dialog (Online_Setup_Screen_) B
    //      3     41     net game name (Change_MP_Game_Name)     C
    //      4     41     load MP game list (Load_Multi_Player_Game_Screen) D
    //      5     16     hotseat setup (Hotseat_Screen)          E
    //      6     17     hotseat player switch (Hotseat_Select_Player) F
    //      7     21/41  host: initializing                       G
    //      8     21     host: waiting for joiners                H
    //      9     21     host: waiting for race info              I
    //     10     21/41  host: sending data                       J
    //     11     41     host a loaded game: pick own position    K
    //     12     41     host a loaded game: waiting for joiners  H + L
    //     13     22     join: initializing                       G
    //     14     22     join: game list (Choose_Multi_Network_Game_Screen) M
    //     15     22     join: joined, waiting for the host       G
    //     16     22     join: generating map (after race pick)   (none)
    //     17     22     join a loaded game: pick position        K
    //     18     22     join a loaded game: getting data         J
    //     19     37     net next turn, chat (Chat_Box_Input_Loop) N
    //
    //     s8 = uint8 length + raw bytes. IN = input field: int16 field,
    //     uint8 max length, s8 its buffer, uint8 editing, s8 typed text (only
    //     if editing). Field ids are -1000 where the game adds none.
    //     A: uint8 settings.multi_player_game_type (1 network, 2 online,
    //        4 hotseat), uint8 game_type, uint8 net.mode, 8 x int16 field:
    //        network, online, hotseat, cancel, start, load, join, setup;
    //        s8 endpoint, s8 settings.net_game_name.
    //     B: IN endpoint (max 30), int16 OK field, int16 Cancel field.
    //     C: IN name (namestar::input_box), int16 cancel field, s8 prompt.
    //     D: uint8 game_type, int16 cancel field, uint8 10, per slot: int16
    //        field, uint8 valid (the save's game type 1-3, 0 invalid), s8
    //        description, s8 stardate, s8 date (as LOADSAVE formatted them).
    //     E: int8 settings.number_of_players, int16 humans joined, int16 join
    //        (-1000 when full), int16 accept, int16 cancel; per joined human
    //        (humans of them, slot i = player i): uint8 race, uint8 color,
    //        s8 name, s8 race name.
    //     F: uint8 8, per player: uint8 status (0 none, 1 has played,
    //        2 still to play: only 2 is selectable), int16 row field, int16
    //        banner field.
    //     G: uint8 net.mode, s8 endpoint, s8 net.cur_game_name.
    //     H: int16 begin field, int16 router users, int8 number_of_players,
    //        s8 net.cur_game_name.
    //     I: int16 players with race info (net.player_connected_flags),
    //        int16 router users, int8 net.num_connected_players.
    //     J: s8 MOX::temp_string (the status line the screen prints, or "").
    //     K: int16 begin field, uint8 8, per player: uint8 shown (human, not
    //        eliminated), uint8 taken (connected: rows of taken players do
    //        nothing), int16 row field, int16 banner field.
    //     L: int8 net.num_connected_players (humans in the save), int32
    //        net.joining_net_id (-1 none: a joiner is picking).
    //     M: int16 cancel field, uint8 rows, per row: int16 field, uint16
    //        players, uint16 max players, uint8 open, s8 session name.
    //     N: IN chat (max 80; buffer MULTPLAY::global_chat_string), int16
    //        the full-screen "C" field; uint8 humans, per human banner:
    //        uint8 player, uint8 has hit next turn; uint8 lines, per chat
    //        line as the screen prints it: uint8 sender (>= 8 GNN), s8 line.
    if (g_multiplayer.phase != MP_NONE) {
        buf.push_back((uint8_t)'M');
        buf.push_back((uint8_t)'P');
        buf.push_back((uint8_t)'L');
        buf.push_back((uint8_t)'Y');
        Write8(buf, 1);                                   // block version
        const uint8_t phase = g_multiplayer.phase;
        Write8(buf, phase);

        const s_mp_screen* mp = MOX::mp_screen;
        static const s_mp_screen mp_none = {};            // before the step sets it
        const s_mp_screen& m = mp != nullptr ? *mp : mp_none;
        const s_network& net = NETMOX::net;
        const netadapter::s_adapter* router = netcode::Ext_Router_Adapter_();
        const int16_t users = router != nullptr
            ? (int16_t)netadapter::Get_Users(router, nullptr, 0) : 0;

        switch (phase) {
        case MP_SETUP:
            Write8(buf, MOX::settings.multi_player_game_type);
            Write8(buf, (uint8_t)MOX::game_type);
            Write8(buf, net.mode);
            for (int16_t f : {m.field_id_network, m.field_id_online, m.field_id_hotseat,
                              m.field_id_cancel, m.field_id_start_game, m.field_id_load_game,
                              m.field_id_join_game, m.field_id_setup}) {
                Write16(buf, f);
            }
            WriteStr8(buf, net.online_settings.endpoint, sizeof(net.online_settings.endpoint));
            WriteStr8(buf, MOX::settings.net_game_name, sizeof(MOX::settings.net_game_name));
            break;
        case MP_ONLINE_SETUP:
            WriteInputField(buf, m.game_list_field_ids[3], 30, net.online_settings.endpoint,
                            sizeof(net.online_settings.endpoint));
            Write16(buf, m.field_id_ok);
            Write16(buf, m.field_id_cancel);
            break;
        case MP_NET_GAME_NAME:
            WriteInputField(buf, namestar::input_box.accept_field_id,
                            namestar::input_box.max_input_chars,
                            namestar::input_box.input_text,
                            sizeof(namestar::input_box.input_text));
            Write16(buf, namestar::input_box.cancel_field_id);
            WriteStr8(buf, g_multiplayer.text, 255);
            break;
        case MP_LOAD_GAME: {
            Write8(buf, (uint8_t)MOX::game_type);
            Write16(buf, m.field_id_cancel);
            Write8(buf, 10);
            const bool dates = MOX::save_game_dates != nullptr
                               && MOX::save_game_stardates != nullptr;
            for (int i = 0; i < 10; i++) {
                Write16(buf, m.game_list_field_ids[i]);
                Write8(buf, (uint8_t)m.save_slot_valid[i]);
                WriteStr8(buf, MOX::save_game_description[i].description,
                          sizeof(MOX::save_game_description[i].description));
                WriteStr8(buf, dates ? MOX::save_game_stardates + i * 25 : nullptr, 25);
                WriteStr8(buf, dates ? MOX::save_game_dates + i * 25 : nullptr, 25);
            }
            break;
        }
        case MP_HOTSEAT_SETUP: {
            Write8(buf, (uint8_t)MOX::settings.number_of_players);
            int16_t humans = HOTPOP::hotseat_human_players;
            if (humans < 0) humans = 0;
            if (humans > MAX_PLAYERS) humans = MAX_PLAYERS;
            Write16(buf, humans);
            for (int16_t f : g_multiplayer.fields) {
                Write16(buf, f);
            }
            for (int16_t i = 0; i < humans; i++) {
                const s_player& p = MOX::player[i];
                Write8(buf, p.race);
                Write8(buf, p.color);
                WriteStr8(buf, p.name, sizeof(p.name));
                WriteStr8(buf, p.race_name, sizeof(p.race_name));
            }
            break;
        }
        case MP_HOTSEAT_SELECT:
            Write8(buf, MAX_PLAYERS);
            for (int i = 0; i < MAX_PLAYERS; i++) {
                const int8_t status = m.player_button_status[i];
                Write8(buf, (uint8_t)status);
                Write16(buf, status != 0 ? m.player_field_ids[i] : (int16_t)-1000);
                Write16(buf, status != 0 ? m.game_list_field_ids[i] : (int16_t)-1000);
            }
            break;
        case MP_HOST_INIT:
        case MP_JOIN_INIT:
        case MP_JOIN_WAITING:
            Write8(buf, net.mode);
            WriteStr8(buf, net.online_settings.endpoint, sizeof(net.online_settings.endpoint));
            WriteStr8(buf, net.cur_game_name, sizeof(net.cur_game_name));
            break;
        case MP_HOST_WAIT_JOINERS:
        case MP_HOST_WAIT_LOADED:
            Write16(buf, m.field_id_begin);
            Write16(buf, users);
            Write8(buf, (uint8_t)MOX::settings.number_of_players);
            WriteStr8(buf, net.cur_game_name, sizeof(net.cur_game_name));
            if (phase == MP_HOST_WAIT_LOADED) {
                Write8(buf, (uint8_t)net.num_connected_players);
                Write32(buf, net.joining_net_id);
            }
            break;
        case MP_HOST_WAIT_RACES: {
            int16_t ready = 0;
            for (int i = 0; i < MOX::NUM_PLAYERS && i < MAX_PLAYERS; i++) {
                if (net.player_connected_flags[i] != 0) ready++;
            }
            Write16(buf, ready);
            Write16(buf, users);
            Write8(buf, (uint8_t)net.num_connected_players);
            break;
        }
        case MP_HOST_SENDING:
        case MP_JOIN_GETTING_DATA:
            WriteStr8(buf, MOX::temp_string, sizeof(MOX::temp_string));
            break;
        case MP_HOST_PICK_POSITION:
        case MP_JOIN_PICK_POSITION:
            Write16(buf, m.field_id_begin);
            Write8(buf, MAX_PLAYERS);
            for (int i = 0; i < MAX_PLAYERS; i++) {
                const s_player& p = MOX::player[i];
                const bool shown = i < MOX::NUM_PLAYERS
                                   && p.objectives == PLAYER_OBJECTIVE_HUMAN && p.eliminated == 0;
                Write8(buf, (uint8_t)shown);
                Write8(buf, shown ? m.player_connected_flags[i] : 0);
                Write16(buf, shown ? m.player_field_ids[i] : (int16_t)-1000);
                Write16(buf, shown ? m.game_list_field_ids[i] : (int16_t)-1000);
            }
            break;
        case MP_JOIN_GAME_LIST: {
            Write16(buf, m.field_id_cancel);
            int32_t rows = router != nullptr ? netadapter::Game_Count(router) : 0;
            if (rows < 0) rows = 0;
            if (rows > 10) rows = 10;
            Write8(buf, (uint8_t)rows);
            for (int32_t i = 0; i < rows; i++) {
                const network::router::s_router_session_info* info =
                    netadapter::Ext_Game_Info(router, (uint32_t)i);
                Write16(buf, m.game_list_field_ids[i]);
                Write16(buf, info != nullptr ? (int16_t)info->current_players : 0);
                Write16(buf, info != nullptr ? (int16_t)info->max_players : 0);
                Write8(buf, info != nullptr ? (uint8_t)info->open : 0);
                WriteStr8(buf, info != nullptr ? info->session_name : nullptr,
                          sizeof(info->session_name));
            }
            break;
        }
        case MP_NET_NEXT_TURN: {
            WriteInputField(buf, m.player_field_ids[0], 80, MULTPLAY::global_chat_string,
                            sizeof(MULTPLAY::global_chat_string));
            Write16(buf, m.player_field_ids[1]);
            uint8_t humans = 0;
            for (int i = 0; i < MOX::NUM_PLAYERS && i < MAX_PLAYERS; i++) {
                if (MOX::player[i].objectives == PLAYER_OBJECTIVE_HUMAN
                    && MOX::player[i].eliminated == 0) humans++;
            }
            Write8(buf, humans);
            for (int i = 0; i < MOX::NUM_PLAYERS && i < MAX_PLAYERS; i++) {
                if (MOX::player[i].objectives == PLAYER_OBJECTIVE_HUMAN
                    && MOX::player[i].eliminated == 0) {
                    Write8(buf, (uint8_t)i);
                    Write8(buf, (uint8_t)(net.players_hit_next_turn[i] != 0));
                }
            }
            // Draw_Net_Next_Turn_Screen's lines, formatted as it prints them.
            const s_net_chat* chat = CHAT::chat_info;
            int32_t lines = chat != nullptr ? chat->count : 0;
            if (lines < 0) lines = 0;
            if (lines > 14) lines = 14;
            Write8(buf, (uint8_t)lines);
            for (int32_t i = 0; i < lines; i++) {
                const s_net_chat_msg& msg = chat->messages[i];
                char line[100];
                if (msg.player_id < MAX_PLAYERS) {
                    snprintf(line, sizeof(line), "(%s)  %.*s", MOX::player[msg.player_id].name,
                             (int)sizeof(msg.text), msg.text);
                } else {
                    snprintf(line, sizeof(line), "( GNN )  %.*s", (int)sizeof(msg.text), msg.text);
                }
                Write8(buf, msg.player_id);
                WriteStr8(buf, line, sizeof(line));
            }
            break;
        }
        default:
            break;
        }
    }

    // 19. The tactical battle's state: "CMBT", written ONLY while a battle is
    //     live (open fix 52's gate), and LAST. OrionLayer, open fix 53.
    //
    //     Combat works on MOX::combat_data alone and writes the losses back
    //     to ship[] only in End_Of_Combat (combinit.cpp), so nothing of a
    //     battle was on the wire: not the units, whose turn it is, the view
    //     origin every map click depends on, nor the legal moves. Every unit
    //     is sent FIELD BY FIELD — the packed s_combat_data's bytes before its
    //     picture pointer and after it (309 bytes; the offsets are asserted
    //     here, the struct has no size assert of its own). The legal moves
    //     are the cells Set_Legal_Moves marked 2000 (combat1.cpp), one bit
    //     per cell, x-major (bit x * 68 + y). The same snapshot's CMEV (open
    //     fix 56) carries every event recorded before this state.
    if (g_combat_live && MOX::combat_data != nullptr) {
        // Port onto orion2re v2.0.0: v2 widened x, y (int16), special_cooldown,
        // stasis_source_idx and black_hole_source_idx (uint16): 322 bytes, the
        // pointer at 202, the record 314 bytes (local: 317 / 198 / 309). The
        // record size below is on the wire; the field offsets after name/
        // ship_idx/owner differ from the local build's.
        static_assert(offsetof(s_combat_data, ship_image_seg) == 202,
                      "open fix 53: s_combat_data moved before its pointer");
        static_assert(offsetof(s_combat_data, drive_max_hits) == 210,
                      "open fix 53: s_combat_data moved after its pointer");
        static_assert(sizeof(s_combat_data) == 322,
                      "open fix 53: s_combat_data changed size");
        buf.push_back((uint8_t)'C');
        buf.push_back((uint8_t)'M');
        buf.push_back((uint8_t)'B');
        buf.push_back((uint8_t)'T');
        Write8(buf, 1);                                   // block version
        Write16(buf, (int16_t)g_combat_serial);
        Write16(buf, MOX::combat_turn);
        Write16(buf, MOX::combat_attacker_player);
        Write16(buf, MOX::combat_defender_player);
        Write16(buf, (int16_t)MOX::combat_init);
        Write16(buf, MOX::auto_combat_flag[0]);
        Write16(buf, MOX::auto_combat_flag[1]);
        Write16(buf, MOX::cur_ship);
        Write16(buf, MOX::cur_ptr);
        Write16(buf, MOX::cur_combat_x);
        Write16(buf, MOX::cur_combat_y);
        Write16(buf, MOX::in_nebula_flag);
        Write16(buf, MOX::cmbt_colony_id);
        Write16(buf, MOX::skan_flag);
        Write16(buf, MOX::board_flag);
        Write16(buf, MOX::super_fast_flag);
        // The turn order as Do_Combat_Turn built it, up to its -10
        // terminator (-1 = acted or captured); none found, none sent.
        int16_t order_n = 0;
        while (order_n < 210 && MOX::ship_turn_order[order_n] != -10) {
            ++order_n;
        }
        if (order_n >= 210) order_n = 0;
        Write8(buf, (uint8_t)order_n);
        for (int16_t i = 0; i < order_n; i++) {
            Write16(buf, MOX::ship_turn_order[i]);
        }
        int16_t units = MOX::combat_total_ship_count;
        if (units < 0) units = 0;
        if (units > 210) units = 210;
        Write16(buf, (int16_t)(offsetof(s_combat_data, ship_image_seg) + sizeof(s_combat_data) - offsetof(s_combat_data, drive_max_hits)));  // record size (v2: 314)
        Write16(buf, units);
        for (int16_t i = 0; i < units; i++) {
            const uint8_t* unit = reinterpret_cast<const uint8_t*>(&MOX::combat_data[i]);
            WriteBytes(buf, unit, offsetof(s_combat_data, ship_image_seg));
            WriteBytes(buf, unit + offsetof(s_combat_data, drive_max_hits),
                       sizeof(s_combat_data) - offsetof(s_combat_data, drive_max_hits));
        }
        // Port onto v2: the grid is MOX::combat_grid_width x combat_grid_height
        // (taller than 68 when a side has more than 100 ships). The wire keeps
        // the original 81 x 68 bitmap; cells past it are not sent.
        uint8_t legal[(ORIGINAL_COMBAT_GRID_WIDTH * ORIGINAL_COMBAT_GRID_HEIGHT + 7) / 8] = {};
        const int legal_w = MOX::combat_grid_width < ORIGINAL_COMBAT_GRID_WIDTH ? MOX::combat_grid_width : ORIGINAL_COMBAT_GRID_WIDTH;
        const int legal_h = MOX::combat_grid_height < ORIGINAL_COMBAT_GRID_HEIGHT ? MOX::combat_grid_height : ORIGINAL_COMBAT_GRID_HEIGHT;
        for (int x = 0; x < legal_w; x++) {
            for (int y = 0; y < legal_h; y++) {
                if (MOX::combat_grid[x][y] == 2000) {
                    const int bit = x * ORIGINAL_COMBAT_GRID_HEIGHT + y;
                    legal[bit >> 3] |= (uint8_t)(1u << (bit & 7));
                }
            }
        }
        WriteBytes(buf, legal, sizeof(legal));
    }

    // 20. The ordnance in flight: "CMSL", written ONLY while a battle is live,
    //     after CMBT. OrionLayer, open fix 55.
    //
    //     Missiles, torpedoes and fighters live in MOX::missile (300 x
    //     s_missile, 0x1A bytes each — sizes.h asserts it); an entry is live
    //     while its type is above 0 (the loop in COMBINIT::Tactical_Combat).
    //     Each live entry as the struct holds it, with its index — the index
    //     is what missile_field[] and a missile's target refer to.
    if (g_combat_live && MOX::missile != nullptr) {
        int16_t live = 0;
        for (int i = 0; i < 300; i++) {
            if (MOX::missile[i].type > 0) ++live;
        }
        buf.push_back((uint8_t)'C');
        buf.push_back((uint8_t)'M');
        buf.push_back((uint8_t)'S');
        buf.push_back((uint8_t)'L');
        Write8(buf, 1);                                   // block version
        Write16(buf, (int16_t)g_combat_serial);
        Write8(buf, (uint8_t)sizeof(s_missile));
        Write16(buf, live);
        for (int16_t i = 0; i < 300; i++) {
            if (MOX::missile[i].type > 0) {
                Write16(buf, i);
                WriteBytes(buf, &MOX::missile[i], sizeof(s_missile));
            }
        }
    }

    // 21. How the last battle ended: "CRES", written while a result is kept
    //     (from just before End_Of_Combat until the next battle starts), on
    //     ANY screen, after CMSL. OrionLayer, open fix 57.
    if (g_combat_result.valid) {
        const CombatResultView& r = g_combat_result;
        buf.push_back((uint8_t)'C');
        buf.push_back((uint8_t)'R');
        buf.push_back((uint8_t)'E');
        buf.push_back((uint8_t)'S');
        Write8(buf, 1);                                   // block version
        Write16(buf, (int16_t)r.number);
        Write16(buf, (int16_t)r.serial);
        Write8(buf, r.fought);
        Write16(buf, r.winner);
        Write16(buf, r.turn);
        Write16(buf, r.star);
        Write16(buf, r.colony);
        Write16(buf, r.attacker);
        Write16(buf, r.defender);
        Write16(buf, r.colony_pop_lost);
        Write32(buf, r.stardate);
        Write16(buf, r.units);
        for (int16_t i = 0; i < r.units; i++) {
            const CombatResultUnit& u = r.unit[i];
            Write16(buf, u.ship_idx);
            Write8(buf, (uint8_t)u.owner);
            Write8(buf, u.previous_owner);
            Write8(buf, (uint8_t)u.unit_status);
            Write8(buf, (uint8_t)u.is_retreating);
            Write8(buf, (uint8_t)u.is_captured);
            Write16(buf, u.structure_damage);
            Write16(buf, u.armor_remaining);
            Write16(buf, u.structure_max);
        }
    }

    // 22. What the acting unit's weapons can hit now: "CTGT", written ONLY
    //     while a battle is live and a unit acts, after CRES.
    //     OrionLayer, open fix 54.
    //
    //     The engine's own verdict, as CMBTFIRE::Fire_Ship takes it for each
    //     weapon slot (cmbtfire.cpp): the target inside the slot's arc
    //     (Get_Weapon_Arc of Relative_Bearing / _XY_), ammunition by the
    //     slot's type (a torpedo fires with ammo 0, anything else needs ammo),
    //     Weapon_In_Range and Target_Is_Legal_For_Weapon. All of them only
    //     read (work order 194 read each and every helper they call: no write,
    //     no random number). NOT in it: whether the player has the slot
    //     switched on (`active`, CMBT has it) and the reload Fire_Ship does
    //     on its way (shots_left / status, CMBT has both). Per unit, then per
    //     live missile: one byte, bit k = slot k can hit it.
    if (g_combat_live && MOX::combat_data != nullptr && MOX::missile != nullptr &&
        MOX::cur_ship >= 0 && MOX::cur_ship < MOX::combat_total_ship_count &&
        MOX::combat_total_ship_count <= 210) {
        const int16_t src = MOX::cur_ship;
        const int16_t units = MOX::combat_total_ship_count;
        buf.push_back((uint8_t)'C');
        buf.push_back((uint8_t)'T');
        buf.push_back((uint8_t)'G');
        buf.push_back((uint8_t)'T');
        Write8(buf, 1);                                   // block version
        Write16(buf, (int16_t)g_combat_serial);
        Write16(buf, src);
        auto slot_mask = [&](int16_t target, int16_t flag, uint8_t arc) {
            uint8_t mask = 0;
            for (int16_t k = 0; k < 8; k++) {
                const s_combat_weapon_slot& w = MOX::combat_data[src].weapons[k];
                if (w.weapon_id == 0 || (arc & w.arc) == 0) continue;
                const bool torpedo = TECHDATA::weapons[w.weapon_id].type == WEAPON_TYPE_TORPEDO;
                if (torpedo ? w.ammo != 0 : w.ammo == 0) continue;
                if (CMBTFIRE::Weapon_In_Range(k, src, target, flag, 0) != 0 &&
                    CMBTFIRE::Target_Is_Legal_For_Weapon(w.weapon_id, w.specials, target, flag) != 0) {
                    mask |= (uint8_t)(1u << k);
                }
            }
            return mask;
        };
        Write16(buf, units);
        for (int16_t t = 0; t < units; t++) {
            uint8_t mask = 0;
            if (t != src && MOX::combat_data[t].unit_status == 0 &&
                (t != 0 || MOX::cmbt_colony_id != -1)) {
                mask = slot_mask(t, 0, CMBTDRW1::Get_Weapon_Arc(CMBTDRW1::Relative_Bearing(src, t)));
            }
            Write8(buf, mask);
        }
        int16_t live = 0;
        for (int i = 0; i < 300; i++) {
            if (MOX::missile[i].type > 0) ++live;
        }
        Write16(buf, live);
        for (int16_t m = 0; m < 300; m++) {
            if (MOX::missile[m].type <= 0) continue;
            Write16(buf, m);
            Write8(buf, slot_mask(m, 1, CMBTDRW1::Get_Weapon_Arc(CMBTDRW1::Relative_Bearing_XY(
                src, MOX::missile[m].x, MOX::missile[m].y))));
        }
    }

    // 23. The galaxy map fleet box's scroll row: "FBSC", written ONLY while
    //     the fleet box (moveable box 2) shows a stack, after CTGT (work
    //     order 191's item 1). OrionLayer, open fix 59.
    //
    //     FLEETPOP builds its ship cells from fltpop_scroll_bar's
    //     first_visible_row (fleetpop.cpp: first_fleet_movement_icon = row
    //     * 3) and resets it when a new stack opens (Initialize_Scroll_Bar);
    //     SerializeState sent only MOX::scroll_bar, a different bar. So a
    //     client could not say which three rows of a stack past nine ships
    //     the box shows. The row, the bar's rows, and its two buttons' fields.
    if (current_screen == SCREEN_MAIN && MOVEBOX::Moveable_Box_Selected(2) &&
        MOX::fleet_box_ship_stack >= 0 &&
        MOX::fleet_box_ship_stack < MOX::ship_stack_count) {
        const s_scroll_bar& bar = MOX::fltpop_scroll_bar;
        buf.push_back((uint8_t)'F');
        buf.push_back((uint8_t)'B');
        buf.push_back((uint8_t)'S');
        buf.push_back((uint8_t)'C');
        Write8(buf, 1);                                   // block version
        Write16(buf, MOX::fleet_box_ship_stack);
        Write16(buf, bar.first_visible_row);
        Write16(buf, bar.visible_rows);
        Write16(buf, bar.total_rows);
        Write16(buf, bar.up_btn);
        Write16(buf, bar.dn_btn);
    }

    // 24. What happened in the battle since the last snapshot: "CMEV", written
    //     on ANY screen while there are events not yet sent (a battle's last
    //     events drain into the first snapshot after it), after FBSC.
    //     OrionLayer, open fix 56.
    //
    //     Numbered from 1 across battles; `first` above the client's next
    //     number is a gap (the ring holds 1024, or no client was connected).
    //     Every state block in the SAME snapshot (CMBT, CMSL, CRES) is the
    //     battle after all of these — so a client that animates plays them,
    //     then shows that state.
    if (g_combat_event_sent < g_combat_event_next) {
        uint32_t first = g_combat_event_sent;
        if (g_combat_event_next - first > 1024) first = g_combat_event_next - 1024;
        const uint16_t n = (uint16_t)(g_combat_event_next - first);
        buf.push_back((uint8_t)'C');
        buf.push_back((uint8_t)'M');
        buf.push_back((uint8_t)'E');
        buf.push_back((uint8_t)'V');
        Write8(buf, 1);                                   // block version
        Write32(buf, (int32_t)first);
        Write16(buf, (int16_t)n);
        for (uint32_t s = first; s < g_combat_event_next; s++) {
            const CombatEventRecord& e = g_combat_events[s % 1024];
            Write16(buf, (int16_t)e.serial);
            Write8(buf, e.type);
            for (int k = 0; k < 8; k++) Write16(buf, e.a[k]);
        }
        g_combat_event_sent = g_combat_event_next;
    }

    // 25. The battle's scan view and board popup: "CPOP", written ONLY while
    //     one of them is up in a battle (66 or 67, open fix 52's ids), after
    //     CMEV. OrionLayer, open fix 66.
    //
    //     The scan view draws MOX::global_combat_data, the copy of the unit
    //     Display_Combat_View_Ship took (cmbtdrw1.cpp:1549-1551), and keeps
    //     its index in ship_data_id; the board popup keeps its message, its
    //     range and the chosen marines in CMBTDRW1's g_msg/g_min/g_max/
    //     g_var (cmbtdrw1.cpp:1581-1666). None of it was on the wire, so a
    //     client could not say which unit is scanned, nor show the popup, nor
    //     see the effect of an arrow. Read only; nothing here changes the game.
    if (g_combat_live &&
        (current_screen == SCREEN_EXT_COMBAT_SCAN || current_screen == SCREEN_EXT_COMBAT_BOARD)) {
        const bool board = current_screen == SCREEN_EXT_COMBAT_BOARD;
        const char* msg = (board && CMBTDRW1::g_msg != nullptr) ? CMBTDRW1::g_msg : "";
        const size_t len = strnlen(msg, 255);
        buf.push_back((uint8_t)'C');
        buf.push_back((uint8_t)'P');
        buf.push_back((uint8_t)'O');
        buf.push_back((uint8_t)'P');
        Write8(buf, 1);                                   // block version
        Write16(buf, current_screen);
        Write16(buf, (int16_t)g_combat_serial);
        Write16(buf, board ? (int16_t)-1 : MOX::ship_data_id);
        Write16(buf, board ? CMBTDRW1::g_min : (int16_t)0);
        Write16(buf, board ? CMBTDRW1::g_max : (int16_t)0);
        Write16(buf, board ? CMBTDRW1::g_var : (int16_t)0);
        Write8(buf, (uint8_t)len);
        buf.insert(buf.end(), msg, msg + len);
    }

    // 26. The battle's OPTIONS panel: "COPT", written ONLY while a battle is
    //     live, after CPOP. OrionLayer, open fix 68.
    //
    //     OPTIONS (the info button, hotkey O) flips MOX::order_info_flag and
    //     slides COMBAT.LBX 9 over the buttons (Move_Options_Background,
    //     cmbtdrw1.cpp:1463); its five lights are MOX's flags for the missile
    //     warning, fast animations, legal moves, shield arcs and the grid
    //     (Draw_Option_Panel_Lights, :1307-1322), toggled by five hidden
    //     fields (combat1.cpp:465-495). The flags are copied from settings
    //     when the battle starts (combinit.cpp:529-534) and back only when
    //     it ends (:1177-1178), so the settings record on the wire is stale
    //     for the whole battle, and neither the panel nor the flags were on
    //     the wire. Read only; nothing here changes the game.
    if (g_combat_live && MOX::combat_data != nullptr) {
        buf.push_back((uint8_t)'C');
        buf.push_back((uint8_t)'O');
        buf.push_back((uint8_t)'P');
        buf.push_back((uint8_t)'T');
        Write8(buf, 1);                                   // block version
        Write16(buf, (int16_t)g_combat_serial);
        Write16(buf, MOX::order_info_flag);              // 0: the panel is up
        Write8(buf, (uint8_t)MOX::msl_impact_warning_flag);
        Write8(buf, (uint8_t)MOX::speedx2_flag);
        Write8(buf, (uint8_t)MOX::draw_legal_boxes_flag);
        Write8(buf, (uint8_t)MOX::shields_flag);
        Write8(buf, (uint8_t)MOX::grid_flag);
    }

    // 27. The help list a right click is checked against now: "HLPL",
    //     written while one is active, after COPT. OrionLayer, open fix 69.
    //
    //     A right click first walks the active list (Check_Help_List,
    //     fields.cpp:2935-2954; first entry with an id under the pointer
    //     wins, an id of -1 is passed over) and shows that HELP.LBX entry.
    //     Many lists are built at run time from the game's state — the
    //     audience's per menu (Setup_Diplomacy_Help, dip_scrn_main.cpp:
    //     1966-1993), the battle's per weapon (combat1.cpp:2402-2590), the
    //     popups that inherit their caller's — and none was on the wire. A
    //     message or text box switches help off while it is up (gendraw.cpp:
    //     60, textbox.cpp:97), so the block is absent then. Read only;
    //     nothing here changes the game.
    if (fields::help_list_active != 0 && fields::help_struct_pointer &&
        fields::help_list_count > 0) {
        int16_t n = fields::help_list_count;
        if (n > 64) {
            n = 64;
        }
        buf.push_back((uint8_t)'H');
        buf.push_back((uint8_t)'L');
        buf.push_back((uint8_t)'P');
        buf.push_back((uint8_t)'L');
        Write8(buf, 1);                                   // block version
        Write16(buf, n);
        for (int16_t i = 0; i < n; i++) {
            const s_help_box& h = fields::help_struct_pointer[i];
            Write16(buf, h.help_id);
            Write16(buf, h.x1);
            Write16(buf, h.y1);
            Write16(buf, h.x2);
            Write16(buf, h.y2);
        }
    }
}

// ── Field list ───────────────────────────────────────────

static void SerializeFields(std::vector<uint8_t>& buf) {
    buf.clear();
    int16_t count = fields::fields_count;
    Write16(buf, count);

    for (int i = 0; i < count; i++) {
        const s_field& f = fields::fields[i];
        Write16(buf, (int16_t)i);        // field index
        Write16(buf, f.x);
        Write16(buf, f.y);
        Write16(buf, f.x_end);
        Write16(buf, f.y_end);
        Write16(buf, f.field_type);
        Write8(buf, (uint8_t)f.hotkey);
    }
}

// ── Save slots ───────────────────────────────────────────

/// The GAME popup's slot list, exactly as its Load and Save dialogs
/// draw it: status and game type per slot, and the three strings the
/// engine formatted itself. Sent so a client never reads SAVEn.GAM
/// from a folder of its own, which could belong to a different game
/// than the slots a click reaches.
///
/// WHEN THE DATA IS VALID. `LOADSAVE::game_popup_fields` and the two
/// date buffers are allocated per popup (`Allocate_Data_Space_For_
/// Game_Popup_`, loadsave.cpp:1545) and filled for `screen_data` 2
/// and 3 by `Set_Up_Load_Save_Popup` and `Add_Game_Popup_Fields`
/// (:1571-1578) with no `Get_Input` in between, so a tick that sees
/// screen 8 with `screen_data` 2 or 3 sees them filled. The first tick
/// at screen 8 runs before the popup allocates (mox2.cpp:41), and there
/// `screen_data` is 0 — the GAME handler sets it (mainscr_main.cpp:612)
/// — so a pointer left over from an earlier popup is never followed.
static bool SerializeSaveSlots(std::vector<uint8_t>& buf,
                               int16_t current_screen) {
    buf.clear();
    // OrionLayer change, Open Fix 34 (orionlayerv3 doc/orion2re_open_fixes.md):
    // the main menu's Load dialog (screen_data 2) sends its save slots too.
    const bool main_menu_load = current_screen == SCREEN_MAIN_MENU
                                && MOX::screen_data == 2;
    if (current_screen != SCREEN_GAME && !main_menu_load) {
        return false;
    }
    const int16_t screen_data = MOX::screen_data;
    if (screen_data != 2 && screen_data != 3) {
        return false;
    }
    const s_game_popup_fields* popup = LOADSAVE::game_popup_fields;
    if (popup == nullptr || MOX::save_game_dates == nullptr
        || MOX::save_game_stardates == nullptr) {
        return false;
    }
    Write8(buf, (uint8_t)screen_data);
    Write8(buf, 10);
    for (int i = 0; i < 10; i++) {
        Write8(buf, (uint8_t)popup->slot_status[i]);
        Write8(buf, (uint8_t)popup->saved_game_types[i]);
        WriteBytes(buf, MOX::save_game_description[i].description,
                   sizeof(MOX::save_game_description[i].description));
        WriteBytes(buf, MOX::save_game_stardates + i * 25, 25);
        WriteBytes(buf, MOX::save_game_dates + i * 25, 25);
    }
    return true;
}

// ── Visual frame ─────────────────────────────────────────

static void SerializeVisual(std::vector<uint8_t>& buf) {
    buf.clear();
    // Framebuffer: 640×480×8bit = 307200 bytes
    constexpr size_t FB_SIZE = 640 * 480;
    if (platform::g_present_surface != nullptr) {
        WriteBytes(buf, platform::g_present_surface, FB_SIZE);
    } else {
        buf.resize(FB_SIZE, 0);
    }
    // Palette: 256 × 3 bytes (RGB)
    for (int i = 0; i < 256; i++) {
        Write8(buf, video::g_palette_entries[i].r);
        Write8(buf, video::g_palette_entries[i].g);
        Write8(buf, video::g_palette_entries[i].b);
    }
}

// ── The pop-move command ─────────────────────────────────

/// Apply a list of (pop_idx, new_job) to ONE colony, or apply none.
///
/// **WHY THIS IS NOT A CLICK CHAIN.** Fundament 52. Addressing a
/// move by clicks costs four round trips (RESORT, ESTABLISH, PICK,
/// DROP — 597 of the 725 ms measured per drop) and needs the game's
/// own ten-row window to be steered first. Every one of those exists
/// only because a click names a SLOT and this names a colony.
///
/// **WHY THE RULES ARE NOT RE-IMPLEMENTED HERE.** They stay in
/// `COLMOVE::Give_Colonist_New_Job`, which is called unchanged.
/// A second copy of four refusal conditions in our own directory is
/// the duplicate this project keeps paying for; what the flag below
/// removes is the message box, never a check.
///
/// **WHY ALL-OR-NOTHING.** `Send_Cluster` stops at the first
/// refusal and leaves the earlier pops moved (colmove.cpp:168-173),
/// because a human is standing at the message box and can decide
/// whether to go on. A command has nobody to ask, so it does not
/// start: the colony's pop words are saved, the list is applied, and
/// a single refusal puts every one of them back. This is a marked
/// DEVIATION from the original and it is fundament 52's, not this
/// file's.
///
/// **THE SAVE COVERS THE WHOLE ARRAY, not just the listed pops.**
/// Forty-two words is nothing, and a rollback that covered less
/// than the call can touch would be a rollback with a hole in it:
/// `Give_Colonist_New_Job` is the engine's function, not ours, and
/// what it writes is its business to change. Saving only the pops
/// we name would be an assumption about somebody else's code that
/// nothing here would notice going stale.
///
/// Returns the number of pops moved, or -1 if the list was refused.
static int Set_Jobs_(int16_t colony_idx, const uint8_t* pairs,
                     int count) {
    // MOX::colony is a POINTER, not a fixed array (mox.h:616), so
    // the bound is the engine's own count (mox.h:381) and not a
    // constant we could get wrong on our side.
    if (colony_idx < 0 || colony_idx >= MOX::NUM_COLONIES) {
        return -1;
    }
    s_colony& colony = MOX::colony[colony_idx];
    // Only the local player's own colonies. The original reaches
    // this code from a screen that cannot show anyone else's, so the
    // guard is the API's to add, not a transcription.
    if (colony.owner != MOX::PLAYER_NUM || colony.n_pops == 0) {
        return -1;
    }
    const int n_pops = (int)(uint8_t)colony.n_pops;

    // AND THE GAME MUST NOT BE HOLDING A CLUSTER. Those pops have
    // their assigned bit clear and belong to a gesture in progress;
    // writing pop[] underneath one would strand them, and the
    // rollback below would then restore a state the gesture no
    // longer matches. With the click chain gone from the HD client
    // this can only be a human at the game's own window, which
    // platform.cpp:1379 hides while the API is on — so it is a
    // precondition that should never fire, stated rather than
    // assumed.
    if (COLMOVE::cluster_colony_n != -1) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        const int pop_idx = pairs[i * 2];
        const int new_job = pairs[i * 2 + 1];
        if (pop_idx >= n_pops || new_job > ECON_RESEARCH) {
            return -1;
        }
    }

    uint32_t saved[42];
    for (int i = 0; i < n_pops; i++) {
        saved[i] = colony.pop[i];
    }

    // THE PICK-UP, transcribed. `Get_Cluster` clears the assigned
    // bit on every pop it takes (colmove.cpp:60-100) and the walk in
    // `Send_Cluster` then re-flags them one at a time, so the caps
    // in `Sum_Colonists` do not count a pop that is in the air.
    // Clearing the whole list first is what makes a two-way swap
    // inside one list behave the way the same swap behaves as two
    // drops.
    for (int i = 0; i < count; i++) {
        colony.pop[pairs[i * 2]] &= ~POP::MASK_ASSIGNED;
    }

    COLMOVE::_ext_suppress_refusal_help = true;
    int moved = 0;
    bool refused = false;
    for (int i = 0; i < count && !refused; i++) {
        const int16_t pop_idx = (int16_t)pairs[i * 2];
        const int16_t new_job = (int16_t)pairs[i * 2 + 1];
        // A pop already in the job it was asked for is re-flagged
        // and NOT rule-checked — colmove.cpp:165-166, and the reason
        // an android returning to its own column lands where rule 2
        // would refuse it. Transcribed, not tidied.
        if ((int16_t)POP::Get_Prof(colony.pop[pop_idx]) == new_job) {
            colony.pop[pop_idx] |= POP::MASK_ASSIGNED;
            continue;
        }
        if (COLMOVE::Give_Colonist_New_Job(colony_idx, pop_idx,
                                            new_job, false) == 0) {
            refused = true;
        } else {
            moved++;
        }
    }
    COLMOVE::_ext_suppress_refusal_help = false;

    if (refused) {
        for (int i = 0; i < n_pops; i++) {
            colony.pop[i] = saved[i];
        }
        return -1;
    }

    // ONCE, AFTER THE WHOLE LIST. `Send_Cluster` recalculates after
    // its loop and not inside it (colmove.cpp:461-464), so a list is
    // the original's own grouping and not a shortcut.
    COLONY::Col_Calc_Wrapper(colony_idx);
    return moved;
}

// ── Fleet box selection (OrionLayer, open fix 21) ────────

// Select or deselect ONE ship of the stack the fleet box shows, the
// way a human painting one icon cell does
// (MAINSCR::Set_Painted_Fleet_Fields, mainscr.cpp:2468): the same
// Ship_Can_Be_Selected test, the same ship_node[].selected write,
// and the bit Save_Ship_Selection_Status keeps across a stack rebuild
// (fleetpop.cpp:356-375). EVERY precondition is checked before
// anything is written, so a refused command changes nothing. Unlike a
// painted cell it reaches a ship scrolled out of the box's nine icons;
// selecting it after scrolling is the same state.
// Returns 1 applied, 0 refused.
static int Select_Ship_(int16_t ship_idx, int16_t selected) {
    if (!MOVEBOX::Moveable_Box_Selected(2)) {
        return 0;
    }
    const int16_t stack = MOX::fleet_box_ship_stack;
    if (stack < 0 || stack >= MOX::ship_stack_count) {
        return 0;
    }
    if (ship_idx < 0 || ship_idx >= MOX::NUM_SHIPS) {
        return 0;
    }
    if ((int16_t)(int8_t)MOX::ship[ship_idx].owner != MOX::PLAYER_NUM) {
        return 0;
    }
    if (!MAINSCR::Ship_Can_Be_Selected(ship_idx)) {
        return 0;
    }
    int16_t node = MOX::ship_stack_start[stack];
    for (int guard = 0; node >= 0 && node < MAX_SHIPS && guard < MAX_SHIPS;
         guard++) {
        if (MOX::ship_node[node].ship_idx == ship_idx) {
            MOX::ship_node[node].selected = (int8_t)(selected != 0 ? 1 : 0);
            if (selected != 0) {
                struct_::Set_Bit_Field(MOX::fleet_icon_selection_status,
                                        (uint16_t)ship_idx);
            } else {
                struct_::Clear_Bit_Field(MOX::fleet_icon_selection_status,
                                          (uint16_t)ship_idx);
            }
            return 1;
        }
        node = MOX::ship_node[node].next_node;
    }
    return 0;
}

/// Select or deselect one ship ON THE FLEET SCREEN. OrionLayer, open
/// fix 28 — the write half of 27, and a SEPARATE function from
/// Select_Ship_ above because the two screens keep their selection in
/// different arrays.
///
/// Open fix 21's Select_Ship_ cannot serve this screen on either of two
/// counts: it refuses unless Moveable_Box_Selected(2), and the fleet
/// screen closes that box; and it writes ship_node[].selected, which
/// this screen never reads — it paints from fltscrn_big_icon[].selected
/// (flt1.cpp:429).
///
/// Nor can a click do it. The per-ship toggle is painted in the DRAW
/// pass, which tests mouse::Mouse_Button() for the LIVE button state
/// (flt1.cpp:413-436); an injected click is a buffered event and the
/// draw pass has already seen the button go up. ACTIVATE_FIELD on a big
/// icon reaches Scan_Fltscrn_Big_Icons result 0, which only SCANS
/// (flt2.cpp:924-928, flt1.cpp:615-620). Without this, ALL is the only
/// way a selection can change, so a subset of a stack cannot be moved
/// or scrapped from outside at all.
///
/// The checks are the original's own, in its order: the screen is up,
/// the stack shown is ours (flt1.cpp:1185, :1193), painting is not
/// switched off (relocate_button_mode != 1, flt1.cpp:413), and the ship
/// is IN the list the screen built (flt2.cpp:207-268). The write is the
/// single line ALL writes (Set_Fltscrn_Big_Icons, flt1.cpp:1610-1624).
/// Nothing else is maintained here on purpose: the loop's own
/// Update_Selection_Flags (flt1.cpp:811, :1069-1093) recounts
/// n_big_icons_selected and mirrors fltscrn_icon_selection_status on
/// the next iteration, which is exactly what it does after ALL.
/// Returns 1 applied, 0 refused.
static int Select_Fltscrn_Ship_(int16_t ship_idx, int16_t selected) {
    if (MOX::current_screen != SCREEN_FLEET) {
        return 0;
    }
    if (MOX::PLAYER_NUM != MOX::fltscrn_stack_owner) {
        return 0;
    }
    if (FLT1::relocate_button_mode == 1) {
        return 0;
    }
    if (MOX::fltscrn_big_icon == nullptr) {
        return 0;
    }
    int16_t icons = MOX::n_fltscrn_big_icons;
    if (icons > SHIP_LIST_CAPACITY) {
        icons = SHIP_LIST_CAPACITY;
    }
    for (int16_t i = 0; i < icons; i++) {
        if (MOX::fltscrn_big_icon[i].ship_idx == ship_idx) {
            MOX::fltscrn_big_icon[i].selected =
                (int8_t)(selected != 0 ? 1 : 0);
            return 1;
        }
    }
    return 0;
}

// ── Input injection ──────────────────────────────────────

// OrionLayer, open fix 64. The Races screen's spies and missions set as one list, all or nothing.
// The original moves spies by picking icons with the POINTER (a pick takes
// as many as the pointer's x says, racescrn.cpp Draw_Icon_Group_Cursor)
// and dropping them on another group; a mission is a multi-button written
// on a mouse click; both reach the player record only when the screen is
// left (Update_Spy_Stuff). A client cannot aim the pointer, and could not
// see the result. Applied only while the screen is up and nothing is in
// hand; every race the screen shows must be listed, no group above 63
// (Move_Spy_Group_From_Mouse), the total kept, a mission 1-3 (or 0: kept).
// Written into the screen's groups AND the record (the setters the
// screen's own commit uses), so the snapshot shows it and the later commit
// writes the same numbers.
static void Set_Spies_(const std::vector<uint8_t>& p) {
    using namespace RACESCRN;
    if (MOX::current_screen != SCREEN_RACE || g_display_p == nullptr ||
        g_agent_group_p == nullptr || g_mouse_group_p == nullptr ||
        g_mouse_group_p->count != 0 || p.size() < 2) return;
    const int n = p[0];
    if ((int)p.size() != 2 + 3 * n) return;
    int want[8], mission[8];
    int eligible = 0, listed = 0, total_now = g_agent_group_p->count;
    for (int i = 0; i < 8; ++i) want[i] = mission[i] = -1;
    for (int i = 0; i < MOX::active_count && i < 8; ++i) {
        const s_racescrn_race_display& d = g_display_p[i];
        if (d.is_empty_slot || d.is_eliminated) continue;
        ++eligible;
        total_now += d.spy_icons_group.count;
    }
    const int agents = p[1 + 3 * n];
    int total_req = agents;
    if (agents > 63) return;
    for (int k = 0; k < n; ++k) {
        const int player = p[1 + 3 * k], spies = p[2 + 3 * k], m = p[3 + 3 * k];
        int at = -1;
        for (int i = 0; i < MOX::active_count && i < 8; ++i) {
            const s_racescrn_race_display& d = g_display_p[i];
            if (!d.is_empty_slot && !d.is_eliminated && d.player_idx == player) at = i;
        }
        if (at < 0 || want[at] >= 0 || spies > 63 || m > 3) return;
        want[at] = spies;
        mission[at] = m;
        total_req += spies;
        ++listed;
    }
    if (listed != eligible || total_req != total_now) return;
    for (int i = 0; i < MOX::active_count && i < 8; ++i) {
        if (want[i] < 0) continue;
        s_racescrn_race_display& d = g_display_p[i];
        d.spy_icons_group.count = (uint16_t)want[i];
        Calculate_Icon_Group_Data(&d.spy_icons_group);
        BILL::Set_My_Spy_Number(d.player_idx, (uint8_t)want[i]);
        if (mission[i] > 0) {
            d.agent_mission_idx = (int16_t)mission[i];
            BILL::Set_My_Spy_Mission(d.player_idx, (int8_t)(mission[i] - 1));
        }
    }
    g_agent_group_p->count = (uint16_t)agents;
    Calculate_Icon_Group_Data(g_agent_group_p);
    BILL::Set_My_Agent_Number((uint8_t)agents);
}

static void ProcessInput() {
    InputCommand cmd;
    while (g_server.PopInput(cmd)) {
        switch (cmd.type) {
        case MSG_ACTIVATE:
            // Queue field activation for Get_Input() early return.
            // Do NOT set last_button_number directly — it gets
            // overwritten by Interpret_Mouse_Input() on normal clicks.
            if (cmd.param1 >= 0 && cmd.param1 < fields::fields_count) {
                g_pending_field = cmd.param1;
                // OrionLayer, open fix 62. A client that names the field's rect gets it checked where the field is taken.
                g_pending_checked = cmd.pairs.size() == 8;
                if (g_pending_checked) {
                    memcpy(g_pending_rect, cmd.pairs.data(), 8);
                }
            }
            break;

        case MSG_INJECT_KEY:
            // Push a keyboard scancode into the SDL event queue
            {
                SDL_Event event{};
                event.type = SDL_EVENT_KEY_DOWN;
                event.key.scancode = (SDL_Scancode)cmd.param1;
                event.key.key = (SDL_Keycode)cmd.param1;
                event.key.down = true;
                SDL_PushEvent(&event);
            }
            break;

        case MSG_INJECT_CLICK:
            // Push a mouse click at (x, y) in 640×480 space.
            // Must send both DOWN and UP — the game's input loop
            // waits for button release before returning.
            {
                // Hold the real-mouse sync off until these are
                // consumed. Without it, Enqueue_Mouse_Input_Event's
                // coalescing rewrites the injected RELEASE to the
                // physical mouse position, and the pointer has walked
                // away before the game reads it.
                g_injected_mouse_pending = true;

                // The comment above has always said "in 640x480
                // space" and until now that was a promise the code
                // did not keep: the point was pushed raw and read
                // back as a WINDOW coordinate.
                float wx = 0.0f;
                float wy = 0.0f;
                Game_Point_To_Window_Point_((float)cmd.param1,
                                            (float)cmd.param2,
                                            &wx, &wy);

                SDL_Event down{};
                down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                down.button.button = SDL_BUTTON_LEFT;
                down.button.x = wx;
                down.button.y = wy;
                down.button.down = true;
                SDL_PushEvent(&down);

                SDL_Event up{};
                up.type = SDL_EVENT_MOUSE_BUTTON_UP;
                up.button.button = SDL_BUTTON_LEFT;
                up.button.x = wx;
                up.button.y = wy;
                up.button.down = false;
                SDL_PushEvent(&up);
            }
            break;

        case MSG_SET_JOBS:
            // Written straight from the drain loop. Unlike
            // MSG_ACTIVATE this returns nothing to the game's input
            // machinery, so it needs no g_pending_field: that
            // indirection exists because Get_Input has to RETURN a
            // field number and Interpret_Mouse_Input would
            // overwrite last_button_number. Nothing here is read
            // back by Get_Input. ProcessInput runs from
            // fields::Get_Input (fields.cpp:167) and the colony
            // summary draws right after it (colsum.cpp:159, :171),
            // so the write lands in the same loop iteration.
            Set_Jobs_(cmd.param1, cmd.pairs.data(), (int)cmd.param2);
            break;

        case MSG_SELECT_SHIP:
            // Open fix 21 for the galaxy map's fleet box, open fix 28
            // for the fleet screen. ONE message id, because it is one
            // question — "select this ship" — and two handlers, because
            // the two screens keep the answer in different arrays. The
            // branch is on the screen and not on which handler happens
            // to succeed: a silent fall-through would make a refusal on
            // one screen look like an attempt on the other.
            // From the drain loop like MSG_SET_JOBS; whichever array is
            // written, the screen reads it when it next draws.
            if (MOX::current_screen == SCREEN_FLEET) {
                Select_Fltscrn_Ship_(cmd.param1, cmd.param2);
            } else {
                Select_Ship_(cmd.param1, cmd.param2);
            }
            break;

        case MSG_SHOW_WINDOW:
            // OrionLayer, open fix 43. Only the flag: the main thread shows or hides the window (platform.cpp).
            g_hide_window = (cmd.param1 == 0);
            break;

        case MSG_INJECT_RIGHT_CLICK:
            // OrionLayer, open fix 61. INJECT_CLICK with the right button: a grid field reads the point (the battle map's turn-to-face, combat1.cpp:696-710).
            {
                // CANCEL_FIELD right-clicks a field's CENTRE, which is
                // the whole answer for a button and the wrong one for a
                // grid field: the game reads xpos/ypos from the point.
                g_injected_mouse_pending = true;
                float wx = 0.0f;
                float wy = 0.0f;
                Game_Point_To_Window_Point_((float)cmd.param1,
                                            (float)cmd.param2,
                                            &wx, &wy);
                SDL_Event down{};
                down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                down.button.button = SDL_BUTTON_RIGHT;
                down.button.x = wx;
                down.button.y = wy;
                down.button.down = true;
                SDL_PushEvent(&down);
                SDL_Event up{};
                up.type = SDL_EVENT_MOUSE_BUTTON_UP;
                up.button.button = SDL_BUTTON_RIGHT;
                up.button.x = wx;
                up.button.y = wy;
                up.button.down = false;
                SDL_PushEvent(&up);
            }
            break;

        case MSG_SET_SPIES:
            Set_Spies_(cmd.pairs);  // OrionLayer, open fix 64.
            break;

        case MSG_COMBAT_COMMAND:
            // OrionLayer, open fix 58. Kept for the battle's next human input; a later one replaces it.
            if (cmd.pairs.size() >= 11) {
                const uint8_t* p = cmd.pairs.data();
                CombatCommand c = {};
                c.pending = true;
                memcpy(&c.serial, p, 2);
                memcpy(&c.unit, p + 2, 2);
                c.op = p[4];
                memcpy(&c.a, p + 5, 2);
                memcpy(&c.b, p + 7, 2);
                memcpy(&c.c, p + 9, 2);
                g_combat_cmd = c;
            }
            break;

        case MSG_CANCEL_FIELD:
            // Right-click on field (DOWN + UP)
            if (cmd.param1 >= 0 && cmd.param1 < fields::fields_count) {
                const s_field& f = fields::fields[cmd.param1];
                // The field rect is in game space, so this centre
                // needs the same conversion as an injected click —
                // it is the same defect reached by a different door.
                float cx = 0.0f;
                float cy = 0.0f;
                Game_Point_To_Window_Point_(
                    (float)((f.x + f.x_end) / 2),
                    (float)((f.y + f.y_end) / 2), &cx, &cy);

                SDL_Event down{};
                down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                down.button.button = SDL_BUTTON_RIGHT;
                down.button.x = cx;
                down.button.y = cy;
                down.button.down = true;
                SDL_PushEvent(&down);

                SDL_Event up{};
                up.type = SDL_EVENT_MOUSE_BUTTON_UP;
                up.button.button = SDL_BUTTON_RIGHT;
                up.button.x = cx;
                up.button.y = cy;
                up.button.down = false;
                SDL_PushEvent(&up);
            }
            break;
        }
    }
}

// ── Public API ───────────────────────────────────────────

bool Init(uint16_t port) {
    // OrionLayer, open fix 43. The window's visibility is the starter's and the client's, no longer forced here.
    g_last_screen = -99;
    g_last_stardate = -1;
    g_last_fields_count = -1;

    bool ok = g_server.Start(port);
    if (ok) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "ext: server started on port %d", port);
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ext: failed to start server on port %d", port);
    }
    return ok;
}

int16_t g_screen_override = -1;

ScreenOverride::ScreenOverride(int16_t id) : previous(g_screen_override) {
    if (id >= 0) {
        g_screen_override = id;
    }
}

ScreenOverride::~ScreenOverride() {
    g_screen_override = previous;
}

// OrionLayer, open fix 50. The Hall of Fame takes input (ext_api.h).
bool g_hof_live = false;

// OrionLayer, open fix 52. A tactical battle is up (ext_api.h).
bool g_combat_live = false;
uint16_t g_combat_serial = 0;
static int32_t g_battle_ticks = 0;  // OrionLayer, open fix 79. Ticks waited since the last event that is not a sound.

CombatGuard::CombatGuard() : screen(SCREEN_EXT_COMBAT), previous(g_combat_live) {
    g_combat_live = true;
    ++g_combat_serial;
    g_combat_result.valid = false;  // OrionLayer, open fix 57. A new battle: the last result goes.
}

// OrionLayer, open fix 56. Combat_Event: the ring above SerializeState (ext_api.h).
void Combat_Event(uint8_t type, int16_t a0, int16_t a1, int16_t a2, int16_t a3,
                  int16_t a4, int16_t a5, int16_t a6, int16_t a7) {
    if (!g_combat_live) return;
    CombatEventRecord& e = g_combat_events[g_combat_event_next % 1024];
    e = {g_combat_event_next, g_combat_serial, type, {a0, a1, a2, a3, a4, a5, a6, a7}};
    ++g_combat_event_next;
    if (type != CEV_SOUND && type != CEV_SOUND_CHANGE) g_battle_ticks = 0;  // OrionLayer, open fix 79. The ticks count from this event.
}

// OrionLayer, open fix 79. The battle's sounds as events (ext_api.h).
static bool Battle_Sound_Recorded_() {
    return g_combat_live && g_hide_window.load() && g_server.ClientCount() > 0;
}

static int16_t Battle_Ticks_() {
    return (int16_t)(g_battle_ticks > 32767 ? 32767 : g_battle_ticks);
}

bool Battle_Sound(int16_t sound_id, int32_t* handle) {
    if (!Battle_Sound_Recorded_()) return false;
    static int32_t next = 16;
    *handle = next;
    next = next >= 127 ? 16 : next + 1;
    if (sound_id != 0) Combat_Event(CEV_SOUND, sound_id, (int16_t)*handle, Battle_Ticks_());
    return true;
}

bool Battle_Sound_Change(int32_t handle, uint8_t change, int32_t value) {
    if (handle != -1 && (handle < 16 || handle > 127)) return false;
    if (!Battle_Sound_Recorded_()) return handle != -1;
    if (value < -32768) value = -32768;
    if (value > 32767) value = 32767;
    Combat_Event(CEV_SOUND_CHANGE, (int16_t)handle, change, (int16_t)value, Battle_Ticks_());
    return handle != -1;
}

void Battle_Wait(int32_t ticks) {
    if (g_combat_live && ticks > 0) g_battle_ticks += ticks;
}

// OrionLayer, open fix 65. The original's view kept round a commanded action, as a player's click keeps it.
// A clicking player can only choose a cell inside the original's view, which
// the game centres on the acting ship at its turn; a command can name any
// legal cell. Move_Ship draws the moving ship at its view position, and
// while it holds a tractor beam it redraws the whole battle first
// (Draw_Main_Combat_Screen), which switches clipping off, so a ship partly
// outside the view was drawn unclipped outside the frame buffer: SIGSEGV
// (work order 197, the armed scratch game at Altair). So the view is centred
// on the middle of the action with the game's own Snap_Center_Combat_Screen
// before the action runs, and a move longer than the view less a ship's
// margin (26 x 12 cells of 32 x 18) is refused (reason 10). The original
// builds a missile's field only while the missile is inside its view
// (combat1.cpp:436-455), so FIRE_MISSILE at one outside it centres the view
// on the missile and rebuilds the battle's fields first (the game's own
// Add_Tactical_Combat_Fields). Only the hidden original view moves;
// nothing of the battle does.
static bool Keep_Action_In_View_(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool refuse_long) {
    if (refuse_long && (abs(x1 - x0) > 26 || abs(y1 - y0) > 12)) return false;
    COMBINIT::Snap_Center_Combat_Screen((int16_t)((x0 + x1) / 2), (int16_t)((y0 + y1) / 2));
    return true;
}

// OrionLayer, open fix 58. Take_Combat_Command: the command's storage is above ProcessInput (ext_api.h).
void Combat_Command_Cell(int16_t* x, int16_t* y) {
    if (g_cmd_cell_valid) {
        *x = g_cmd_cell_x;
        *y = g_cmd_cell_y;
        g_cmd_cell_valid = false;
    }
}

static bool Combat_Unit_At_Anchor_(int16_t t) {
    if (t < 0 || t >= MOX::combat_total_ship_count) return false;
    const s_combat_data& u = MOX::combat_data[t];
    return u.unit_status == 0 && u.x >= 0 && u.x < MOX::combat_grid_width &&
           u.y >= 0 && u.y < MOX::combat_grid_height && MOX::combat_grid[u.x][u.y] == t;
}

int16_t Take_Combat_Command(int16_t input_val) {
    g_cmd_cell_valid = false;
    if (!g_combat_live || MOX::combat_data == nullptr) {
        g_combat_cmd.pending = g_board_target.pending = false;
        return input_val;
    }
    const s_combat_data& me = MOX::combat_data[MOX::cur_ship];
    if (g_board_target.pending) {                     // BOARD, step 2
        const CombatCommand b = g_board_target;
        g_board_target.pending = false;
        if (b.serial == g_combat_serial && b.unit == MOX::cur_ship &&
            MOX::board_flag != 0 && Combat_Unit_At_Anchor_(b.a)) {
            g_cmd_cell_x = MOX::combat_data[b.a].x;
            g_cmd_cell_y = MOX::combat_data[b.a].y;
            g_cmd_cell_valid = true;
            return MOX::space_field;
        }
        Combat_Event(CEV_COMMAND, CCMD_BOARD, 7, b.a, 0);   // board mode refused (already boarded)
        return input_val;
    }
    if (!g_combat_cmd.pending) return input_val;
    const CombatCommand c = g_combat_cmd;
    g_combat_cmd.pending = false;
    auto refuse = [&](int16_t why) {
        Combat_Event(CEV_COMMAND, c.op, why, c.a, c.b);
        return input_val;
    };
    auto cell = [&](int16_t x, int16_t y, int16_t input) {
        g_cmd_cell_x = x;
        g_cmd_cell_y = y;
        g_cmd_cell_valid = true;
        Combat_Event(CEV_COMMAND, c.op, 0, c.a, c.b);
        return input;
    };
    if (c.serial != g_combat_serial || c.unit != MOX::cur_ship) return refuse(1);
    if (MOX::order_info_flag != 1 || MOX::space_field < 1) return refuse(2);
    const bool free_mode = MOX::board_flag + MOX::skan_flag <= 0;
    switch (c.op) {
    case CCMD_MOVE:
        if (!free_mode) return refuse(5);
        if (c.a < 0 || c.a >= MOX::combat_grid_width || c.b < 0 || c.b >= MOX::combat_grid_height ||
            MOX::combat_grid[c.a][c.b] != 2000) return refuse(3);
        if (!Keep_Action_In_View_(me.x, me.y, c.a, c.b, true)) return refuse(10);  // OrionLayer, open fix 65.
        return cell(c.a, c.b, MOX::space_field);
    case CCMD_FACE:
        if (c.a < 0 || c.a >= MOX::combat_grid_width || c.b < 0 || c.b >= MOX::combat_grid_height) return refuse(3);
        return cell(c.a, c.b, (int16_t)-MOX::space_field);
    case CCMD_FIRE:
    case CCMD_SELECT:
    case CCMD_REMOVE_STASIS: {
        if (!free_mode) return refuse(5);
        if (!Combat_Unit_At_Anchor_(c.a) || c.a == MOX::cur_ship) return refuse(4);
        const s_combat_data& t = MOX::combat_data[c.a];
        const bool enemy = t.owner != me.owner;
        const bool stasis = CMBTMOV1::Ship_Is_In_Stasis(c.a) != 0;
        if (c.op == CCMD_FIRE && (!enemy || stasis || t.special_status_flag == 4)) return refuse(4);
        if (c.op == CCMD_SELECT && (enemy || MOX::combat_init != 0)) return refuse(4);
        if (c.op == CCMD_REMOVE_STASIS && (!enemy || !stasis)) return refuse(4);
        if (c.op == CCMD_FIRE && c.b != -1) {
            // The mask as the eight weapon rows would leave it: a slot in the
            // mask on, one out of it off for this shot (0, which the fire
            // branch puts back to 1), one switched off for good (-1) left so.
            // A network game learns the rows only from TOGGLE_WEAPON
            // messages, so there the mask must be the rows' own.
            if (MOX::net_flag != 0) return refuse(6);
            for (int k = 0; k < 8; k++) {
                s_combat_weapon_slot& w = MOX::combat_data[MOX::cur_ship].weapons[k];
                if ((c.b >> k) & 1) w.active = 1;
                else if (w.active != -1) w.active = 0;
            }
        }
        Keep_Action_In_View_(me.x, me.y, t.x, t.y, false);  // OrionLayer, open fix 65.
        return cell(t.x, t.y, MOX::space_field);
    }
    case CCMD_FIRE_MISSILE:
        if (c.a >= 0 && c.a < 300 && MOX::missile[c.a].type > 0 && MOX::missile_field[c.a] < 1) {  // OrionLayer, open fix 65. A missile outside the view has no field: centre on it, rebuild the fields.
            Keep_Action_In_View_(me.x, me.y, (int16_t)(MOX::missile[c.a].x / 20), (int16_t)(MOX::missile[c.a].y / 20), false);
            COMBAT1::Add_Tactical_Combat_Fields();
        }
        if (c.a < 0 || c.a >= 300 || MOX::missile[c.a].type <= 0 ||
            MOX::missile_field[c.a] < 1) return refuse(8);
        Combat_Event(CEV_COMMAND, c.op, 0, c.a, c.b);
        return MOX::missile_field[c.a];
    case CCMD_BOARD:
        if (MOX::skan_flag != 0) return refuse(5);
        if (!Combat_Unit_At_Anchor_(c.a) || MOX::combat_data[c.a].owner == me.owner) return refuse(4);
        if (MOX::board_flag == 0) {
            // Step 1 is the BOARD radio: the field engine writes the flag
            // on a click, an activation does not — so it is written here,
            // and the original's handler checks Already_Boarded.
            MOX::board_flag = 1;
            g_board_target = c;
            g_board_target.pending = true;
            Combat_Event(CEV_COMMAND, c.op, 0, c.a, c.b);
            return MOX::board_button;
        }
        Keep_Action_In_View_(me.x, me.y, MOX::combat_data[c.a].x, MOX::combat_data[c.a].y, false);  // OrionLayer, open fix 65.
        return cell(MOX::combat_data[c.a].x, MOX::combat_data[c.a].y, MOX::space_field);
    default:
        return refuse(9);
    }
}

// OrionLayer, open fix 57. How the last battle ended (ext_api.h).
CombatResultView g_combat_result = {};

void Record_Combat_Result(int16_t star_idx, int16_t colony_pop_lost,
                          int16_t winner, bool fought) {
    CombatResultView& r = g_combat_result;
    r.valid = true;
    ++r.number;
    r.serial = fought ? g_combat_serial : 0;
    r.fought = fought ? 1 : 0;
    r.winner = winner;
    r.turn = MOX::combat_turn;
    r.star = star_idx;
    r.colony = MOX::cmbt_colony_id;
    r.attacker = MOX::combat_attacker_player;
    r.defender = MOX::combat_defender_player;
    r.colony_pop_lost = colony_pop_lost;
    r.stardate = MOX::stardate;
    int16_t units = MOX::combat_data != nullptr ? MOX::combat_total_ship_count : 0;
    if (units < 0) units = 0;
    if (units > 210) units = 210;
    r.units = units;
    for (int16_t i = 0; i < units; i++) {
        const s_combat_data& d = MOX::combat_data[i];
        r.unit[i] = {d.ship_idx, d.owner, d.previous_owner, d.unit_status,
                     d.is_retreating, d.is_captured, d.structure_damage,
                     d.armor_remaining, d.structure_max};
    }
}

CombatGuard::~CombatGuard() {
    g_combat_live = previous;
}

// OrionLayer, open fix 29. The generic message box that is up (ext_api.h).
MessageBoxView g_message_box = {0, nullptr, nullptr, -1, -1, 0};

MessageBoxGuard::MessageBoxGuard(uint8_t kind, const char* title,
                                 const char* text, int16_t field_a,
                                 int16_t field_b, int16_t ticks)
    : previous(g_message_box) {
    g_message_box = {kind, title, text, field_a, field_b, ticks};
}

MessageBoxGuard::~MessageBoxGuard() {
    g_message_box = previous;
}

// OrionLayer, open fix 49. The turn-time popup that is up (ext_api.h).
TurnPopupView g_turn_popup = {0, {}, 0, nullptr, nullptr};

TurnPopupGuard::TurnPopupGuard(int16_t screen_id, uint8_t kind,
                               const char* title, const char* text)
    : previous(g_turn_popup), previous_screen(g_screen_override),
      active(true) {
    g_turn_popup.kind = kind;
    for (int16_t& a : g_turn_popup.args) a = -1;
    g_turn_popup.n_args = 0;
    g_turn_popup.text = text;
    g_turn_popup.title = title;
    if (screen_id >= 0) {
        g_screen_override = screen_id;
    }
}

TurnPopupGuard::~TurnPopupGuard() {
    end();
}

void TurnPopupGuard::arg(int16_t i, int16_t v) const {
    if (!active || i < 0 || i >= 16) return;
    g_turn_popup.args[i] = v;
    if (g_turn_popup.n_args < i + 1) g_turn_popup.n_args = (int16_t)(i + 1);
}

void TurnPopupGuard::set_text(const char* text) const {
    if (active) g_turn_popup.text = text;
}

void TurnPopupGuard::end() {
    if (!active) return;
    active = false;
    g_turn_popup = previous;
    g_screen_override = previous_screen;
}

// OrionLayer, open fix 51. The multiplayer step that takes input (ext_api.h).
MultiplayerView g_multiplayer = {0, {-1000, -1000, -1000}, nullptr};

MultiplayerGuard::MultiplayerGuard(uint8_t phase, int16_t field_a,
                                   int16_t field_b, int16_t field_c,
                                   const char* text)
    : previous(g_multiplayer) {
    g_multiplayer = {phase, {field_a, field_b, field_c}, text};
}

MultiplayerGuard::~MultiplayerGuard() {
    g_multiplayer = previous;
}

void MultiplayerGuard::set(uint8_t phase) const {
    g_multiplayer = {phase, {-1000, -1000, -1000}, nullptr};
}

// OrionLayer, open fix 47. The running list field (ext_api.h).
ListFieldView g_list_field = {nullptr, nullptr, 0, 0, nullptr};

ListFieldGuard::ListFieldGuard(const char* title, const char* items,
                               int32_t item_size, int16_t count,
                               const uint8_t* flags)
    : previous(g_list_field) {
    g_list_field = {title, items, item_size, count, flags};
}

ListFieldGuard::~ListFieldGuard() {
    g_list_field = previous;
}

void Tick(int16_t current_screen) {
    // What the client is told. The override is the only difference; the
    // game's own MOX::current_screen is untouched, because the dialogs it
    // distinguishes draw themselves from it (ext_api.h).
    if (g_screen_override >= 0) {
        current_screen = g_screen_override;
    }
    // Accept new connections
    g_server.AcceptClients();

    // Read input from clients
    g_server.ReadInput();

    // Process queued input commands
    ProcessInput();

    // OrionLayer, open fix 62. A pending activation is taken by the NEXT
    // Get_Input (fields.cpp), which may belong to another list than the one
    // the client answered: a screen that ended by itself in between (the
    // reports phase's whole-screen field, gone in 0.02-0.15 s) handed its
    // index to SELECT NEW RESEARCH, whose row 1 was then committed after its
    // input delay (open fix 26). Every Tick runs right before that take, so
    // an index that no longer names the field the client saw is dropped here.
    if (g_pending_field > 0 && g_pending_checked) {
        const bool same = g_pending_field < fields::fields_count &&
            fields::fields[g_pending_field].x == g_pending_rect[0] &&
            fields::fields[g_pending_field].y == g_pending_rect[1] &&
            fields::fields[g_pending_field].x_end == g_pending_rect[2] &&
            fields::fields[g_pending_field].y_end == g_pending_rect[3];
        if (!same) {
            g_pending_field = 0;
            g_pending_checked = false;
        }
    }

    // No clients? Skip serialization.
    if (g_server.ClientCount() == 0) return;

    // Detect events
    uint16_t events = 0;
    if (current_screen != g_last_screen) {
        events |= EVT_SCREEN_CHANGED;
        g_last_screen = current_screen;
    }
    if (MOX::stardate != g_last_stardate) {
        events |= EVT_TURN_ENDED;
        g_last_stardate = MOX::stardate;
    }

    // Send state snapshot
    static std::vector<uint8_t> state_buf;
    SerializeState(state_buf, current_screen);
    g_server.SendToSubscribers(SUB_STATE, MSG_STATE,
                                state_buf.data(), state_buf.size());

    // Send field list (only if changed)
    int16_t fc = fields::fields_count;
    if (fc != g_last_fields_count || (events & EVT_SCREEN_CHANGED)
        || g_server.TakeFieldsDirty()) {
        static std::vector<uint8_t> fields_buf;
        SerializeFields(fields_buf);
        g_server.SendToSubscribers(SUB_FIELDS, MSG_FIELDS,
                                    fields_buf.data(), fields_buf.size());
        // Right after the list it belongs to: a client that gets a
        // list with no slot message behind it knows it has none.
        static std::vector<uint8_t> slots_buf;
        if (SerializeSaveSlots(slots_buf, current_screen)) {
            g_server.SendToSubscribers(SUB_FIELDS, MSG_SAVE_SLOTS,
                                        slots_buf.data(),
                                        slots_buf.size());
        }
        g_last_fields_count = fc;
    }

    // Send visual frame
    static std::vector<uint8_t> visual_buf;
    SerializeVisual(visual_buf);
    g_server.SendToSubscribers(SUB_VISUAL, MSG_VISUAL,
                                visual_buf.data(), visual_buf.size());

    // Send events
    if (events != 0) {
        uint8_t evt_data[4];
        memcpy(evt_data, &events, 2);
        memcpy(evt_data + 2, &current_screen, 2);
        g_server.SendToSubscribers(SUB_EVENTS, MSG_EVENT,
                                    evt_data, sizeof(evt_data));
    }
}

void Shutdown() {
    g_server.Stop();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "ext: server stopped");
}

} // namespace ext
