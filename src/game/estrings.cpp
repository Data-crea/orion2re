#include "pch.h"
#include "string_overrides.h"

namespace ESTRINGS {

    char* strings[ESTRINGS_COUNT];
    std::vector<char> string_data;

    const char s_0_0055110c[4] = "\0320";
    const char s_1_00551110[4] = "\0321";

    void __cdecl Load_E_Strings() {
        static const char* const filenames[] = {
            "estrings.lbx", "estrGERM.lbx", "estrFREN.lbx", "estrSPAN.lbx", "estrITAL.lbx", "estrPOLI.lbx"};
        const uint8_t language = (uint8_t)MOX::settings.language;
        const char* filename = filenames[language < 6 ? language : 0];
        std::vector<char> loaded(ESTRINGS_DATA_SIZE);
        farload::Farload_Data_Static(filename, 0, loaded.data(), 0, 1, ESTRINGS_DATA_SIZE);
        string_overrides::Apply_Table(filename, &loaded, ESTRINGS_COUNT);
        string_data.swap(loaded);

        char* current_string = string_data.data();
        for (int string_index = 0; string_index < ESTRINGS_COUNT; ++string_index) {
            ESTRINGS::strings[string_index] = current_string;
            current_string += strlen(current_string) + 1;
        }

        Load_H_And_Pro_File_Strings();

        if (MOX::settings.language == 1) {
            ESTRINGS::strings[368] = const_cast<char*>("maingerm.lbx");
            return;
        }

        if (MOX::settings.language == 0) {
            ESTRINGS::strings[368] = const_cast<char*>("maintext.lbx");
            return;
        }

        if (MOX::settings.language == 2) {
            ESTRINGS::strings[368] = const_cast<char*>("mainfren.lbx");
            return;
        }

        if (MOX::settings.language == 3) {
            ESTRINGS::strings[368] = const_cast<char*>("mainspan.lbx");
            return;
        }

        if (MOX::settings.language == 4) {
            ESTRINGS::strings[368] = const_cast<char*>("mainital.lbx");
            return;
        }

        if (MOX::settings.language == 5) {
            ESTRINGS::strings[368] = const_cast<char*>("mainpoli.lbx");
        }
    }

