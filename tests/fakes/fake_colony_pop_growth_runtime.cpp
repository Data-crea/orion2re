#include "pch.h"

#include "lua/lua_game.h"

namespace {
    s_colony colonies[2];
    // MOX::planet points past the sentinel, leaving two usable planet records.
    s_planet_data planet_storage[3];
    s_player players[MAX_PLAYERS];
    s_star_data stars[1];
    s_ship_data ships[1];
    s_leader_data leaders[1];
    int32_t destroy_colony_reason;
    int16_t add_spy_result;
    int16_t deleted_product;
    bool population_boom;
    bool plague;
    int custom_autobuild_calls;
    int16_t legacy_autobuild_product;
    int calc_ship_level_calls;
    int init_ship_designs_calls;
    int16_t population_growth_adjustment;
    int population_growth_calls;
    orion2re::lua::game::s_population_growth_context population_growth_context;
}

namespace TEST_COLONY_POP_GROWTH {
    void Reset_() {
        memset(colonies, 0, sizeof(colonies));
        memset(planet_storage, 0, sizeof(planet_storage));
        memset(players, 0, sizeof(players));
        memset(stars, 0, sizeof(stars));
        memset(ships, 0, sizeof(ships));
        memset(leaders, 0, sizeof(leaders));
        memset(TECHDATA::buildings, 0, sizeof(TECHDATA::buildings));
        memset(
            TECHDATA::technology_applications,
            0,
            sizeof(TECHDATA::technology_applications));
        planet_storage[0].colony_index = 1;
        planet_storage[1].size = PLANET_SIZE_HUGE;
        colonies[1].buildings[BUILDING_CAPITOL] = 1;
        stars[0].officer_index[0] = -1;
        TECHDATA::buildings[BUILDING_CAPITOL].tech_app = TECH_APP_CAPITOL;
        destroy_colony_reason = -1;
        add_spy_result = 1;
        deleted_product = COLONY_PRODUCTION_NONE;
        population_boom = false;
        plague = false;
        custom_autobuild_calls = 0;
        legacy_autobuild_product = COLONY_PRODUCTION_NONE;
        calc_ship_level_calls = 0;
        init_ship_designs_calls = 0;
        population_growth_adjustment = 0;
        population_growth_calls = 0;
        population_growth_context = {};
        memset(
            COLCALC::government_bonuses,
            0,
            sizeof(COLCALC::government_bonuses));
        COLCALC::growth_rate_bonus = {
            0,
            25,
            50,
            100,
            -200,
        };
        COLCALC::growth_formula_factor = 2000;
        COLCALC::food_farmer_bonus = {2, 2, 4, 2, 4, 6};
        COLCALC::fungi_climate = COLCALC::FUNGI_CLIMATE_CLASSIC;
        AIDATA::uncolonized_planet_food_worth = {75, 38};
        AIDATA::uncolonized_planet_special_worth = {1280, 2560, 640, 1600};
        COLCALC::population_bonus.advanced_city_planning = 5;
        COLCALC::population_bonus.tolerant = 25;
        COLCALC::pop_bonus_biospheres = 2;
        const uint8_t subterranean_defaults[PLANET_SIZE_COUNT] = {
            2, 4, 6, 8, 10,
        };
        memcpy(
            COLCALC::pop_bonus_subterranean,
            subterranean_defaults,
            sizeof(subterranean_defaults));
        COLCALC::weather_control_climate = PLANET_CLIMATE_TOXIC;
        COLCALC::terraform_toxic_building = BUILDING_NO_BUILDING;
        COLCALC::aquatic_treats_tundra_as_swamp = 0;
        memset(COLCALC::ignore_mixed_race_penalty, 0,
               sizeof(COLCALC::ignore_mixed_race_penalty));
        COLCALC::housing_creates_no_farmers = 0;
        COLCALC::new_farmer_food_threshold = 0;
        COLCALC::housing_no_popagg = 0;
        COLCALC::housing_formula_factor = 40;
        COLCALC::planet_extinction = 0;
        COLCALC::zero_colony_lost = 0;
        COLCALC::feudal_affects_freighters = 0;
        COLCALC::freighters_cash_bonus = 0;
        COLCALC::multibuild = 0;
        COLCALC::multibuild_nonbuilding_products_only = 0;
        memset(
            DESIGN::rebuild_ship_designs_after_field,
            0,
            sizeof(DESIGN::rebuild_ship_designs_after_field));
        DESIGN::rebuild_ship_designs_after_field[TECH_FIELD_CHEMISTRY] = true;
        DESIGN::rebuild_ship_designs_after_field[TECH_FIELD_NUCLEAR_FISSION] = true;
        DESIGN::rebuild_ship_designs_after_field[TECH_FIELD_PHYSICS] = true;
        COLCALC::lander_ship_cost.colony_ship = 500;
        COLCALC::lander_ship_cost.outpost_ship = 100;
        COLCALC::lander_ship_cost.transport_ship = 100;
        COLCALC::cloning_center_growth_bonus = 100;
        COLCALC::misc_costs.freighters = 50;
        COLCALC::misc_costs.android_farmer = 50;
        COLCALC::misc_costs.android_worker = 50;
        COLCALC::misc_costs.android_scientist = 50;
        COLCALC::misc_costs.spy = 100;
        COLCALC::misc_costs.terraforming_step = 250;
        memset(MOX::food_per_farmer_table, 0, sizeof(MOX::food_per_farmer_table));
    }

