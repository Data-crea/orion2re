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

/// True while an injected mouse event is queued and not yet
/// pumped. Sync_Mouse_State_From_SDL stands aside while this is
/// set, so the injected position survives to whoever reads
/// mouse::Pointer_X(). Cleared by Pump_Game_Input_Queue().
extern bool g_injected_mouse_pending;

} // namespace ext
