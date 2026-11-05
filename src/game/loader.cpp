#include "pch.h"

namespace LOADER {
    namespace {
        void* picture_pool;

        template <typename Visitor>
        void Visit_Pictures(Visitor visit) {
            s_animation_header** buttons[] = {
                &MOX::main_game_button_seg,
                &MOX::main_next_button_seg,
                &MOX::main_colonies_button_seg,
                &MOX::main_planets_button_seg,
                &MOX::main_fleets_button_seg,
                &MOX::main_zoom_in_button_seg,
                &MOX::main_zoom_out_button_seg,
                &MOX::main_leaders_button_seg,
                &MOX::main_races_button_seg,
                &MOX::main_info_button_seg,
                &MOX::fleet_box_selection_seg,
                &MOX::darkened_turn_button_seg,
            };
            for (int i = 0; i < 12; ++i) {
                visit("BUFFER0.LBX", i + 1, buttons[i]);
            }
            visit("BUFFER0.LBX", 0x52, &MOX::moveable_box_pict[4]);
            s_animation_header** fleet_buttons[] = {
                &MOX::fleet_box_scroll_up_button_seg,
                &MOX::fleet_box_scroll_dn_button_seg,
                &MOX::fleet_box_all_button_seg,
                &MOX::fleet_box_att_xenons_lg_button_seg,
                &MOX::fleet_box_colonize_lg_button_seg,
                &MOX::fleet_box_engage_lg_button_seg,
                &MOX::fleet_box_transport_lg_button_seg,
                &MOX::fleet_box_outpost_lg_button_seg,
                &MOX::fleet_box_close_lg_button_seg,
                &MOX::fleet_box_att_xenons_sm_button_seg,
                &MOX::fleet_box_engage_sm_button_seg,
                &MOX::fleet_box_colonize_sm_button_seg,
                &MOX::fleet_box_transport_sm_button_seg,
                &MOX::fleet_box_outpost_sm_button_seg,
                &MOX::fleet_box_close_sm_button_seg,
            };
            for (int i = 0; i < 15; ++i) {
                visit("BUFFER0.LBX", 0x3A + i, fleet_buttons[i]);
            }
            for (int i = 0; i < 24; ++i) {
                visit("BUFFER0.LBX", 0xF1 + i, &MOX::hl_antaran_ship_icon_seg[i]);
                visit("BUFFER0.LBX", 0xF1 + i, &MOX::antaran_ship_icons[i]);
            }
            for (int i = 0; i < 11; ++i) {
                visit("gstar.lbx", i + 1, &MOX::star_colors_seg[i]);
            }
            for (int i = 0; i < MAX_PLAYERS; ++i) {
                visit("BUFFER0.LBX", 0x109 + i, &MAINSCR::player_lights_seg[i]);
            }
        }
    } // namespace

    uint32_t Persistent_Picture_Space() {
        uint64_t size = 0xDC + 2 * 0x3A4 + 3 * sizeof(allocate::s_alloc_header);
        Visit_Pictures([&](const char* filename, int entry, s_animation_header**) {
            s_vfs_file* file = vfs::fopen_lbx(filename, entry);
            if (file == nullptr) {
                farload::Error_Handler(filename, 1, entry);
            }
            const int32_t bytes = vfs::fsize(file);
            vfs::fclose(file);
            if (bytes < (int32_t)sizeof(s_animation_header)) {
                farload::Error_Handler(filename, 7, entry);
            }
            size += allocate::Nested_No_Header_Pool_Size((uint32_t)bytes);
            if (size > INT32_MAX - sizeof(allocate::s_alloc_header)) {
                exitm::Exit_With_Message("Persistent picture arena exceeds supported size");
            }
        });
        return (uint32_t)size;
    }

    void Free_Pictures() {
        if (picture_pool != nullptr) {
            free((allocate::s_alloc_header*)picture_pool - 1);
            picture_pool = nullptr;
        }
    }

    void __cdecl Load_Main_Screen_Pictures() {
        const uint32_t size = Persistent_Picture_Space();
        Free_Pictures();
        picture_pool = allocate::Allocate_Space(size);
        Visit_Pictures([](const char* filename, int entry, s_animation_header** target) {
            *target = (s_animation_header*)farload::Far_Reload_Next(filename, entry, picture_pool);
        });
        MOX::moveable_box_pict[14] = nullptr;
        MOX::star_width = MOX::star_height = 0x21;
        SYS::sys_data_buffer = allocate::Allocate_Next_Block(picture_pool, 0xDC);
        MOX::star_name_loaded_string = nullptr;
        MOX::sys_disp = nullptr;
        COLONY::swap_bitm = (s_animation_header*)allocate::Allocate_Next_Block(picture_pool, 0x3A4);
        COLONY::mouse_bitm = (s_animation_header*)allocate::Allocate_Next_Block(picture_pool, 0x3A4);
    }

    void __cdecl Load_Pictures() {
        MOX::screen_seg = (s_animation_header*)MOX::global_screen_seg;
        memset(Animation_Frame_Offsets_((s_animation_header*)MOX::screen_seg), 0, sizeof(uint32_t) * 5);
        Load_Main_Screen_Pictures();
        RUSS::Mox_Update();
        TECHINIT::Init_Tech();
        RUSS::Mox_Update();
    }

    void __cdecl Load_Player_Special_Names() {
        char* const loaded_names = (char*)farload::Far_Reload(
            "RACESTUF.lbx",
            (int32_t)(uint8_t)MOX::settings.language + 8,
            MOX::special_names_seg);

        int16_t name_offset = 0;
        int16_t name_index = 1;

        MOX::special_ability_names[0] = loaded_names;

        do {
            name_offset = (int16_t)(name_offset + (int16_t)strlen(loaded_names + name_offset));

            while (loaded_names[name_offset] == '\0') {
                ++name_offset;
            }

            MOX::special_ability_names[name_index] = loaded_names + name_offset;
            ++name_index;
        } while (name_index < 0x20);
    }
} // namespace LOADER