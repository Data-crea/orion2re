#include "pch.h"

#include <cstring>

namespace CMBTMIS {
    void Reset_Fighter_Pursuit_History() {}
} // namespace CMBTMIS

namespace {
    s_combat_data combat_data[2];
    s_colony colonies[1];
    s_current_design current_design;
    s_planet_data planets[1];
    s_player players[MAX_PLAYERS];
    char planet_name[] = "Planet";
}

namespace TEST_SHIELD_TABLE {
    void Reset_() {
        memset(combat_data, 0, sizeof(combat_data));
        memset(colonies, 0, sizeof(colonies));
        memset(&current_design, 0, sizeof(current_design));
        memset(&MOX::settings, 0, sizeof(MOX::settings));
        memset(planets, 0, sizeof(planets));
        memset(players, 0, sizeof(players));
        MOX::combat_data = ::combat_data;
        MOX::colony = colonies;
        MOX::design = &current_design;
        MOX::planet = planets;
        MOX::combat_total_ship_count = 0;
        MOX::combat_defender_player = 0;
        MOX::PLAYER_NUM = 0;
        MOX::NUM_PLAYERS = 1;
        MOX::in_nebula_flag = 0;
        COMBINIT::defenses_allowed_mods = {
            WEAPON_MOD_FLAG_HEAVY_MOUNT |
                WEAPON_MOD_FLAG_POINT_DEFENSE |
                WEAPON_MOD_FLAG_ARMOR_PIERCING |
                WEAPON_MOD_FLAG_CONTINUOUS |
                WEAPON_MOD_FLAG_NO_RANGE_DISSIPATION |
                WEAPON_MOD_FLAG_SHIELD_PIERCING |
                WEAPON_MOD_FLAG_AUTO_FIRE |
                WEAPON_MOD_FLAG_ENVELOPING,
            WEAPON_MOD_FLAG_HEAVY_MOUNT |
                WEAPON_MOD_FLAG_POINT_DEFENSE |
                WEAPON_MOD_FLAG_ARMOR_PIERCING |
                WEAPON_MOD_FLAG_CONTINUOUS |
                WEAPON_MOD_FLAG_NO_RANGE_DISSIPATION |
                WEAPON_MOD_FLAG_SHIELD_PIERCING |
                WEAPON_MOD_FLAG_AUTO_FIRE |
                WEAPON_MOD_FLAG_ENVELOPING,
            WEAPON_MOD_FLAG_HEAVY_MOUNT |
                WEAPON_MOD_FLAG_POINT_DEFENSE |
                WEAPON_MOD_FLAG_CONTINUOUS |
                WEAPON_MOD_FLAG_NO_RANGE_DISSIPATION |
                WEAPON_MOD_FLAG_AUTO_FIRE |
                WEAPON_MOD_FLAG_ENVELOPING,
            WEAPON_MOD_FLAG_NO_RANGE_DISSIPATION |
                WEAPON_MOD_FLAG_ENVELOPING |
                WEAPON_MOD_FLAG_ECCM |
                WEAPON_MOD_FLAG_HEAVILY_ARMORED |
                WEAPON_MOD_FLAG_FAST |
                WEAPON_MOD_FLAG_OVERLOADED,
        };
        COMBINIT::satellite_space_allocation = {26, 13, 12, 49};
        COMBINIT::reinforced_hull_multiplier = {3, 3};
        COMBINIT::antaran_fortress_armor_multiplier = 3;
        COMBINIT::planet = {};
        COMBINIT::satellite_size_class_plus_one = 0;
        DESIGN::arc_cost_and_space[DESIGN::WEAPON_ARC_MODIFIER_EXTENDED] =
            {25, 25};
        DESIGN::arc_cost_and_space[DESIGN::WEAPON_ARC_MODIFIER_ALL_ROUND] =
            {50, 50};
        DESIGN::miniaturization.cost[0] = 100;
        for (int category = 0;
             category < DESIGN::MINIATURIZATION_CATEGORY_COUNT; ++category) {
            DESIGN::miniaturization.space[category][0] = 1000;
        }

        const uint16_t multipliers[HULL_CLASS_COUNT] = {
            5, 10, 15, 20, 25, 35, 15, 20, 25
        };
        memcpy(TECHDATA::shield_hull_multiplier, multipliers,
               sizeof(multipliers));
        const uint16_t shield_classes[SHIELD_COUNT] = {0, 1, 3, 5, 7, 10};
        for (int i = 0; i < SHIELD_COUNT; ++i) {
            TECHDATA::shields[i].shield_class = shield_classes[i];
        }
        TECHDATA::multi_phased_shields_bonus = 50;
        TECHDATA::esd_bonus = 100;
        TECHDATA::hard_shields_damage_reduction = 3;
    }