    int32_t Destroy_Colony_Reason_() {
        return destroy_colony_reason;
    }

    void Set_Add_Spy_Result_(int16_t result) {
        add_spy_result = result;
    }

    int16_t Deleted_Product_() {
        return deleted_product;
    }

    void Set_Population_Boom_(bool enabled) {
        population_boom = enabled;
    }

    void Set_Plague_(bool enabled) {
        plague = enabled;
    }

    int Custom_Autobuild_Calls_() {
        return custom_autobuild_calls;
    }

    void Set_Legacy_Autobuild_Product_(int16_t product) {
        legacy_autobuild_product = product;
    }

    int Calc_Ship_Level_Calls_() {
        return calc_ship_level_calls;
    }

    int Init_Ship_Designs_Calls_() {
        return init_ship_designs_calls;
    }

    void Set_Population_Growth_Adjustment_(int16_t adjustment) {
        population_growth_adjustment = adjustment;
    }

    int Population_Growth_Calls_() {
        return population_growth_calls;
    }

    const orion2re::lua::game::s_population_growth_context&
    Population_Growth_Context_() {
        return population_growth_context;
    }
} // namespace TEST_COLONY_POP_GROWTH

namespace orion2re::lua::game {
    std::int16_t Modify_Population_Growth(
        const s_population_growth_context* context, std::int64_t growth) {
        ++population_growth_calls;
        population_growth_context = *context;
        return static_cast<std::int16_t>(
            growth + population_growth_adjustment);
    }
} // namespace orion2re::lua::game

namespace MOX {
    int16_t NUM_COLONIES = 1;
    int16_t NUM_PLAYERS = 1;
    int16_t NUM_STARS = 0;
    s_colony* colony = colonies;
    s_planet_data* planet = &planet_storage[1];
    s_player* player = players;
    s_star_data* star = stars;
    s_ship_data* ship = ships;
    s_leader_data* leaders = ::leaders;
    s_settings settings = {};
    s_crew_data crew_data[CREW_QUALITY_COUNT] = {};
    uint8_t food_per_farmer_table[PLANET_CLIMATE_COUNT] = {};
}

namespace TECHDATA {
    s_tech_building_data buildings[BUILDING_COUNT] = {};
    s_tech_app_data technology_applications[TECH_APP_COUNT] = {};
    s_tech_field_data technology_fields[TECH_FIELD_COUNT] = {};
}

namespace EVENTS {
    int16_t __cdecl Event_Check_Space_Anomaly(int16_t) {
        return 0;
    }

    int16_t __cdecl Event_Check_Colony_Researching(int16_t) {
        return 0;
    }

    int16_t __cdecl Event_Check_Population_Boom(int16_t) {
        return population_boom;
    }

    int16_t __cdecl Event_Check_Plague(int16_t) {
        return plague;
    }
}

namespace OFFICER {
    int16_t __cdecl Owned_Officer_Level(int16_t) {
        return 0;
    }
}

namespace special {
    uint32_t __cdecl isqrt(uint32_t) {
        return 100;
    }
}

namespace MSG_ {
    void __cdecl Add_Msg(s_MSG, int16_t) {
    }
}

namespace RUSS {
    void __cdecl Shuffle_Sint(int16_t*, int16_t) {
    }

    int16_t __cdecl Get_Weighted_Choice_Int(uint32_t*, int16_t) {
        return 1;
    }
}

namespace BOMB {
    void __cdecl Destroy_Colony(s_colony*, int32_t capturing_player) {
        destroy_colony_reason = capturing_player;
    }
}

