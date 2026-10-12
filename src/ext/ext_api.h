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
extern int16_t g_screen_override;

/// Sets `g_screen_override` while it lives and restores the previous value
/// when it goes out of scope — so a function with several returns needs one
/// line. An id below 0 means "no override" and the guard does nothing.
struct ScreenOverride {
    int16_t previous;
    explicit ScreenOverride(int16_t id);
    ~ScreenOverride();
};

/// True while an injected mouse event is queued and not yet
/// pumped. Sync_Mouse_State_From_SDL stands aside while this is
/// set, so the injected position survives to whoever reads
/// mouse::Pointer_X(). Cleared by Pump_Game_Input_Queue().
extern bool g_injected_mouse_pending;

} // namespace ext
