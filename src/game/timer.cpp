#include "pch.h"
#ifdef ORION2RE_EXT
#include "ext/ext_api.h"  // OrionLayer, open fix 79. The battle's ticks for its sounds.
#endif

namespace timer {
    namespace {
        constexpr int32_t k_animation_speed_min = -3;
        constexpr int32_t k_animation_speed_max = 3;
        constexpr uint32_t k_milliseconds_per_tick = 55;
        constexpr uint32_t k_animation_speed_display_ticks = 55;

        int32_t animation_speedup;
        uint32_t animation_speed_change_ms;
    } // namespace

    uint32_t time_mark;

    void __cdecl Mark_Time() {
        time_mark = (uint32_t)SDL_GetTicks();
    }

    int32_t __cdecl Get_Time_Since_Mark() {
        return (int32_t)((uint32_t)SDL_GetTicks() - time_mark);
    }

    void __cdecl Handle_Animation_Speed_Key(uint32_t key_code) {
        if (key_code == key::KEY_PAGE_DOWN) {
            if (animation_speedup > k_animation_speed_min) {
                animation_speedup--;
            }
            animation_speed_change_ms = (uint32_t)SDL_GetTicks();
        } else if (key_code == key::KEY_PAGE_UP) {
            if (animation_speedup < k_animation_speed_max) {
                animation_speedup++;
            }
            animation_speed_change_ms = (uint32_t)SDL_GetTicks();
        }
    }

    int32_t __cdecl Get_Animation_Speedup() {
        return animation_speedup;
    }

    bool __cdecl Animation_Speed_Change_Is_Visible() {
        const uint32_t current_ms = (uint32_t)SDL_GetTicks();
        return animation_speed_change_ms != 0 &&
            current_ms >= animation_speed_change_ms &&
            current_ms - animation_speed_change_ms <
            k_animation_speed_display_ticks * k_milliseconds_per_tick;
    }

    void __cdecl Release_Time(int32_t ticks) {
#ifdef ORION2RE_EXT
        ext::Battle_Wait(ticks);  // OrionLayer, open fix 79. The original's time, counted for the battle's sounds (before v2's animation speed-up is taken off).
        if (ext::Skip_Animation_Wait_()) {  // OrionLayer, open fix 71. A hidden battle's animation frame while a client draws it: no wait.
            SDL_Delay(0);
            return;
        }
#endif
        const int32_t adjusted_ticks = ticks - animation_speedup;
        if (adjusted_ticks <= 0) {
            SDL_Delay(0);
            return;
        }

        const uint32_t target_ms =
            (uint32_t)adjusted_ticks * k_milliseconds_per_tick;
        const uint32_t elapsed_ms = (uint32_t)(Get_Time_Since_Mark());

        if (elapsed_ms < target_ms) {
            SDL_Delay(target_ms - elapsed_ms);
        } else {
            SDL_Delay(0);
        }
    }
} // namespace timer