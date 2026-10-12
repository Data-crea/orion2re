// Extension API implementation.
// This is the only ext file that includes game headers.

#include "pch.h"
#include "ext/ext_api.h"
#include "ext/ext_server.h"
#include "game/newgame.h"
#include "game/platform.h"
#include "game/build_queue.h"  // OrionLayer, open fix 35. autobuild_settings, sent in "COLS".

#include <vector>
#include <cstring>

namespace ext {

bool g_hide_window = false;
int16_t g_pending_field = 0;
int16_t g_activated_input = 0;
bool g_injected_mouse_pending = false;

static Server g_server;
static int16_t g_last_screen = -99;
static int32_t g_last_stardate = -1;
static int16_t g_last_fields_count = -1;

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
    g_hide_window = true;
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
