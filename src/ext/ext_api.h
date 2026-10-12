#pragma once
// orion2re Extension API
// Public interface. Only ext_api.cpp includes game headers.

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
extern bool g_hide_window;

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
