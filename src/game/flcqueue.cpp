#include "pch.h"
#ifdef ORION2RE_EXT
#include "ext/ext_api.h"  // OrionLayer, open fix 91. The invasion's explosions on the wire.
#endif

namespace FLCQUEUE {
    int16_t n_queue_entries;

    s_flic_queue_entry flic_queue[100];

    void __cdecl Allocate_Flic_Queue_And_Pointers() {
        n_queue_entries = 0;
    }

    void __cdecl Free_Flic_Queue_And_Pointers() {
        n_queue_entries = 0;
    }

    void __cdecl Draw_Flic_Queue() {
        for (int16_t i = 0; i < n_queue_entries; ++i) {
            Draw_Queue_Entry(i);
        }
    }

    void __cdecl Squeze_Out_Queue_Entry(int16_t entry_idx) {
        strings::Delete_Structure(entry_idx, flic_queue, sizeof(s_flic_queue_entry), 100);
        n_queue_entries--;
    }

    void __cdecl Draw_Queue_Entry(int16_t entry_idx) {
        s_flic_queue_entry* entry = &flic_queue[entry_idx];

        animate::Set_Animation_Frame(entry->animation, entry->current_frame);
#ifdef ORION2RE_EXT
        ext::Invasion_Flic(entry->animation, entry->current_frame, entry->x, entry->y);  // OrionLayer, open fix 91.
#endif

        if (entry->use_clipping != 0) {
            graphics::Set_Window(0, 0, 0x27F, 0x1DF);
            video::Clip_On();
            animate::Draw(entry->x, entry->y, entry->animation);
            video::Clip_Off();
        } else {
            animate::Draw(entry->x, entry->y, entry->animation);
        }

        entry->current_frame++;

        if (entry->play_mode == 0) {
            if (animate::Get_Animation_Frame_Count(entry->animation) <= entry->current_frame) {
                entry->current_frame = 0;
            }
        } else if (entry->play_mode == 1) {
            if (animate::Get_Animation_Frame_Count(entry->animation) <= entry->current_frame) {
                Squeze_Out_Queue_Entry(entry_idx);
            }
        }
    }

    void __cdecl Add_Flic_To_End_Of_Queue(int16_t x, int16_t y, s_animation_header* animation, uint8_t play_mode, uint8_t use_clipping) {
        if (n_queue_entries < 100) {
            int16_t entry_idx = n_queue_entries;
            flic_queue[entry_idx].x = x;
            flic_queue[entry_idx].y = y;
            flic_queue[entry_idx].animation = animation;
            flic_queue[entry_idx].play_mode = play_mode;
            flic_queue[entry_idx].use_clipping = use_clipping;
            n_queue_entries++;
        }
    }

    void __cdecl Add_Explosion_Anim(int16_t x, int16_t y) {
        Add_Flic_To_End_Of_Queue(x + 15, y + 15, COLGCBT::explosion_anim, 1, 1);
    }
} // namespace FLCQUEUE