    void __cdecl Load_H_And_Pro_File_Strings(void) {
        MOX::personality_labels[0] = ESTRINGS::E_Strings(0x268);
        MOX::personality_labels[1] = ESTRINGS::E_Strings(0x269);
        MOX::personality_labels[2] = ESTRINGS::E_Strings(0x26A);
        MOX::personality_labels[3] = ESTRINGS::E_Strings(0x26B);
        MOX::personality_labels[4] = ESTRINGS::E_Strings(0x26C);
        MOX::personality_labels[5] = ESTRINGS::E_Strings(0x26D);
        MOX::personality_labels[6] = ESTRINGS::E_Strings(0x26E);

        MOX::objective_labels[0] = ESTRINGS::E_Strings(0x26F);
        MOX::objective_labels[1] = ESTRINGS::E_Strings(0x270);
        MOX::objective_labels[2] = ESTRINGS::E_Strings(0x271);
        MOX::objective_labels[3] = ESTRINGS::E_Strings(0x272);
        MOX::objective_labels[4] = ESTRINGS::E_Strings(0x273);
        MOX::objective_labels[5] = ESTRINGS::E_Strings(0x274);

        MOX::treaty_labels[0] = ESTRINGS::E_Strings(0x275);
        MOX::treaty_labels[1] = ESTRINGS::E_Strings(0x276);
        MOX::treaty_labels[2] = ESTRINGS::E_Strings(0x277);
        MOX::treaty_labels[3] = ESTRINGS::E_Strings(0x278);
        MOX::treaty_labels[4] = ESTRINGS::E_Strings(0x279);
        MOX::treaty_labels[5] = ESTRINGS::E_Strings(0x27A);
        MOX::treaty_labels[6] = ESTRINGS::E_Strings(0x27B);

        MOX::government_labels[GOVERNMENT_FEUDAL] = ESTRINGS::E_Strings(0x27C);
        MOX::government_labels[GOVERNMENT_CONFEDERATION] = ESTRINGS::E_Strings(0x27D);
        MOX::government_labels[GOVERNMENT_DICTATORSHIP] = ESTRINGS::E_Strings(0x27E);
        MOX::government_labels[GOVERNMENT_IMPERIUM] = ESTRINGS::E_Strings(0x27F);
        MOX::government_labels[GOVERNMENT_DEMOCRACY] = ESTRINGS::E_Strings(0x280);
        MOX::government_labels[GOVERNMENT_FEDERATION] = ESTRINGS::E_Strings(0x281);
        MOX::government_labels[GOVERNMENT_UNIFICATION] = ESTRINGS::E_Strings(0x282);
        MOX::government_labels[GOVERNMENT_GALACTIC_UNIFICATION] = ESTRINGS::E_Strings(0x283);

        MOX::race_names[0] = ESTRINGS::E_Strings(0x285);
        MOX::race_names[1] = ESTRINGS::E_Strings(0x286);
        MOX::race_names[2] = ESTRINGS::E_Strings(0x287);
        MOX::race_names[3] = ESTRINGS::E_Strings(0x288);
        MOX::race_names[4] = ESTRINGS::E_Strings(0x289);
        MOX::race_names[5] = ESTRINGS::E_Strings(0x28A);
        MOX::race_names[6] = ESTRINGS::E_Strings(0x28B);
        MOX::race_names[7] = ESTRINGS::E_Strings(0x28C);
        MOX::race_names[8] = ESTRINGS::E_Strings(0x28D);
        MOX::race_names[9] = ESTRINGS::E_Strings(0x28E);
        MOX::race_names[10] = ESTRINGS::E_Strings(0x28F);
        MOX::race_names[11] = ESTRINGS::E_Strings(0x290);
        MOX::race_names[12] = ESTRINGS::E_Strings(0x291);
        MOX::race_names[13] = ESTRINGS::E_Strings(0x292);
        MOX::race_names[14] = ESTRINGS::E_Strings(0x293);
        MOX::race_names[15] = ESTRINGS::E_Strings(0x0AD);
        MOX::race_names[16] = ESTRINGS::E_Strings(0x0F6);
        MOX::race_names[17] = ESTRINGS::E_Strings(0x110);
        MOX::race_names[18] = ESTRINGS::E_Strings(0x119);
        MOX::race_names[19] = ESTRINGS::E_Strings(0x144);

        MOX::help_category_labels[0] = ESTRINGS::E_Strings(0x08E);
        MOX::help_category_labels[1] = ESTRINGS::E_Strings(0x294);
        MOX::help_category_labels[2] = ESTRINGS::E_Strings(0x295);
        MOX::help_category_labels[3] = ESTRINGS::E_Strings(0x296);
        MOX::help_category_labels[4] = ESTRINGS::E_Strings(0x297);
        MOX::help_category_labels[5] = ESTRINGS::E_Strings(0x298);
        MOX::help_category_labels[6] = ESTRINGS::E_Strings(0x299);
        MOX::help_category_labels[7] = ESTRINGS::E_Strings(0x29A);
        MOX::help_category_labels[8] = ESTRINGS::E_Strings(0x29B);
        MOX::help_category_labels[9] = ESTRINGS::E_Strings(0x29C);
        MOX::help_category_labels[10] = ESTRINGS::E_Strings(0x29D);
        MOX::help_category_labels[11] = ESTRINGS::E_Strings(0x29E);
        MOX::help_category_labels[12] = ESTRINGS::E_Strings(0x29F);
        MOX::help_category_labels[13] = ESTRINGS::E_Strings(0x2A0);
        MOX::help_category_labels[14] = ESTRINGS::E_Strings(0x2A1);
        MOX::help_category_labels[15] = ESTRINGS::E_Strings(0x2A2);
        MOX::help_category_labels[16] = ESTRINGS::E_Strings(0x2A3);

        MOX::spectral_class_string[STAR_CLASS_B] = ESTRINGS::E_Strings(0x2A4);
        MOX::spectral_class_string[STAR_CLASS_F] = ESTRINGS::E_Strings(0x2A5);
        MOX::spectral_class_string[STAR_CLASS_G] = ESTRINGS::E_Strings(0x2A6);
        MOX::spectral_class_string[STAR_CLASS_K] = ESTRINGS::E_Strings(0x2A7);
        MOX::spectral_class_string[STAR_CLASS_M] = ESTRINGS::E_Strings(0x2A8);
        MOX::spectral_class_string[STAR_CLASS_DWARF] = ESTRINGS::E_Strings(0x2A9);
        MOX::spectral_class_string[STAR_CLASS_BLACK_HOLE] = ESTRINGS::E_Strings(0x2AA);

        MOX::planet_size_string[PLANET_SIZE_TINY] = ESTRINGS::E_Strings(0x2AB);
        MOX::planet_size_string[PLANET_SIZE_SMALL] = ESTRINGS::E_Strings(0x1E0);
        MOX::planet_size_string[PLANET_SIZE_MEDIUM] = ESTRINGS::E_Strings(0x173);
        MOX::planet_size_string[PLANET_SIZE_LARGE] = ESTRINGS::E_Strings(0x168);
        MOX::planet_size_string[PLANET_SIZE_HUGE] = ESTRINGS::E_Strings(0x143);

        MOX::mineral_class_string[MINERAL_RESOURCE_ULTRA_POOR] = ESTRINGS::E_Strings(0x2AC);
        MOX::mineral_class_string[MINERAL_RESOURCE_POOR] = ESTRINGS::E_Strings(0x1A7);
        MOX::mineral_class_string[MINERAL_RESOURCE_ABUNDANT] = ESTRINGS::E_Strings(0x2AD);
        MOX::mineral_class_string[MINERAL_RESOURCE_RICH] = ESTRINGS::E_Strings(0x1C2);
        MOX::mineral_class_string[MINERAL_RESOURCE_ULTRA_RICH] = ESTRINGS::E_Strings(0x2AE);

        MOX::planet_gravity_string[PLANET_GRAVITY_LOW_G] = ESTRINGS::E_Strings(0x2AF);
        MOX::planet_gravity_string[PLANET_GRAVITY_NORMAL_G] = ESTRINGS::E_Strings(0x2B0);
        MOX::planet_gravity_string[PLANET_GRAVITY_HEAVY_G] = ESTRINGS::E_Strings(0x2B1);

        MOX::planet_atmosphere_string[0] = ESTRINGS::E_Strings(0x2B2);
        MOX::planet_atmosphere_string[1] = ESTRINGS::E_Strings(0x2B3);
        MOX::planet_atmosphere_string[2] = ESTRINGS::E_Strings(0x2B4);

        MOX::planet_group_string[PLANET_GROUP_BOMBARDED] = ESTRINGS::E_Strings(0x2B5);
        MOX::planet_group_string[PLANET_GROUP_HOSTILE] = ESTRINGS::E_Strings(0x2B6);
        MOX::planet_group_string[PLANET_GROUP_LIFE_SUPPORTING] = ESTRINGS::E_Strings(0x2B7);
        MOX::planet_group_string[PLANET_GROUP_LIFELESS] = ESTRINGS::E_Strings(0x2B8);

        MOX::planet_special_string[SYSTEM_SPECIAL_NO_SPECIAL] = ESTRINGS::E_Strings(0x2B9);
        MOX::planet_special_string[SYSTEM_SPECIAL_STABLE_WORMHOLE] = ESTRINGS::E_Strings(0x2BA);
        MOX::planet_special_string[SYSTEM_SPECIAL_SPACE_DEBRIS] = ESTRINGS::E_Strings(0x2BB);
        MOX::planet_special_string[SYSTEM_SPECIAL_PIRATE_CACHE] = ESTRINGS::E_Strings(0x2BC);
        MOX::planet_special_string[SYSTEM_SPECIAL_GOLD_DEPOSITS] = ESTRINGS::E_Strings(0x2BD);
        MOX::planet_special_string[SYSTEM_SPECIAL_GEM_DEPOSITS] = ESTRINGS::E_Strings(0x2BE);
        MOX::planet_special_string[SYSTEM_SPECIAL_NATIVES] = ESTRINGS::E_Strings(0x2BF);
        MOX::planet_special_string[SYSTEM_SPECIAL_SPLINTER_COLONY] = ESTRINGS::E_Strings(0x2C0);
        MOX::planet_special_string[SYSTEM_SPECIAL_MAROONED_HERO] = ESTRINGS::E_Strings(0x2C1);
        MOX::planet_special_string[SYSTEM_SPECIAL_SPACE_MONSTER] = ESTRINGS::E_Strings(0x2C2);
        MOX::planet_special_string[SYSTEM_SPECIAL_ANCIENT_ARTIFACTS] = ESTRINGS::E_Strings(0x2C3);
        MOX::planet_special_string[SYSTEM_SPECIAL_ORION_STAR] = ESTRINGS::E_Strings(0x2C4);

        MOX::planet_type_string[PLANET_TYPE_NOT_USED0] = ESTRINGS::E_Strings(0x2C5);
        MOX::planet_type_string[PLANET_TYPE_ASTEROID] = ESTRINGS::E_Strings(0x2C6);
        MOX::planet_type_string[PLANET_TYPE_GAS_GIANT] = ESTRINGS::E_Strings(0x2C7);
        MOX::planet_type_string[PLANET_TYPE_PLANET] = ESTRINGS::E_Strings(0x2C8);
        MOX::planet_type_string[PLANET_TYPE_NOT_USED4] = ESTRINGS::E_Strings(0x2C9);
        MOX::planet_type_string[PLANET_TYPE_NOT_USED5] = ESTRINGS::E_Strings(0x2CA);
        MOX::planet_type_string[PLANET_TYPE_NOT_USED6] = ESTRINGS::E_Strings(0x2CB);
        MOX::planet_type_string[PLANET_TYPE_NOT_USED7] = ESTRINGS::E_Strings(0x2CC);
        MOX::planet_type_string[PLANET_TYPE_NOT_USED8] = ESTRINGS::E_Strings(0x2CD);
        MOX::planet_type_string[PLANET_TYPE_NOT_USED9] = ESTRINGS::E_Strings(0x2CE);

        MOX::planet_climate_string[PLANET_CLIMATE_TOXIC] = ESTRINGS::E_Strings(0x21B);
        MOX::planet_climate_string[PLANET_CLIMATE_RADIATED] = ESTRINGS::E_Strings(0x2CF);
        MOX::planet_climate_string[PLANET_CLIMATE_BARREN] = ESTRINGS::E_Strings(0x2D0);
        MOX::planet_climate_string[PLANET_CLIMATE_DESERT] = ESTRINGS::E_Strings(0x2D1);
        MOX::planet_climate_string[PLANET_CLIMATE_TUNDRA] = ESTRINGS::E_Strings(0x2D2);
        MOX::planet_climate_string[PLANET_CLIMATE_OCEAN] = ESTRINGS::E_Strings(0x18F);
        MOX::planet_climate_string[PLANET_CLIMATE_SWAMP] = ESTRINGS::E_Strings(0x1F5);
        MOX::planet_climate_string[PLANET_CLIMATE_ARID] = ESTRINGS::E_Strings(0x0B8);
        MOX::planet_climate_string[PLANET_CLIMATE_TERRAN] = ESTRINGS::E_Strings(0x2D3);
        MOX::planet_climate_string[PLANET_CLIMATE_GAIA] = ESTRINGS::E_Strings(0x12F);

        MOX::skill_data[0].name = ESTRINGS::E_Strings(0x2D4);
        MOX::skill_data[1].name = ESTRINGS::E_Strings(0x2D5);
        MOX::skill_data[2].name = ESTRINGS::E_Strings(0x2D6);
        MOX::skill_data[3].name = ESTRINGS::E_Strings(0x2D7);
        MOX::skill_data[4].name = ESTRINGS::E_Strings(0x26F);
        MOX::skill_data[5].name = ESTRINGS::E_Strings(0x2D8);
        MOX::skill_data[6].name = ESTRINGS::E_Strings(0x2D9);
        MOX::skill_data[7].name = ESTRINGS::E_Strings(0x2DA);
        MOX::skill_data[8].name = ESTRINGS::E_Strings(0x2DB);
        MOX::skill_data[9].name = ESTRINGS::E_Strings(0x2DC);
        MOX::skill_data[10].name = ESTRINGS::E_Strings(0x2DD);
        MOX::skill_data[11].name = ESTRINGS::E_Strings(0x2DE);
        MOX::skill_data[12].name = ESTRINGS::E_Strings(0x2DF);
        MOX::skill_data[13].name = ESTRINGS::E_Strings(0x2E0);
        MOX::skill_data[14].name = ESTRINGS::E_Strings(0x2E1);
        MOX::skill_data[15].name = ESTRINGS::E_Strings(0x2E2);
        MOX::skill_data[16].name = ESTRINGS::E_Strings(0x2E3);
        MOX::skill_data[17].name = ESTRINGS::E_Strings(0x2E4);
        MOX::skill_data[18].name = ESTRINGS::E_Strings(0x2E5);
        MOX::skill_data[19].name = ESTRINGS::E_Strings(0x2E6);
        MOX::skill_data[20].name = ESTRINGS::E_Strings(0x2E7);
        MOX::skill_data[21].name = ESTRINGS::E_Strings(0x2E8);
        MOX::skill_data[22].name = ESTRINGS::E_Strings(0x2E9);
        MOX::skill_data[23].name = ESTRINGS::E_Strings(0x2EA);
        MOX::skill_data[24].name = ESTRINGS::E_Strings(0x2EB);
        MOX::skill_data[25].name = ESTRINGS::E_Strings(0x2EC);
        MOX::skill_data[26].name = ESTRINGS::E_Strings(0x2ED);
        MOX::skill_data[27].name = ESTRINGS::E_Strings(0x2EE);
        MOX::skill_data[28].name = ESTRINGS::E_Strings(0x2EF);
        MOX::skill_data[29].name = ESTRINGS::E_Strings(0x2F0);
        MOX::skill_data[30].name = ESTRINGS::E_Strings(0x2F1);
        MOX::skill_data[31].name = ESTRINGS::E_Strings(0x2F2);
        MOX::skill_data[32].name = ESTRINGS::E_Strings(0x2F3);
        MOX::skill_data[33].name = ESTRINGS::E_Strings(0x2F4);
        MOX::skill_data[34].name = ESTRINGS::E_Strings(0x2F5);
        MOX::skill_data[35].name = ESTRINGS::E_Strings(0x2F6);
        MOX::skill_data[36].name = ESTRINGS::E_Strings(0x2F7);
        MOX::skill_data[37].name = ESTRINGS::E_Strings(0x2F8);
        MOX::skill_data[38].name = ESTRINGS::E_Strings(0x2F9);
        MOX::skill_data[39].name = ESTRINGS::E_Strings(0x2FA);
        MOX::skill_data[40].name = ESTRINGS::E_Strings(0x2A0);
        MOX::skill_data[41].name = ESTRINGS::E_Strings(0x2FB);
        MOX::skill_data[42].name = ESTRINGS::E_Strings(0x2FC);
        MOX::skill_data[43].name = ESTRINGS::E_Strings(0x2FD);
        MOX::skill_data[44].name = ESTRINGS::E_Strings(0x2FE);
        MOX::skill_data[45].name = ESTRINGS::E_Strings(0x2FF);
        MOX::skill_data[46].name = ESTRINGS::E_Strings(0x300);
        MOX::skill_data[47].name = ESTRINGS::E_Strings(0x301);
        MOX::skill_data[48].name = ESTRINGS::E_Strings(0x302);
        MOX::skill_data[49].name = ESTRINGS::E_Strings(0x303);
        MOX::skill_data[50].name = ESTRINGS::E_Strings(0x304);
        MOX::skill_data[51].name = ESTRINGS::E_Strings(0x305);
        MOX::skill_data[52].name = ESTRINGS::E_Strings(0x306);
        MOX::skill_data[53].name = ESTRINGS::E_Strings(0x307);

        MOX::admin_skill_names[0] = ESTRINGS::E_Strings(0x2DB);
        MOX::admin_skill_names[1] = ESTRINGS::E_Strings(0x2DF);
        MOX::admin_skill_names[2] = ESTRINGS::E_Strings(0x2E3);
        MOX::admin_skill_names[3] = ESTRINGS::E_Strings(0x2E9);
        MOX::admin_skill_names[4] = ESTRINGS::E_Strings(0x2EB);
        MOX::admin_skill_names[5] = ESTRINGS::E_Strings(0x2ED);
        MOX::admin_skill_names[6] = ESTRINGS::E_Strings(0x2F9);
        MOX::admin_skill_names[7] = ESTRINGS::E_Strings(0x2FC);
        MOX::admin_skill_names[8] = ESTRINGS::E_Strings(0x300);

        MOX::officer_level_names[0] = ESTRINGS::E_Strings(0x308);
        MOX::officer_level_names[1] = ESTRINGS::E_Strings(0x309);
        MOX::officer_level_names[2] = ESTRINGS::E_Strings(0x30A);
        MOX::officer_level_names[3] = ESTRINGS::E_Strings(0x30B);
        MOX::officer_level_names[4] = ESTRINGS::E_Strings(0x30C);
        MOX::officer_level_names[5] = ESTRINGS::E_Strings(0x30D);

        MOX::star_officer_level_names[0] = ESTRINGS::E_Strings(0x30E);
        MOX::star_officer_level_names[1] = ESTRINGS::E_Strings(0x30F);
        MOX::star_officer_level_names[2] = ESTRINGS::E_Strings(0x310);
        MOX::star_officer_level_names[3] = ESTRINGS::E_Strings(0x311);
        MOX::star_officer_level_names[4] = ESTRINGS::E_Strings(0x312);
        MOX::star_officer_level_names[5] = ESTRINGS::E_Strings(0x313);

        MOX::hyper_field_title = ESTRINGS::E_Strings(0x284);
    }

    char* __cdecl E_Strings(int16_t string_id) {
        return strings[string_id];
    }

} // namespace ESTRINGS