    s_combat_data* Combat_Ship_(int16_t ship_idx) {
        return &combat_data[ship_idx];
    }

    s_colony* Colony_() {
        return &colonies[0];
    }

    s_current_design* Design_() {
        return &current_design;
    }

    s_player* Player_() {
        return &players[0];
    }

    void Research_Special_(int16_t special_id) {
        players[0].tech_applications[TECHDATA::specials[special_id].tech_app_id] =
            TECH_RESEARCH_STATUS_RESEARCHED;
    }

    void Set_Combat_Special_(int16_t special_id) {
        combat_data[0].special_device_flags[special_id / 8] |=
            (uint8_t)(1u << (special_id % 8));
    }

    void Set_Design_Special_(int16_t special_id) {
        for (int i = 0; i < 8; ++i) {
            if (current_design.special_devices[i] == 0) {
                current_design.special_devices[i] = special_id;
                return;
            }
        }
    }
}

namespace MOX {
    int16_t first_field_in_group[10] = {
        0, 18, 55, 57, 29, 7, 22, 28, 10, 74};
    s_combat_data* combat_data = ::combat_data;
    s_colony* colony = colonies;
    s_player* player = players;
    s_planet_data* planet = planets;
    s_current_design* design = &current_design;
    s_settings settings;
    s_animation_header* screen_seg;
    s_animation_header* combat_planet_seg;
    s_animation_header* combat_planet_seg2;
    void* global_cache_seg;
    int16_t PLAYER_NUM;
    int16_t NUM_PLAYERS = 1;
    int16_t combat_total_ship_count;
    int16_t combat_defender_player;
    int16_t in_nebula_flag;
}

namespace HACCESS {
    char* __cdecl Get_Planet_Name_No_Prime(int16_t) {
        return planet_name;
    }
}

namespace buffer {
    void* __cdecl Buffer_Reload(const char*, int32_t, void*) {
        return nullptr;
    }
}

namespace farload {
    void* __cdecl Far_Reload_Next(const char*, int32_t, void*) {
        return nullptr;
    }
}

namespace animate {
    void __cdecl Draw_Palette(s_animation_header*) {
    }
}

namespace palette {
    void __cdecl Refresh_Palette() {
    }
}

namespace INITSHIP {
    int16_t __cdecl Best_Armor(int16_t) {
        return ARMOR_NO_ARMOR;
    }

    int16_t __cdecl Best_Computer(int16_t) {
        return COMPUTER_NO_COMPUTER;
    }

    int16_t __cdecl Best_Ship_Shield(int16_t) {
        return SHIELD_NO_SHIELD;
    }

    int16_t __cdecl Best_Beam_Weapon(int16_t, uint16_t) {
        return WEAPON_LASER_CANNON;
    }

    int16_t __cdecl Best_Point_Defense_Weapon_By_Size(int16_t) {
        return WEAPON_LASER_CANNON;
    }

    int16_t __cdecl Best_Missile_Weapon_NT(int16_t) {
        return WEAPON_NUCLEAR_MISSILE;
    }

    int16_t __cdecl Best_Warp_Drive(int16_t) {
        return DRIVE_NO_DRIVE;
    }
}

namespace CMBTFIR2 {
    void __cdecl Init_Internal_Space(s_combat_data*) {
    }
}

namespace MISC {
    int16_t __cdecl Bit_Value_To_Num(int32_t value, int16_t bit_limit) {
        for (int16_t bit = 0; bit < bit_limit; ++bit) {
            if (value == (1 << bit)) {
                return bit;
            }
        }
        return 0;
    }
}

namespace RUSS {
    void __cdecl Mox_Update() {
    }

    void __cdecl PFMT(int32_t, int32_t, const char*, ...) {
    }
}

namespace key {
    int32_t Wait_For_Key() {
        return 0;
    }
}

namespace struct_ {
    void __cdecl Clear_Structure(void* dest, uint32_t size) {
        memset(dest, 0, size);
    }

    void __cdecl Set_Bit_Field(void* bit_field, uint16_t bit_index) {
        uint8_t* bytes = static_cast<uint8_t*>(bit_field);
        bytes[bit_index / 8] |= (uint8_t)(1u << (bit_index % 8));
    }

    int16_t __cdecl Test_Bit_Field(void* bit_field, uint16_t bit_index) {
        const uint8_t* bytes = static_cast<const uint8_t*>(bit_field);
        return (bytes[bit_index / 8] >> (bit_index % 8)) & 1;
    }
}