namespace ERICNET {
    void __cdecl Add_Building(int16_t colony_idx, int16_t building_idx) {
        MOX::colony[colony_idx].buildings[building_idx] = 1;
    }

    void __cdecl Calc_Ship_Level(int16_t) {
        ++calc_ship_level_calls;
    }

    void __cdecl Remove_Building(int16_t, int16_t) {
    }
}

namespace AIBUILD {
    void __cdecl Player_Colony_Autobuild(s_colony* colony) {
        if (legacy_autobuild_product != COLONY_PRODUCTION_NONE) {
            colony->producing[0] = legacy_autobuild_product;
        }
    }
}

namespace BILL {
    int16_t __cdecl Add_Their_Spy(int16_t, int16_t) {
        return add_spy_result;
    }
}

namespace BUILD_QUEUE {
    bool Apply_Custom_Autobuild(int16_t) {
        ++custom_autobuild_calls;
        return false;
    }
}

namespace COLBLDG {
    int16_t __cdecl Create_Ship(int16_t, int16_t, int16_t) {
        return -1;
    }

    void __cdecl Delete_Prod_From_Colony_Queue(int16_t, int16_t product) {
        deleted_product = product;
    }
}

namespace DESIGN {
    int16_t __cdecl Cost_Of_Design(int16_t, s_ship_design*) {
        return 0;
    }

    void __cdecl Cost_Reduction_For_Govt_Type(
        int16_t player_idx, int32_t* cost) {
        const int8_t government =
            MOX::player[player_idx].traits[TRAIT_CURRENT_GOVERNMENT];
        if ((uint8_t)government >= GOVERNMENT_COUNT) {
            return;
        }

        const s_ratio16& reduction =
            COLCALC::government_bonuses[government].cost_reduction;
        if (reduction.denominator == 0) {
            return;
        }

        *cost -= (int32_t)(
            (int64_t)*cost * reduction.numerator /
            reduction.denominator);
    }
}

namespace INITSHIP {
    void __cdecl Init_Ship_Designs(int16_t) {
        ++init_ship_designs_calls;
    }
}

namespace COLCALC {
    void __cdecl Player_Gets_Tech_App(s_player* player, int16_t tech_app_id) {
        player->tech_applications[tech_app_id] = TECH_RESEARCH_STATUS_RESEARCHED;
    }

    int16_t __cdecl Colony_Empire_Base_Food2_Produced(
        const s_colony*, int16_t, int16_t, s_colony_job_production*) {
        return 8;
    }

    int16_t __cdecl Colony_Empire_Base_Industry_Produced(
        const s_colony*, int16_t, int16_t, s_colony_job_production*) {
        return 6;
    }

    int16_t __cdecl Colony_Empire_Base_Research_Produced(
        const s_colony*, int16_t, int16_t, s_colony_job_production*) {
        return 4;
    }

    void __cdecl Colony_Morale(s_colony*, STRBUILD::s_builder*) {
    }

    void __cdecl Colony_Industry_Production(s_colony*, STRBUILD::s_builder*) {
    }

    void __cdecl Colony_Food_Production(s_colony*, STRBUILD::s_builder*) {
    }

    void __cdecl Colony_Replicators(s_colony*) {
    }

    void __cdecl Colony_Research_Production(s_colony*, STRBUILD::s_builder*) {
    }

    void __cdecl Colony_BC_Production(s_colony*, STRBUILD::s_builder*) {
    }

    void __cdecl Pass_Out_Imports(int16_t, s_colony*) {
    }

    void __cdecl Update_Player_Stats(s_player*) {
    }
}

namespace COLONY {
    bool __cdecl Colony_Pop_Is_Annihilation_Target(
        const s_colony*, uint32_t) {
        return false;
    }

    void __cdecl Fix_Colony_Queue(int16_t colony_idx) {
        s_colony& colony = MOX::colony[colony_idx];
        int destination = 0;
        for (int source = 0; source < 7; ++source) {
            if (colony.producing[source] != COLONY_PRODUCTION_NONE) {
                colony.producing[destination++] = colony.producing[source];
            }
        }
        while (destination < 7) {
            colony.producing[destination++] = COLONY_PRODUCTION_NONE;
        }
    }
}

namespace game_random {
    uint32_t __cdecl Random(uint32_t) {
        return 1;
    }
}

namespace GEO {
    int32_t __cdecl Parsecs_Between_Stars(int16_t, int16_t) {
        return 0;
    }
}