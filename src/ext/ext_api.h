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
