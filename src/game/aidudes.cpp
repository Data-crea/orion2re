#include "pch.h"

namespace aidudes {

    bool builds_colony_bases_prewarp = false;
    int16_t n_our_blockaded;
    int16_t n_our_unblockaded;
    s_aidudes_colony_info* our_blockaded;
    s_player* p_ai;
    s_aidudes_colony_info* our_unblockaded;
    int16_t n_our_colonies;
    s_aidudes_colony_info* Sort_Pops_colony_info;
    s_colony* Sort_Pops_colony_data;
    int16_t i_ai;
    s_aidudes_colony_info (*our_colonies)[MAX_COLONIES];

    void __cdecl All_Colony_AI() {
        int16_t num_players = (int16_t)MOX::NUM_PLAYERS;
        s_player* plr = MOX::player;

        for (int16_t i = 0; i < num_players; ++i, ++plr) {
            if (plr->objectives != PLAYER_OBJECTIVE_HUMAN) {
                RUSS::Mox_Update();
                Colony_AI(i);
            }
        }

        RUSS::Mox_Update();

        s_colony* col = MOX::colony;
        s_colony* end = MOX::colony + (int16_t)MOX::NUM_COLONIES;
        while (col < end) {
            if (col->producing[0] == COLONY_PRODUCTION_AI_TRADE_GOODS) {
                col->producing[0] = COLONY_PRODUCTION_TRADE_GOODS;
            }
            ++col;
        }
    }

    void __cdecl Industry_Sanity_Check() {
        int n_colonies = (int)aidudes::n_our_unblockaded;
        s_aidudes_colony_info* colony_infos = aidudes::our_unblockaded;

        for (int i = 0; i < n_colonies; ++i) {
            s_aidudes_colony_info* info = &colony_infos[i];
            int16_t colony_idx = info->colony_idx;
            s_colony* col = &MOX::colony[colony_idx];

            bool is_colony_producing_trade_goods = (col->producing[0] == COLONY_PRODUCTION_TRADE_GOODS);

            if ((is_colony_producing_trade_goods && aidudes::p_ai->do_not_research == 0) ||
                EVENTS::Event_Check_Colony_Researching(colony_idx) != 0) {
                for (int j = (int)info->pop_tail_idx - 1; j >= (int)info->first_assignable_pop_idx; --j) {
                    uint32_t* pop_entry = &col->pop[j];

                    if (!POP::Is_Worker(POP::Load(pop_entry))) {
                        break;
                    }

                    POP::Set_Prof(pop_entry, POP::PROF_SCIENTIST);
                }

                COLCALC::Colony_Calculation(colony_idx);
            }
        }
    }

    void __cdecl Colony_AI_Given_Tax() {
        for (int i = 0; i < aidudes::n_our_blockaded; ++i) {
            aidudes::Do_Blockaded_Colony(&aidudes::our_blockaded[i]);
        }

        for (int i = 0; i < aidudes::n_our_unblockaded; ++i) {
            aidudes::Do_Unblockaded_Colony(&aidudes::our_unblockaded[i]);
        }

        aidudes::Assign_Additional_Research_Industry();
        aidudes::Assign_Empire_Building(aidudes::i_ai);
        aidudes::Assign_Buildings();
        aidudes::Industry_Sanity_Check();

        if (aidudes::n_our_unblockaded > 0) {
            aidudes::Assign_Additional_Unblockaded_Farmers();
        } else {
            COLCALC::Do_Player_Colony_Post_Production(aidudes::i_ai);
        }
    }

    void __cdecl Colony_AI(int16_t i_ai) {
        s_aidudes_colony_info* our_colonies = ai_state::Prepare_Colony_State(i_ai);
        bool free_our_colonies = false;
        if (our_colonies == nullptr) {
            our_colonies = (s_aidudes_colony_info*)calloc(
                MAX_COLONIES, sizeof(s_aidudes_colony_info));
            if (our_colonies == nullptr) {
                allocate::Allocation_Error(MAX_COLONIES * sizeof(s_aidudes_colony_info));
            }
            free_our_colonies = true;
        }

        aidudes::i_ai = i_ai;
        aidudes::our_colonies = (s_aidudes_colony_info(*)[MAX_COLONIES])our_colonies;
        aidudes::p_ai = &MOX::player[i_ai];

        aidudes::Collect_AI_Colonies();

        if (MOX::settings.game_difficulty > GAME_DIFFICULTY_TUTOR) {
            if (AIBUILD::Player_War_Level(aidudes::i_ai) < 3) {
                aidudes::Clear_Build_Queues_If_New_Tech(aidudes::p_ai->got_new_app);
            }
        }

        aidudes::Keep_Ship_Designs_Up_To_Date();
        aidudes::Freighter_Cheat(aidudes::i_ai);
        aidudes::Do_Ship_Upgrade_Cheats();
        AIBUILD::Empire_Buy_Outright(aidudes::i_ai);
        aidudes::Colony_AI_Given_Tax();
        RUSS::Mox_Update();

#if defined(ORION2RE_ENABLE_DEBUG_OVERLAY)
        ai_state::Capture_Colony_State(i_ai, *aidudes::our_colonies,
            aidudes::n_our_colonies, aidudes::n_our_blockaded);
#endif

        aidudes::our_colonies = nullptr;
        aidudes::our_blockaded = nullptr;
        aidudes::our_unblockaded = nullptr;
        aidudes::p_ai = nullptr;
        aidudes::Sort_Pops_colony_info = nullptr;
        aidudes::Sort_Pops_colony_data = nullptr;
        if (free_our_colonies) free(our_colonies);
    }

    void __cdecl Clear_Build_Queues_If_New_Tech(bool force_clear) {
        s_aidudes_colony_info* current_info = *our_colonies;
        int num_colonies = (int)n_our_colonies;

        if (num_colonies == 0) return;

        for (int i = 0; i < num_colonies; ++i, ++current_info) {
            int16_t colony_idx = current_info->colony_idx;
            s_colony* colony = &MOX::colony[colony_idx];

            bool should_rebuild = force_clear;
            if (!should_rebuild) {
                int16_t producing = colony->producing[0];
                if (producing == COLONY_PRODUCTION_NONE ||
                    producing == COLONY_PRODUCTION_HOUSING ||
                    producing == COLONY_PRODUCTION_TRADE_GOODS ||
                    producing == COLONY_PRODUCTION_REPEAT) {
                    should_rebuild = true;
                }
            }

            if (should_rebuild) {
                int16_t producing = colony->producing[0];
                if (COLBLDG::Colony_Production_Is_Queued_Ship(producing)) {
                    int16_t ship_idx = COLBLDG::Colony_Production_Ship_Index(producing);
                    if (MOX::ship[ship_idx].status == 4) {
                        return; // Abandon evaluation entirely if an outdated ship is being built
                    }
                }

                uint8_t weights[BUILDING_COUNT + 1];
                memset(weights, 0, sizeof(weights));
                weights[BUILDING_COUNT] = 55; // Default/Keep Current weight

                if (colony->n_pops >= 5) {
                    // TECH_APP_SPACEPORT (164), BUILDING_SPACEPORT (39)
                    if (p_ai->tech_applications[TECH_APP_SPACEPORT] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_SPACEPORT] == 0) {
                        weights[BUILDING_SPACEPORT] = colony->n_pops + 25;
                    }
                    // TECH_APP_PLANETARY_STOCK_EXCHANGE (135), BUILDING_STOCK_EXCHANGE (29)
                    if (p_ai->tech_applications[TECH_APP_PLANETARY_STOCK_EXCHANGE] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_STOCK_EXCHANGE] == 0) {
                        weights[BUILDING_STOCK_EXCHANGE] = colony->n_pops + 20;
                    }
                }

                // TECH_APP_SUBTERRANEAN_FARMS (178), BUILDING_SUBTERRANEAN_FARMS (43)
                if (p_ai->tech_applications[TECH_APP_SUBTERRANEAN_FARMS] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_SUBTERRANEAN_FARMS] == 0) {
                    weights[BUILDING_SUBTERRANEAN_FARMS] = 20;
                }
                // TECH_APP_HYDROPONIC_FARM (87), BUILDING_HYDROPONIC_FARM (21)
                if (p_ai->tech_applications[TECH_APP_HYDROPONIC_FARM] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_HYDROPONIC_FARM] == 0) {
                    weights[BUILDING_HYDROPONIC_FARM] = 20;
                }
                // TECH_APP_AUTOMATED_FACTORIES (22), BUILDING_AUTOMATED_FACTORY (7)
                if (p_ai->tech_applications[TECH_APP_AUTOMATED_FACTORIES] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_AUTOMATED_FACTORY] == 0) {
                    weights[BUILDING_AUTOMATED_FACTORY] = 35;
                }
                // TECH_APP_ROBO_MINERS (156), BUILDING_ROBO_MINER_PLANT (36)
                if (p_ai->tech_applications[TECH_APP_ROBO_MINERS] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_ROBO_MINER_PLANT] == 0) {
                    weights[BUILDING_ROBO_MINER_PLANT] = 20;
                }
                // TECH_APP_DEEP_CORE_MINING (49), BUILDING_DEEP_CORE_MINE (12)
                if (p_ai->tech_applications[TECH_APP_DEEP_CORE_MINING] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_DEEP_CORE_MINE] == 0) {
                    weights[BUILDING_DEEP_CORE_MINE] = 10;
                }
                // TECH_APP_ROBOTIC_FACTORY (154), BUILDING_ROBOTIC_FACTORY (34)
                if (p_ai->tech_applications[TECH_APP_ROBOTIC_FACTORY] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_ROBOTIC_FACTORY] == 0) {
                    weights[BUILDING_ROBOTIC_FACTORY] = 7;
                }

                if (colony->n_pops >= 5) {
                    // TECH_APP_RECYCLOTRON (152), BUILDING_RECYCLOTRON (33)
                    if (p_ai->tech_applications[TECH_APP_RECYCLOTRON] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_RECYCLOTRON] == 0) {
                        weights[BUILDING_RECYCLOTRON] = colony->n_pops + 15;
                    }
                    if (p_ai->do_not_research == 0) {
                        // TECH_APP_RESEARCH_LABORATORY (155), BUILDING_RESEARCH_LAB (35)
                        if (p_ai->tech_applications[TECH_APP_RESEARCH_LABORATORY] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_RESEARCH_LAB] == 0) {
                            weights[BUILDING_RESEARCH_LAB] = 20;
                        }
                        // TECH_APP_PLANETARY_SUPERCOMPUTER (136), BUILDING_SUPERCOMPUTER (30)
                        if (p_ai->tech_applications[TECH_APP_PLANETARY_SUPERCOMPUTER] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_SUPERCOMPUTER] == 0) {
                            weights[BUILDING_SUPERCOMPUTER] = 10;
                        }
                    }
                }

                int player_idx = (int)(p_ai - MOX::player);
                int trait_check = MOX::player[player_idx].traits[TRAIT_CURRENT_GOVERNMENT] / 2;

                if (trait_check != GOVERNMENT_UNIFICATION / 2) {
                    if (trait_check == GOVERNMENT_DICTATORSHIP / 2 || trait_check == GOVERNMENT_FEUDAL / 2) {
                        // TECH_APP_MARINE_BARRACKS (103), BUILDING_MARINE_BARRACKS (22), BUILDING_ARMOR_BARRACKS (2)
                        if (p_ai->tech_applications[TECH_APP_MARINE_BARRACKS] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_MARINE_BARRACKS] == 0 && colony->buildings[BUILDING_ARMOR_BARRACKS] == 0) {
                            weights[BUILDING_MARINE_BARRACKS] = 37;
                        }
                        // TECH_APP_ARMOR_BARRACKS (14), BUILDING_ARMOR_BARRACKS (2), BUILDING_MARINE_BARRACKS (22)
                        if (p_ai->tech_applications[TECH_APP_ARMOR_BARRACKS] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_ARMOR_BARRACKS] == 0 && colony->buildings[BUILDING_MARINE_BARRACKS] == 0) {
                            weights[BUILDING_ARMOR_BARRACKS] = 10;
                        }
                    }
                    if (colony->n_pops >= 3) {
                        // TECH_APP_HOLO_SIMULATOR (86), BUILDING_HOLO_SIMULATOR (20)
                        if (p_ai->tech_applications[TECH_APP_HOLO_SIMULATOR] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_HOLO_SIMULATOR] == 0) {
                            weights[BUILDING_HOLO_SIMULATOR] = colony->n_pops + 25;
                        }
                        // TECH_APP_PLEASURE_DOME (141), BUILDING_PLEASURE_DOME (31)
                        if (p_ai->tech_applications[TECH_APP_PLEASURE_DOME] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_PLEASURE_DOME] == 0) {
                            weights[BUILDING_PLEASURE_DOME] = colony->n_pops + 10;
                        }
                    }
                }

                // TECH_APP_BIOSPHERES (61), BUILDING_BIOSPHERES (15)
                if (p_ai->tech_applications[TECH_APP_BIOSPHERES] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_BIOSPHERES] == 0) {
                    weights[BUILDING_BIOSPHERES] = 30;
                }

                if (colony->pollution >= 5 && current_info->eats_minerals == 0) {
                    // TECH_APP_CORE_WASTE_DUMPS (50), BUILDING_CORE_WASTE_DUMP (13)
                    if (p_ai->tech_applications[TECH_APP_CORE_WASTE_DUMPS] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_CORE_WASTE_DUMP] == 0) {
                        weights[BUILDING_CORE_WASTE_DUMP] = (uint8_t)special::isqrt(colony->pollution) + 8;
                    }
                    // TECH_APP_ATMOSPHERIC_RENEWER (19), BUILDING_ATMOSPHERE_RENEWER (5)
                    if (p_ai->tech_applications[TECH_APP_ATMOSPHERIC_RENEWER] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_ATMOSPHERE_RENEWER] == 0) {
                        weights[BUILDING_ATMOSPHERE_RENEWER] = (uint8_t)special::isqrt(colony->pollution) + 8;
                    }
                    // TECH_APP_POLLUTION_PROCESSOR (142), BUILDING_POLLUTION_PROCESSOR (32)
                    if (p_ai->tech_applications[TECH_APP_POLLUTION_PROCESSOR] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_POLLUTION_PROCESSOR] == 0) {
                        weights[BUILDING_POLLUTION_PROCESSOR] = 10;
                    }
                }

                // TECH_APP_PLANETARY_GRAVITY_GENERATOR (131), BUILDING_GRAVITY_GENERATOR (25)
                if (p_ai->tech_applications[TECH_APP_PLANETARY_GRAVITY_GENERATOR] == TECH_RESEARCH_STATUS_RESEARCHED && colony->buildings[BUILDING_GRAVITY_GENERATOR] == 0) {
                    int8_t gravity = MOX::planet[colony->planet].gravity_class;
                    if (gravity == PLANET_GRAVITY_LOW_G) {
                        if (p_ai->traits[TRAIT_LOW_G_WORLD] == 0) {
                            weights[BUILDING_GRAVITY_GENERATOR] = 10;
                        }
                    } else if (gravity == PLANET_GRAVITY_NORMAL_G) {
                        if (p_ai->traits[TRAIT_LOW_G_WORLD] != 0) {
                            weights[BUILDING_GRAVITY_GENERATOR] = 10;
                        }
                    } else if (gravity == PLANET_GRAVITY_HEAVY_G) {
                        if (p_ai->traits[TRAIT_HEAVY_G_WORLD] == 0) {
                            weights[BUILDING_GRAVITY_GENERATOR] = 20;
                        }
                    }
                }

                int16_t choice = RUSS::Get_Weighted_Choice_Char(weights, BUILDING_COUNT + 1);
                if (choice < BUILDING_COUNT) {
                    COLBLDG::Clear_Colony_Queue(colony_idx);
                    colony->producing[0] = choice;
                }
            }
        }
    }

    int __cdecl Sort_Colonies_Blockaded(const void* a, const void* b) {
        const s_aidudes_colony_info* info1 = (const s_aidudes_colony_info*)a;
        const s_aidudes_colony_info* info2 = (const s_aidudes_colony_info*)b;

        s_colony* colony1 = &MOX::colony[info1->colony_idx];
        s_colony* colony2 = &MOX::colony[info2->colony_idx];

        bool blockaded2 = COLCALC::Colony_Is_Blockaded(colony2);
        bool blockaded1 = COLCALC::Colony_Is_Blockaded(colony1);

        return (int)blockaded2 - (int)blockaded1;
    }

    void __cdecl Keep_Ship_Designs_Up_To_Date() {
        s_ship_design doom_star_design;
        doom_star_design.cost = 0;

        for (int i = 0; i < (int)aidudes::n_our_colonies; ++i) {
            int16_t colony_idx = (*aidudes::our_colonies)[i].colony_idx;
            short producing_id = MOX::colony[colony_idx].producing[0];

            if (COLBLDG::Colony_Production_Is_Queued_Ship(producing_id)) {
                int16_t ship_idx = COLBLDG::Colony_Production_Ship_Index(producing_id);
                s_ship_data& ship = MOX::ship[ship_idx];

                if (ship.d.ship_type == SHIP_TYPE_COMBAT) {
                    s_ship_design* source_design;
                    if (ship.d.size < SHIP_SIZE_DOOM_STAR) {
                        source_design = &aidudes::p_ai->ship_designs[(int)ship.d.size];
                    } else {
                        if (doom_star_design.cost == 0) {
                            INITSHIP::Load_Doom_Star_Ship_Design(
                                aidudes::i_ai, &doom_star_design, MOX::settings.strategic_combat_flag != 0);
                        }
                        source_design = &doom_star_design;
                    }
                    memcpy(&ship.d, source_design, sizeof(s_ship_design));
                }
            }
        }
    }

    void __cdecl Collect_AI_Colonies() {
        n_our_colonies = 0;
        n_our_unblockaded = 0;
        n_our_blockaded = 0;

        memset(*our_colonies, 0, (size_t)MOX::NUM_COLONIES * sizeof(s_aidudes_colony_info));

        for (int16_t i = 0; i < (int)MOX::NUM_COLONIES; ++i) {
            s_colony* colony_ptr = &MOX::colony[i];

            if (colony_ptr->owner == (char)i_ai) {
                if (EVENTS::Event_Check_Space_Anomaly(i) == 0 && colony_ptr->outpost_flag == 0) {
                    (*our_colonies)[n_our_colonies].colony_idx = i;

                    if (COLCALC::Colony_Is_Blockaded(colony_ptr)) {
                        n_our_blockaded++;
                    } else {
                        n_our_unblockaded++;
                    }
                    n_our_colonies++;
                }
            }
        }

        qsort(*our_colonies, (size_t)n_our_colonies, sizeof(s_aidudes_colony_info), Sort_Colonies_Blockaded);

        our_blockaded = (s_aidudes_colony_info*)*our_colonies;
        our_unblockaded = &((*our_colonies)[n_our_blockaded]);
    }

    void __cdecl Compute_Max_Min_R_I_Worthiness(s_aidudes_colony_info* info) {
        if (info->cur_pop_idx < info->pop_end_idx) {
            s_colony* colony = &MOX::colony[info->colony_idx];

            uint32_t* cur_pop_ptr = &colony->pop[info->cur_pop_idx];
            int16_t cur_prod_2 = COLCALC::Colony_Pop_Prod_Produced(colony, cur_pop_ptr, 2);
            int16_t cur_prod_1 = COLCALC::Colony_Pop_Prod_Produced(colony, cur_pop_ptr, 1);
            info->best_worker_to_farmer_score = cur_prod_2 - cur_prod_1;

            uint32_t* end_pop_ptr = &colony->pop[info->pop_end_idx - 1];
            int16_t end_prod_2 = COLCALC::Colony_Pop_Prod_Produced(colony, end_pop_ptr, 2);
            int16_t end_prod_1 = COLCALC::Colony_Pop_Prod_Produced(colony, end_pop_ptr, 1);
            info->worst_worker_to_farmer_score = end_prod_2 - end_prod_1;
        } else {
            info->best_worker_to_farmer_score = (int16_t)0x8001;
            info->worst_worker_to_farmer_score = 0x7FFF;
        }
    }

    void __cdecl Assign_Buildings() {
        s_aidudes_colony_info* it = *our_colonies;
        s_aidudes_colony_info* end = it + n_our_colonies;

        while (it < end) {
            AIBUILD::Assign_Colony_Building(&MOX::colony[it->colony_idx]);
            ++it;
            end = (*our_colonies) + n_our_colonies;
        }
    }

    void __cdecl Freighter_Cheat(int32_t player_idx) {
        s_player* plr = &MOX::player[player_idx];

        if (plr->surplus_freighters < 0 || plr->surplus_food < 0) {
            int32_t threshold = 1 - (int32_t)MOX::settings.game_difficulty * (int32_t)plr->surplus_freighters;
            int32_t roll = (int32_t)game_random::Random(50);

            if (threshold >= roll) {
                plr->n_freighters += 5;
            } else if (plr->n_freighters == 0 && MOX::settings.game_difficulty >= GAME_DIFFICULTY_EASY) {
                plr->n_freighters = 5;
            }
        }
    }

    void __cdecl Do_Ship_Upgrade_Cheats() {
        s_ai_refit_player* refit_player = &(*AIDATA::airefit)[aidudes::i_ai];
        int16_t num_entries = refit_player->n_entries;

        if (MOX::stardate <= 35035 /* 0x88db */) {
            return;
        }

        if (num_entries == 0) {
            return;
        }

        // Determine if AI is eligible for an upgrade cheat based on difficulty and last refit entry desirability
        int32_t desirability = refit_player->entry[num_entries - 1].desirability;
        uint8_t difficulty_idx = (uint8_t)MOX::settings.game_difficulty;
        int32_t difficulty_bonus = (int32_t)AIBUILD::difficulty_ship_design[difficulty_idx];
        int32_t upgrade_threshold = (difficulty_bonus + desirability) * 2;
        int upgrade_count = 0;

        if ((int32_t)game_random::Random(900) <= upgrade_threshold) {
            upgrade_count = 1;
        }

        // AI must have a stable empire (at least 2 colonies and sufficient population) to cheat
        if ((*AIDATA::ai_player_info)[aidudes::i_ai].colony_count < 2) {
            upgrade_count = 0;
        } else {
            int32_t total_pop = MOX::player[aidudes::i_ai].total_pop;
            if ((int32_t)game_random::Random((uint32_t)total_pop) <= 8) {
                upgrade_count = 0;
            }
        }

        // Iterate backwards through refit entries and perform the upgrade if conditions met
        for (int i = num_entries; i > 0; i--) {
            if (upgrade_count == 0) {
                break;
            }

            s_ai_refit_entry* entry = &refit_player->entry[i - 1];
            s_ship_data* ship = &MOX::ship[entry->ship_idx];
            int16_t location = ship->location;

            // Skip ships in systems with incoming human activity
            if ((*AIDATA::enroute_human_ships)[location][3] != 0) {
                continue;
            }

            // Only upgrade if the system is either uncolonized or owned strictly by this AI
            uint8_t has_colony_mask = MOX::star[location].has_colony;
            uint32_t other_players_mask = ~(1 << aidudes::i_ai);
            if ((has_colony_mask & other_players_mask) != 0) {
                continue;
            }

            // Apply design upgrade
            if (ship->d.size < SHIP_SIZE_DOOM_STAR) {
                ship->d = aidudes::p_ai->ship_designs[(int)ship->d.size];
            } else {
                INITSHIP::Load_Doom_Star_Ship_Design(
                    aidudes::i_ai, &ship->d, MOX::settings.strategic_combat_flag != 0);
            }

            upgrade_count--;

            // Remove the entry from the refit list
            refit_player->n_entries--;
            int entries_to_move = refit_player->n_entries - (i - 1);
            if (entries_to_move > 0) {
                memmove(&refit_player->entry[i - 1], &refit_player->entry[i], entries_to_move * sizeof(s_ai_refit_entry));
            }
        }
    }

    void __cdecl Assign_Additional_Research_Industry() {
        int industry_mult = 10;
        int research_mult = 18;

        if (aidudes::p_ai->surplus_bc < 0) {
            research_mult = 18 + (int)special::isqrt((uint32_t)-aidudes::p_ai->surplus_bc);
        }

        if (aidudes::p_ai->do_not_research == 0) {
            if (aidudes::p_ai->objectives == PLAYER_OBJECTIVE_TECHNOLOGIST) {
                industry_mult = 12;
            } else if (aidudes::p_ai->objectives == PLAYER_OBJECTIVE_INDUSTRIALIST) {
                research_mult += 2;
            }
        } else {
            industry_mult = 0;
        }

        COLCALC::Do_Player_Colony_Calculations(aidudes::i_ai);

        while (true) {
            s_aidudes_colony_info* best_colony = nullptr;

            if ((uint32_t)aidudes::p_ai->research_produced * (uint32_t)research_mult < (uint32_t)aidudes::p_ai->industry_produced * (uint32_t)industry_mult) {
                short max_score = (short)0x8001;
                for (int i = 0; i < (int)aidudes::n_our_unblockaded; ++i) {
                    if (aidudes::our_unblockaded[i].best_worker_to_farmer_score > max_score) {
                        max_score = aidudes::our_unblockaded[i].best_worker_to_farmer_score;
                        best_colony = &aidudes::our_unblockaded[i];
                    }
                }

                if (best_colony == nullptr) {
                    return;
                }
                aidudes::Grab_Scientist(best_colony);
            } else {
                short min_score = 0x7fff;
                for (int i = 0; i < (int)aidudes::n_our_unblockaded; ++i) {
                    if (aidudes::our_unblockaded[i].worst_worker_to_farmer_score < min_score) {
                        min_score = aidudes::our_unblockaded[i].worst_worker_to_farmer_score;
                        best_colony = &aidudes::our_unblockaded[i];
                    }
                }

                if (best_colony == nullptr) {
                    return;
                }
                aidudes::Grab_Worker(best_colony);
            }

            COLCALC::Colony_Calculation(best_colony->colony_idx);
        }
    }

    void __cdecl Grab_Scientist(s_aidudes_colony_info* colony_info) {
        int16_t colony_idx = colony_info->colony_idx;
        uint8_t cur_pop_idx = (uint8_t)colony_info->cur_pop_idx;

        s_colony& colony = MOX::colony[colony_idx];

        uint32_t* pop_flags = &colony.pop[cur_pop_idx];

        POP::Set_Prof(pop_flags, POP::PROF_SCIENTIST);
        POP::Set_Assigned(pop_flags, true);

        colony_info->cur_pop_idx = cur_pop_idx + 1;

        Compute_Max_Min_R_I_Worthiness(colony_info);
    }

    void __cdecl Grab_Worker(s_aidudes_colony_info* info) {
        s_colony* colony = &MOX::colony[info->colony_idx];

        info->pop_end_idx--;

        uint32_t* pop_record = &colony->pop[(uint8_t)info->pop_end_idx];

        POP::Set_Prof(pop_record, POP::PROF_WORKER);
        POP::Set_Assigned(pop_record, true);

        Compute_Max_Min_R_I_Worthiness(info);
    }

    void __cdecl Do_Unblockaded_Colony(s_aidudes_colony_info* info) {
        s_colony* colony = &MOX::colony[info->colony_idx];
        uint8_t n_pops = (uint8_t)colony->n_pops;

        // Calculate minimum required workers (ceiling of n_pops / 5)
        int32_t n_required_workers = (n_pops + 4) / 5;

        Assign_Required_Colonists(info);

        Sort_Pops_colony_data = colony;
        qsort(&colony->pop[info->first_assignable_pop_idx],
            n_pops - info->first_assignable_pop_idx,
            4,
            Sort_Pops_FR_I);

        if (EVENTS::Event_Check_Colony_Researching(info->colony_idx)) {
            n_required_workers = 0;
        }

        COLCALC::Pre_Import_Computing(colony);

        // Main loop to shift population to workers based on industry needs
        while (info->cur_pop_idx < info->pop_tail_idx) {
            int32_t needed_industry = (uint8_t)colony->industry2_needed_for_conquered +
                (uint8_t)colony->industry2_needed_for_our_empire +
                (uint8_t)colony->industry2_needed_for_androids +
                (uint8_t)colony->industry2_needed_for_assimilated;

            if (info->n_workers >= n_required_workers && needed_industry <= colony->production[1] * 2) {
                break;
            }

            info->pop_tail_idx--;
            uint8_t idx = info->pop_tail_idx;
            POP::Set_Prof(&colony->pop[idx], POP::PROF_WORKER);
            POP::Set_Assigned(&colony->pop[idx], true);
            info->n_workers++;

            COLCALC::Pre_Import_Computing(colony);
        }

        info->pop_end_idx = info->pop_tail_idx;
        info->pollution_at_start = (uint8_t)colony->pollution;

        Sort_Pops_colony_data = colony;
        qsort(&colony->pop[info->first_assignable_pop_idx],
            info->pop_end_idx - info->cur_pop_idx,
            4,
            Sort_Pops_R_I);

        Compute_Max_Min_R_I_Worthiness(info);
    }

    static void Assign_Blockaded_Non_Farmers(s_colony* colony, s_aidudes_colony_info* colony_info) {
        int16_t is_researching = EVENTS::Event_Check_Colony_Researching(colony_info->colony_idx);
        POP::e_prof prof = is_researching == 0 ? POP::PROF_WORKER : POP::PROF_SCIENTIST;

        while ((uint8_t)colony_info->cur_pop_idx < (uint8_t)colony_info->pop_end_idx) {
            uint32_t* pop = &colony->pop[(uint8_t)colony_info->cur_pop_idx];
            POP::Set_Prof(pop, prof);
            POP::Set_Assigned(pop, true);
            colony_info->cur_pop_idx++;
        }
    }

    void __cdecl Do_Blockaded_Colony(s_aidudes_colony_info* colony_info) {
        s_colony* colony = &MOX::colony[colony_info->colony_idx];
        Assign_Required_Colonists(colony_info);

        Sort_Pops_colony_data = colony;

        qsort(&colony->pop[(uint8_t)colony_info->first_assignable_pop_idx],
            (uint8_t)colony->n_pops - (uint8_t)colony_info->first_assignable_pop_idx,
            4,
            Sort_Pops_FI);

        if (colony->food2_per_farmer == 0) {
            Assign_Blockaded_Non_Farmers(colony, colony_info);
        } else {
            COLCALC::Pre_Import_Computing(colony);

            while ((uint8_t)colony_info->cur_pop_idx < (uint8_t)colony_info->pop_end_idx) {
                int32_t total_food_needed = (uint8_t)colony->food2_needed_for_our_empire +
                    (uint8_t)colony->food2_needed_for_assimilated +
                    (uint8_t)colony->food2_needed_for_conquered +
                    (uint8_t)colony->food2_needed_for_natives;

                if (total_food_needed > colony->production[0] * 2) {
                    if ((uint8_t)colony_info->n_farmers < (uint8_t)colony->max_farms) {
                        uint32_t* pop = &colony->pop[(uint8_t)colony_info->cur_pop_idx];
                        POP::Set_Prof(pop, POP::PROF_FARMER);
                        POP::Set_Assigned(pop, true);
                        colony_info->cur_pop_idx++;
                        colony_info->n_farmers++;
                    } else if (colony->food2_per_farmer == 0) {
                        Assign_Blockaded_Non_Farmers(colony, colony_info);
                        break;
                    }
                } else {
                    colony_info->pop_end_idx--;
                    uint32_t* pop = &colony->pop[(uint8_t)colony_info->pop_end_idx];
                    POP::Set_Prof(pop, POP::PROF_WORKER);
                    POP::Set_Assigned(pop, true);
                }
                COLCALC::Pre_Import_Computing(colony);
            }
        }

        COLCALC::Pre_Import_Computing(colony);
    }

    int16_t __cdecl FI_Worthiness(const s_colony* colony, const uint32_t* pop) {
        int16_t worthiness_0;
        int16_t worthiness_1;

        worthiness_0 = COLCALC::Colony_Pop_Prod_Produced(colony, pop, 0);
        worthiness_1 = COLCALC::Colony_Pop_Prod_Produced(colony, pop, 1);

        return worthiness_0 - worthiness_1;
    }

    int __cdecl Sort_Pops_FI(const void* a, const void* b) {
        const uint32_t* pop1 = (const uint32_t*)a;
        const uint32_t* pop2 = (const uint32_t*)b;
        int16_t worth1 = FI_Worthiness(Sort_Pops_colony_data, pop1);
        int16_t worth2 = FI_Worthiness(Sort_Pops_colony_data, pop2);

        if (worth2 == worth1) {
            int16_t prod1 = COLCALC::Colony_Pop_Prod_Produced(Sort_Pops_colony_data, pop1, 0);
            int16_t prod2 = COLCALC::Colony_Pop_Prod_Produced(Sort_Pops_colony_data, pop2, 0);

            // If worthiness is equal, sort by production ascending
            return (int)prod1 - (int)prod2;
        }

        // Sort by worthiness descending
        return (int)worth2 - (int)worth1;
    }

    void __cdecl Assign_Required_Colonists(s_aidudes_colony_info* colony_info) {
        s_colony* colony = &MOX::colony[colony_info->colony_idx];
        uint32_t* pop_ptr = colony->pop;
        char do_not_research = MOX::player[(int)colony->owner].do_not_research;
        int16_t non_immune_pops = 0;

        colony_info->first_assignable_pop_idx = 0;
        colony_info->n_farmers = 0;
        colony_info->n_workers = 0;

        for (int i = 0; i < (int)colony->n_pops; ++i) {
            uint32_t pop_entry = POP::Load(&pop_ptr[i]);
            uint32_t race_idx = POP::Get_Race(pop_entry);

            if (POP::Is_Android_Or_Native(pop_entry)) {
                colony_info->first_assignable_pop_idx++;
                POP::e_prof prof = POP::Get_Prof(pop_entry);

                if (prof == POP::PROF_FARMER) {
                    colony_info->n_farmers++;
                } else if (prof == POP::PROF_WORKER) {
                    colony_info->n_workers++;
                } else if (prof == POP::PROF_SCIENTIST) {
                    if (do_not_research != 0) {
                        POP::Set_Assigned(&pop_ptr[i], false);
                    }
                }
            } else {
                POP::Set_Assigned(&pop_ptr[i], false);
            }

            // Re-evaluate current pop entry state after potential modifications
            uint32_t current_pop = POP::Load(&pop_ptr[i]);
            uint32_t current_race = POP::Get_Race(current_pop);

            // Check if population is affected by pollution (not Android/Native and not Tolerant)
            if (current_race < POP::RACE_ANDROID) {
                if (MOX::player[current_race].traits[TRAIT_ENVIRONMENT_IMMUNE] == 0) {
                    if (!POP::Is_Conquered(current_pop) || (colony->occupation_policy >= 2 && colony->occupation_policy <= 3)) {
                        non_immune_pops++;
                    }
                }
            }
        }

        // Determine if Lithovore trait (eats minerals) applies to this colony
        if ((non_immune_pops * 2 < (int)colony->n_pops) &&
            (MOX::player[(int)colony->owner].traits[TRAIT_EATS_MINERALS] != 0) &&
            (MOX::player[(int)colony->owner].surplus_food >= 0)) {
            colony_info->eats_minerals = 1;
        }

        // Sort population for AI processing
        aidudes::Sort_Pops_colony_data = colony;
        qsort(colony->pop, (size_t)colony->n_pops, 4, aidudes::Sort_Pops_Fixed);

        colony_info->cur_pop_idx = colony_info->first_assignable_pop_idx;
        colony_info->pop_end_idx = colony->n_pops;
        colony_info->pop_tail_idx = colony->n_pops;
    }

    int __cdecl Sort_Pops_Fixed(const void* a, const void* b) {
        const bool flag1 = POP::Is_Android_Or_Native(POP::Load(a));
        const bool flag2 = POP::Is_Android_Or_Native(POP::Load(b));
        return (int)flag2 - (int)flag1;
    }

    int __cdecl _Sort_Pops_colony(const void* a, const void* b) {
        s_colony* colony = aidudes::Sort_Pops_colony_data;

        // Get production values for both pops across two different production types
        int16_t p1_prod_1 = COLCALC::Colony_Pop_Prod_Produced(colony, (const uint32_t*)a, 1);
        int16_t p2_prod_1 = COLCALC::Colony_Pop_Prod_Produced(colony, (const uint32_t*)b, 1);
        int16_t p1_prod_2 = COLCALC::Colony_Pop_Prod_Produced(colony, (const uint32_t*)a, 2);
        int16_t p2_prod_2 = COLCALC::Colony_Pop_Prod_Produced(colony, (const uint32_t*)b, 2);

        // Calculate differences in production contributions
        int16_t diff1 = p1_prod_2 - p1_prod_1;
        int16_t diff2 = p2_prod_2 - p2_prod_1;

        // Primary sort: difference between production types
        if (diff2 != diff1) {
            return (int)diff2 - (int)diff1;
        }

        // Secondary sort: specific production type 1 contribution
        if (p1_prod_1 != p2_prod_1) {
            return (int)p1_prod_1 - (int)p2_prod_1;
        }

        // Tertiary sort: masked raw value comparison
        uint32_t val1 = POP::Load(a);
        uint32_t val2 = POP::Load(b);

        return (int)POP::Get_Race(val1) - (int)POP::Get_Race(val2);
    }

    void __cdecl Assign_Empire_Building(int16_t player_idx) {
        s_player* p = &MOX::player[player_idx];
        s_ai_refit_player* p_refit = &(*AIDATA::airefit)[player_idx];

        int war_level = AIBUILD::Player_War_Level(player_idx);
        int colony_queue_count = 0;

        bool is_militarist = (p->objectives == PLAYER_OBJECTIVE_MILITARIST);
        bool low_money = (p->bc + p->surplus_bc < p->total_maintenance);

        int isqrt_val = 0;
        if (low_money || p->bc < 1500) {
            isqrt_val = 0;
        } else {
            isqrt_val = special::isqrt((p->surplus_bc + ((p->surplus_bc >> 31) & 0x3f)) >> 6);
        }

        if (isqrt_val > 10) {
            isqrt_val = 10;
        }

        RUSS::Mox_Update();

        if (p->war_buildup_duration != 0 && war_level == 0) {
            war_level = is_militarist + 1;
        }

        bool recalculate_colony = false;
        int16_t colony_queue[MAX_COLONIES];

        if (MOX::NUM_COLONIES > 0) {
            for (int16_t current_colony_idx = 0; current_colony_idx < MOX::NUM_COLONIES; ++current_colony_idx) {
                s_colony* temp_colony = &MOX::colony[current_colony_idx];

                if (temp_colony->owner == (char)player_idx &&
                    temp_colony->outpost_flag == 0 &&
                    temp_colony->n_pops != 0) {
                    if (EVENTS::Event_Check_Space_Anomaly(current_colony_idx) == 0) {
                        RUSS::Mox_Update();

                        if (COLCALC::Colony_Is_Blockaded(temp_colony) ||
                            (*AIDATA::ai_colony_info)[current_colony_idx].threatened != 0 ||
                            low_money) {
                            bool delete_from_queue = false;
                            int16_t prod = temp_colony->producing[0];
                            if (COLBLDG::Colony_Production_Is_Queued_Ship(prod)) {
                                if (MOX::ship[COLBLDG::Colony_Production_Ship_Index(prod)].d.ship_type != SHIP_TYPE_COMBAT) {
                                    delete_from_queue = true;
                                }
                            } else if (prod > COLONY_PRODUCTION_REFIT) {
                                if (prod > COLONY_PRODUCTION_FARMER) {
                                    if (prod == COLONY_PRODUCTION_TRADE_GOODS) {
                                        recalculate_colony = true;
                                        delete_from_queue = true;
                                    }
                                } else {
                                    delete_from_queue = true;
                                }
                            }

                            if (delete_from_queue) {
                                COLBLDG::Delete_From_Colony_Queue(current_colony_idx, 0);
                            }
                        }

                        int16_t prod = temp_colony->producing[0];
                        if (prod == COLONY_PRODUCTION_HOUSING) {
                            // pass
                        } else if (prod == COLONY_PRODUCTION_TRADE_GOODS) {
                            recalculate_colony = true;
                        } else if (prod == COLONY_PRODUCTION_NONE) {
                            // pass
                        } else {
                            continue;
                        }

                        temp_colony->producing[0] = COLONY_PRODUCTION_AI_UNASSIGNED;
                        colony_queue[colony_queue_count] = current_colony_idx;
                        colony_queue_count++;
                    }
                }
            }
        }

        if (recalculate_colony) {
            COLCALC::Do_Player_Colony_Calculations(player_idx);
        }

        if (colony_queue_count <= 0) {
            return;
        }

        bool no_range_or_speed;
        if (p->ship_range == 0 || p->ship_speed == 0) {
            no_range_or_speed = true;
        } else {
            no_range_or_speed = false;
        }

        int freighter_groups_assigned = 0;
        int16_t best_planet = AIMOVE::Best_Uncolonized_Undestined_Planet(player_idx);
        bool is_expansionist = (p->objectives == PLAYER_OBJECTIVE_EXPANSIONIST);

        s_ship_design ship_design;
        ship_design.cost = 0;

        for (int i = 0; i < colony_queue_count;) {
            s_colony* temp_colony = &MOX::colony[colony_queue[i]];
            RUSS::Mox_Update();
            bool changed = false;

            if (AIBUILD::strategic_production.emergency_defense.enabled &&
                ((war_level != 0 && (*AIDATA::enroute_human_ships)[MOX::planet[temp_colony->planet].star_index][9] != 0) ||
                    AIBUILD::Colony_Is_Conquered(temp_colony))) {
                for (int32_t order_idx = 0; order_idx < AIBUILD::EMERGENCY_DEFENSE_PRODUCT_COUNT; ++order_idx) {
                    int16_t product = AIBUILD::strategic_production.emergency_defense.order[order_idx];
                    if (product > BUILDING_NO_BUILDING && product < BUILDING_COUNT &&
                        COLCALC::Colony_Can_Build_Product(temp_colony, product)) {
                        AIBUILD::Force_Produce(colony_queue, (int16_t)i, (int32_t*)&colony_queue_count, product);
                        changed = true;
                        break;
                    }
                }
            }

            if (!changed) {
                ++i;
            }
        }

        bool want_colony_base = false;
        if (best_planet != -1) {
            if ((*AIDATA::ai_player_info)[player_idx].n_contacts == 0 ||
                (war_level == 0 && AIBUILD::player_total_strength_vs_ships[player_idx][0] > 0) ||
                AIBUILD::player_total_strength_vs_ships[player_idx][2] < 10 ||
                isqrt_val > 1) {
                if (builds_colony_bases_prewarp ||
                    (p->tech_applications[TECH_APP_COLONY_SHIP] == TECH_RESEARCH_STATUS_RESEARCHED &&
                        !no_range_or_speed)) {
                    int val = (*AIDATA::contextual_planet_values)[player_idx * MAX_PLANETS + best_planet];
                    if (war_level > 0) {
                        val /= 4;
                    }

                    int deficit = (*AIDATA::ai_player_info)[player_idx].colony_deficit;
                    int bonus = deficit * 10;
                    if (val / 2 < bonus) {
                        bonus = val / 2;
                    }
                    val += bonus;

                    int rand_val = game_random::Random(700);
                    int adjusted_rand = rand_val + 100 + (is_expansionist ? 0 : 100);

                    if (adjusted_rand < (isqrt_val + 64) * val) {
                        want_colony_base = true;
                    }
                }
            }
        }

        RUSS::Mox_Update();
        qsort(colony_queue, (uint32_t)colony_queue_count, sizeof(int16_t), aidudes::colony_less_industry_cmp);

        if (AIBUILD::strategic_production.expansion.enabled &&
            p->tech_applications[TECH_APP_COLONY_BASE] == TECH_RESEARCH_STATUS_RESEARCHED) {
            int i = colony_queue_count;
            while (want_colony_base && i > 0) {
                i--;
                s_colony* temp_colony = &MOX::colony[colony_queue[i]];
                if (!COLCALC::Colony_Is_Blockaded(temp_colony) &&
                    AIBUILD::Industry_Worth(temp_colony) >=
                        AIBUILD::strategic_production.expansion.colony_base_minimum_industry) {
                    if (MOX::planet[temp_colony->planet].star_index == MOX::planet[best_planet].star_index) {
                        if (temp_colony->buildings[BUILDING_COLONY_BASE] != 1) {
                            AIBUILD::Force_Produce(colony_queue, (int16_t)i, (int32_t*)&colony_queue_count, BUILDING_COLONY_BASE);
                        }
                        want_colony_base = false;
                    }
                }
            }
        }

        RUSS::Mox_Update();
        if (AIBUILD::strategic_production.expansion.enabled && want_colony_base &&
            p->tech_applications[TECH_APP_COLONY_SHIP] == TECH_RESEARCH_STATUS_RESEARCHED) {
            int r = 0;
            if (is_expansionist) {
                if (game_random::Random(5) == 1) {
                    r = 1;
                }
            }

            if (AIBUILD::player_n_colony_ships[player_idx][0] <= r) {
                int i = colony_queue_count;
                while (want_colony_base && i > 0) {
                    i--;
                    s_colony* temp_colony = &MOX::colony[colony_queue[i]];
                    if (!COLCALC::Colony_Is_Blockaded(temp_colony) &&
                        AIBUILD::Industry_Worth(temp_colony) >=
                            AIBUILD::strategic_production.expansion.colony_ship_minimum_industry) {
                        if ((*AIDATA::ai_player_info)[player_idx].colony_deficit + 50 < (int32_t)game_random::Random(100)) {
                            s_ship_design colony_ship_design = {};
                            INITSHIP::Load_Colony_Ship_Design(player_idx, &colony_ship_design);
                            int16_t ship_idx = COLBLDG::Create_Ship_From_Design(
                                player_idx, &colony_ship_design, colony_queue[i]);
                            if (ship_idx == -1) {
                                no_range_or_speed = true;
                            } else {
                                MOX::ship[ship_idx].status = 6;
                                AIBUILD::Force_Produce(colony_queue, (int16_t)i, (int32_t*)&colony_queue_count, COLBLDG::Colony_Production_From_Ship_Index(ship_idx));
                            }
                            want_colony_base = false;
                        }
                    }
                }
            }
        }

        while (colony_queue_count > 0) {
            int prev_count = colony_queue_count;
            int i = colony_queue_count - 1;
            s_colony* temp_colony = &MOX::colony[colony_queue[i]];

            RUSS::Mox_Update();

            if (AIBUILD::strategic_production.economic_emergency.enabled && colony_queue_count > 0 &&
                p->bc + p->surplus_bc < p->total_maintenance &&
                p->surplus_bc < 0 &&
                temp_colony->maintenance[1] < temp_colony->production[1]) {
                int16_t current_colony_idx = colony_queue[i];
                AIBUILD::Force_Produce(colony_queue, (int16_t)i, (int32_t*)&colony_queue_count, AIBUILD::strategic_production.economic_emergency.product);
                COLCALC::Colony_Calculation(current_colony_idx);
                continue;
            }

            int vs_ships_2 = AIBUILD::player_total_strength_vs_ships[player_idx][2];
            int16_t current_colony_idx = colony_queue[i];

            int ship_build_weight = 0;
            if (vs_ships_2 > 0 && p->ship_range != 0 && p->ship_speed != 0 && !no_range_or_speed) {
                int strength_0 = AIBUILD::player_total_strength_vs_ships[player_idx][0];
                ship_build_weight = (vs_ships_2 * 1000) / (strength_0 + vs_ships_2);
                if (war_level > 0) {
                    ship_build_weight *= 2;
                }
            }

            int freighter_weight = 0;
            int spy_weight = 0;
            int transport_weight = 0;
            int defer_weight = 0;

            uint8_t threatened = (*AIDATA::ai_colony_info)[current_colony_idx].threatened;
            if (threatened == 0) {
                if (AIBUILD::player_n_freighter_groups_wanted[player_idx] != 0 &&
                    freighter_groups_assigned <= 1 &&
                    p->tech_applications[TECH_APP_FREIGHTERS] == TECH_RESEARCH_STATUS_RESEARCHED) {
                    freighter_weight = (AIBUILD::player_n_freighter_groups_wanted[player_idx] - freighter_groups_assigned * 2) * 100 + 500;
                    if (war_level > 0) {
                        freighter_weight /= (war_level * war_level);
                    }
                }

                if (AIBUILD::player_n_spies[player_idx][1] != 0) {
                    spy_weight = (AIBUILD::player_n_spies[player_idx][1] * 4 + 40) * 25;
                    if (war_level > 0) {
                        spy_weight /= (war_level * war_level);
                    }
                }

                if (AIBUILD::player_n_transports[player_idx][1] != 0 &&
                    p->tech_applications[TECH_APP_TRANSPORT] == TECH_RESEARCH_STATUS_RESEARCHED &&
                    !no_range_or_speed) {
                    transport_weight = (AIBUILD::player_n_transports[player_idx][1] * 4 + 20) * 25;
                    if (war_level > 0) {
                        transport_weight = (transport_weight * 3) / 2;
                    }
                }
            }

            if (war_level == 0) {
                defer_weight = 333;
            } else {
                defer_weight = 350 - war_level * 100;
            }

            uint32_t choice_weights[6] = {0, 0, 0, 0, 0, 0};
            int32_t allocation_weights[6] = {
                ship_build_weight,
                freighter_weight,
                0,
                spy_weight,
                transport_weight,
                defer_weight,
            };
            production_rules::s_context production_context;
            AIBUILD::Build_Production_Context(temp_colony, war_level, &production_context);
            production_context.metrics[production_rules::METRIC_FREIGHTER_NEED] =
                AIBUILD::player_n_freighter_groups_wanted[player_idx] - freighter_groups_assigned * 2;
            production_context.metrics[production_rules::METRIC_REFIT_NEED] = p_refit->n_entries;
            production_rules::s_candidate* allocation_candidates =
                AIBUILD::strategic_production.allocation.candidates;
            for (int32_t weight_idx = 0; weight_idx < 6; ++weight_idx) {
                if (!allocation_candidates[weight_idx].enabled ||
                    !production_rules::Evaluate_Eligibility(
                        &production_context, &allocation_candidates[weight_idx].eligibility)) {
                    allocation_weights[weight_idx] = 0;
                } else {
                    allocation_weights[weight_idx] = production_rules::Evaluate_Weight(
                        &production_context, &allocation_candidates[weight_idx].weight, allocation_weights[weight_idx]);
                }
            }
            production_rules::Apply_Cutoff(&AIBUILD::strategic_production.allocation.cutoff, 0, allocation_candidates, allocation_weights, AIBUILD::STRATEGIC_ALLOCATION_CANDIDATE_COUNT);
            for (int32_t weight_idx = 0; weight_idx < AIBUILD::STRATEGIC_ALLOCATION_CANDIDATE_COUNT; ++weight_idx) {
                choice_weights[weight_idx] = (uint32_t)allocation_weights[weight_idx];
            }

            while (true) {
                RUSS::Mox_Update();
                int random_choice = RUSS::Get_Weighted_Choice_Int(choice_weights, 6);

                if (random_choice == -1 || random_choice == 5) {
                    colony_queue_count--;
                    break;
                }

                switch (random_choice) {
                    case 0: {
                        bool battlestation_candidate = false;
                        bool star_fortress_candidate = false;
                        bool star_base_candidate = false;

                        int ind_worth = AIBUILD::Industry_Worth(temp_colony);
                        if (p->surplus_bc > 5 && p->bc > 50) {
                            ind_worth += special::isqrt(special::isqrt(p->bc)) + special::isqrt(p->surplus_bc);
                        }

                        uint16_t ship_weights[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

                        if (p->ship_designs[SHIP_SIZE_SMALL].cost <= (war_level + 15) * ind_worth * 2) {
                            ship_weights[SHIP_SIZE_SMALL] = 2;
                            int cost_threshold_1 = (war_level * 2 + 33) * ind_worth;
                            if (p->ship_designs[SHIP_SIZE_MEDIUM].cost <= cost_threshold_1) {
                                ship_weights[SHIP_SIZE_MEDIUM] = 16;
                                ship_weights[SHIP_SIZE_SMALL] = 1;

                                if (temp_colony->buildings[BUILDING_STAR_FORTRESS] == 0) {
                                    if (p->tech_applications[TECH_APP_STAR_FORTRESS] == TECH_RESEARCH_STATUS_RESEARCHED &&
                                        COLCALC::Colony_Product_Cost(MOX::colony, BUILDING_STAR_FORTRESS) <= (war_level * 2 + 31) * ind_worth) {
                                        star_fortress_candidate = true;
                                        ship_weights[8] = (uint16_t)(((*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].neutral_count + 2 +
                                                                         ((*AIDATA::ai_colony_info)[i].threatened + (*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].alliance_count) * 2) *
                                                                  2 +
                                                              (*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].enemy_count) *
                                            2;
                                        if (p->command_points < p->command_points_used) {
                                            ship_weights[8] += (p->command_points_used - p->command_points) * 2;
                                        }
                                    }
                                }

                                // ORION95 0047ca47 skips all station candidates when a Fortress exists.
                                if (temp_colony->buildings[BUILDING_STAR_FORTRESS] == 0 &&
                                    temp_colony->buildings[BUILDING_BATTLESTATION] == 0) {
                                    if (p->tech_applications[TECH_APP_BATTLESTATION] == TECH_RESEARCH_STATUS_RESEARCHED &&
                                        COLCALC::Colony_Product_Cost(MOX::colony, BUILDING_BATTLESTATION) <= cost_threshold_1) {
                                        battlestation_candidate = true;
                                        ship_weights[7] = (uint16_t)(((*AIDATA::ai_colony_info)[i].threatened + (*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].alliance_count) * 2 +
                                                              (*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].neutral_count + 2) *
                                                5 +
                                            (*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].enemy_count * 3;
                                        if (p->command_points < p->command_points_used) {
                                            ship_weights[7] += (p->command_points_used - p->command_points) * 2;
                                        }
                                    }

                                    if (temp_colony->buildings[BUILDING_STAR_BASE] == 0) {
                                        if (p->tech_applications[TECH_APP_STAR_BASE] == TECH_RESEARCH_STATUS_RESEARCHED &&
                                            COLCALC::Colony_Product_Cost(MOX::colony, BUILDING_STAR_BASE) <= (war_level * 2 + 35) * ind_worth) {
                                            star_base_candidate = true;
                                            ship_weights[6] = (uint16_t)((*AIDATA::ai_colony_info)[i].threatened + (*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].alliance_count) * 10 +
                                                (*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].enemy_count * 4 + 12 +
                                                (*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].neutral_count * 6;
                                            if (p->command_points < p->command_points_used) {
                                                ship_weights[6] += (p->command_points_used - p->command_points) * 2;
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (COLCALC::Colony_Can_Build_Large_Ships(temp_colony)) {
                            int size_9 = AIBUILD::player_ship_sizes[player_idx].target_ship_size;
                            if (size_9 >= SHIP_SIZE_LARGE && p->ship_designs[SHIP_SIZE_LARGE].cost <= (war_level * 2 + 45) * ind_worth) {
                                ship_weights[SHIP_SIZE_LARGE] = 32;
                                ship_weights[SHIP_SIZE_MEDIUM] /= 5;
                                if (size_9 >= SHIP_SIZE_HUGE && p->ship_designs[SHIP_SIZE_HUGE].cost <= (war_level * 2 + 55) * ind_worth) {
                                    ship_weights[SHIP_SIZE_HUGE] = 64;
                                    ship_weights[SHIP_SIZE_LARGE] = 5;
                                    if (p->tech_applications[TECH_APP_TITAN_CONSTRUCTION] == TECH_RESEARCH_STATUS_RESEARCHED && size_9 >= SHIP_SIZE_TITAN && p->ship_designs[SHIP_SIZE_TITAN].cost <= (war_level * 2 + 48) * ind_worth) {
                                        ship_weights[SHIP_SIZE_TITAN] = 100;
                                    }
                                    if (p->tech_applications[TECH_APP_DOOM_STAR_CONSTRUCTION] == TECH_RESEARCH_STATUS_RESEARCHED && size_9 >= SHIP_SIZE_DOOM_STAR) {
                                        if (ship_design.cost == 0) {
                                            INITSHIP::Load_Doom_Star_Ship_Design(
                                                player_idx, &ship_design, MOX::settings.strategic_combat_flag != 0);
                                        }
                                        if (ship_design.cost <= (war_level * 2 + 48) * ind_worth) {
                                            ship_weights[SHIP_SIZE_DOOM_STAR] = 150;
                                        }
                                    }
                                }
                            }
                        } else {
                            if (star_base_candidate) {
                                ship_weights[6] += (*AIDATA::ai_colony_info)[current_colony_idx].threatened * 7 + 2;
                            }
                            if (battlestation_candidate) {
                                ship_weights[7] += (*AIDATA::ai_colony_info)[current_colony_idx].threatened * 6 + 2;
                            }
                            if (star_fortress_candidate) {
                                ship_weights[8] += (*AIDATA::ai_colony_info)[current_colony_idx].threatened * 5 + 2;
                            }
                        }

                        if (AIBUILD::player_total_strength_vs_ships[player_idx][0] > 0 && game_random::Random(5) == 1) {
                            ship_weights[SHIP_SIZE_SMALL] = 0;
                        }

                        int ship_choice = RUSS::Get_Weighted_Choice_Short(ship_weights, 9);
                        switch (ship_choice + 1) {
                            case 0:
                                break;
                            case 1:
                            case 2:
                            case 3:
                            case 4:
                            case 5: {
                                int16_t created_idx = COLBLDG::Create_Ship(player_idx, (int16_t)ship_choice, current_colony_idx);
                                if (created_idx == -1) {
                                    no_range_or_speed = true;
                                } else {
                                    MOX::ship[created_idx].status = 6;
                                    AIBUILD::Force_Produce(colony_queue, (int16_t)i, (int32_t*)&colony_queue_count, COLBLDG::Colony_Production_From_Ship_Index(created_idx));
                                    AIBUILD::player_total_strength_vs_ships[player_idx][2] -= AIBUILD::Mean_Ship_Strength(&MOX::ship[created_idx]);
                                }
                                break;
                            }
                            case 6: {
                                int16_t ds_idx = COLBLDG::Create_Ship_From_Design(
                                    player_idx, &ship_design, current_colony_idx);
                                if (ds_idx == -1) {
                                    no_range_or_speed = true;
                                } else {
                                    MOX::ship[ds_idx].status = 6;
                                    AIBUILD::Force_Produce(colony_queue, (int16_t)i, (int32_t*)&colony_queue_count, COLBLDG::Colony_Production_From_Ship_Index(ds_idx));
                                    AIBUILD::player_total_strength_vs_ships[player_idx][2] -= AIBUILD::Mean_Ship_Strength(&MOX::ship[ds_idx]);
                                }
                                break;
                            }
                            case 7:
                                AIBUILD::Force_Produce(colony_queue, (int16_t)i, (int32_t*)&colony_queue_count, BUILDING_STAR_BASE);
                                AIBUILD::player_total_strength_vs_ships[player_idx][2] -= ((*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].alliance_count * 2 + (*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].neutral_count + (*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].enemy_count);
                                break;
                            case 8:
                                AIBUILD::Force_Produce(colony_queue, (int16_t)i, (int32_t*)&colony_queue_count, BUILDING_BATTLESTATION);
                                AIBUILD::player_total_strength_vs_ships[player_idx][2] -= ((*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].alliance_count * 3 + (*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].neutral_count * 2 + (*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].enemy_count);
                                break;
                            case 9:
                                AIBUILD::Force_Produce(colony_queue, (int16_t)i, (int32_t*)&colony_queue_count, BUILDING_STAR_FORTRESS);
                                AIBUILD::player_total_strength_vs_ships[player_idx][2] -= ((*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].alliance_count * 6 + (*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].neutral_count * 4 + (*AIDATA::ai_star_info)[MOX::planet[temp_colony->planet].star_index].per_player[player_idx].enemy_count);
                                break;
                        }
                        choice_weights[0] = 0;
                        break;
                    }
                    case 1:
                        if (!COLCALC::Colony_Is_Blockaded(temp_colony) && AIBUILD::Industry_Worth(temp_colony) >= 12) {
                            AIBUILD::Force_Produce(colony_queue, (int16_t)i, (int32_t*)&colony_queue_count, COLONY_PRODUCTION_FREIGHTERS);
                            freighter_groups_assigned++;
                            AIBUILD::player_n_freighter_groups_wanted[player_idx]--;
                        }
                        choice_weights[1] = 0;
                        break;
                    case 2: {
                        int16_t refit_planet_idx = MOX::planet[temp_colony->planet].star_index;
                        int n_entries = p_refit->n_entries - 1;
                        bool refitted = false;
                        for (int j = n_entries; j >= 0; j--) {
                            int16_t ship_idx = p_refit->entry[j].ship_idx;
                            s_ship_data* s = &MOX::ship[ship_idx];
                            if (s->location == refit_planet_idx &&
                                (s->d.size < SHIP_SIZE_LARGE || COLCALC::Colony_Can_Build_Large_Ships(temp_colony))) {
                                s_ship_design* refit_design;
                                if (s->d.size == SHIP_SIZE_DOOM_STAR) {
                                    if (ship_design.cost == 0) {
                                        INITSHIP::Load_Doom_Star_Ship_Design(
                                            player_idx, &ship_design, MOX::settings.strategic_combat_flag != 0);
                                    }
                                    refit_design = &ship_design;
                                } else {
                                    refit_design = &p->ship_designs[s->d.size];
                                }

                                int16_t cost = AIBUILD::Refit_Cost(player_idx, &s->d, refit_design);
                                if (cost <= refit_design->cost &&
                                    cost <= AIBUILD::Industry_Worth(temp_colony) * (war_level + 10)) {
                                    if (s->officer_index != -1) {
                                        OFFICER::Move_Officer_To_Pool(s->officer_index);
                                        s->officer_index = -1;
                                    }

                                    memcpy(&s->d, refit_design, sizeof(s_ship_design));
                                    s->d.cost = cost;
                                    s->status = 4;
                                    AIBUILD::Force_Produce(colony_queue, (int16_t)i, (int32_t*)&colony_queue_count, COLBLDG::Colony_Production_From_Ship_Index(ship_idx));

                                    p_refit->n_entries--;
                                    memmove(&p_refit->entry[j], &p_refit->entry[j + 1], (p_refit->n_entries - j) * sizeof(s_ai_refit_entry));
                                    refitted = true;
                                    break;
                                }
                            }
                        }
                        if (!refitted) {
                            choice_weights[2] = 0;
                        }
                        break;
                    }
                    case 3:
                        if (!COLCALC::Colony_Is_Blockaded(temp_colony) && AIBUILD::Industry_Worth(temp_colony) >= 15) {
                            AIBUILD::Force_Produce(colony_queue, (int16_t)i, (int32_t*)&colony_queue_count, COLONY_PRODUCTION_SPY);
                            AIBUILD::player_n_spies[player_idx][1]--;
                        }
                        choice_weights[3] = 0;
                        break;
                    case 4:
                        if (!COLCALC::Colony_Is_Blockaded(temp_colony) && AIBUILD::Industry_Worth(temp_colony) >= 14) {
                            if (temp_colony->buildings[BUILDING_MARINE_BARRACKS] == 0) {
                                AIBUILD::Force_Produce(colony_queue, (int16_t)i, (int32_t*)&colony_queue_count, BUILDING_MARINE_BARRACKS);
                                AIBUILD::player_n_transports[player_idx][1]--;
                            } else {
                                s_ship_design transport_design = {};
                                INITSHIP::Load_Transport_Ship_Design(player_idx, &transport_design);
                                int16_t tr_idx = COLBLDG::Create_Ship_From_Design(
                                    player_idx, &transport_design, current_colony_idx);
                                if (tr_idx == -1) {
                                    no_range_or_speed = true;
                                } else {
                                    MOX::ship[tr_idx].status = 6;
                                    AIBUILD::Force_Produce(colony_queue, (int16_t)i, (int32_t*)&colony_queue_count, COLBLDG::Colony_Production_From_Ship_Index(tr_idx));
                                    AIBUILD::player_n_transports[player_idx][1]--;
                                }
                            }
                        }
                        choice_weights[4] = 0;
                        break;
                }

                if (colony_queue_count != prev_count) {
                    break;
                }
            }
        }
    }

    int __cdecl Sort_Pops_F_IR(const void* a, const void* b) {
        const uint32_t* pop1 = (const uint32_t*)a;
        const uint32_t* pop2 = (const uint32_t*)b;

        int prod1 = (int)COLCALC::Colony_Pop_Prod_Produced(Sort_Pops_colony_data, pop1, 0);
        int prod2 = (int)COLCALC::Colony_Pop_Prod_Produced(Sort_Pops_colony_data, pop2, 0);

        int score1;
        if (POP::Is_Worker(POP::Load(pop1))) {
            score1 = prod1 - (int)COLCALC::Colony_Pop_Prod_Produced(Sort_Pops_colony_data, pop1, 1);
            if (Sort_Pops_colony_info->pollution_at_start < Sort_Pops_colony_data->pollution) {
                score1 += 1000;
            }
        } else {
            score1 = prod1 - (int)COLCALC::Colony_Pop_Prod_Produced(Sort_Pops_colony_data, pop1, 2);
        }

        int score2;
        if (POP::Is_Worker(POP::Load(pop2))) {
            score2 = prod2 - (int)COLCALC::Colony_Pop_Prod_Produced(Sort_Pops_colony_data, pop2, 1);
            if (Sort_Pops_colony_info->pollution_at_start < Sort_Pops_colony_data->pollution) {
                score2 += 1000;
            }
        } else {
            score2 = prod2 - (int)COLCALC::Colony_Pop_Prod_Produced(Sort_Pops_colony_data, pop2, 2);
        }

        if (score2 != score1) {
            return score2 - score1;
        }

        if (prod1 != prod2) {
            return prod1 - prod2;
        }

        uint32_t val1 = POP::Load(pop1);
        uint32_t val2 = POP::Load(pop2);

        if (POP::Get_Prof(val1) == POP::Get_Prof(val2)) {
            return (int)POP::Get_Race(val1) - (int)POP::Get_Race(val2);
        }

        return (int)POP::Get_Prof(val1) - (int)POP::Get_Prof(val2);
    }

    void __cdecl Make_First_Unassigned_Into_Farmer(s_aidudes_colony_info* info) {
        s_colony* colony = &MOX::colony[info->colony_idx];
        int16_t old_pollution = colony->pollution;

        uint32_t* pop = &colony->pop[(uint8_t)info->cur_pop_idx];
        POP::Set_Prof(pop, POP::PROF_FARMER);
        POP::Set_Assigned(pop, true);

        info->cur_pop_idx++;
        info->n_farmers++;

        COLCALC::Pre_Import_Computing(colony);

        if (colony->pollution < old_pollution) {
            aidudes::Sort_Pops_colony_info = info;
            aidudes::Sort_Pops_colony_data = colony;

            size_t n_to_sort = (size_t)(info->pop_end_idx - info->cur_pop_idx);
            qsort(&colony->pop[(uint8_t)info->cur_pop_idx], n_to_sort, sizeof(uint32_t), aidudes::Sort_Pops_F_IR);
        }

        if (info->n_farmers < (uint8_t)colony->max_farms && info->cur_pop_idx < (uint8_t)colony->n_pops) {
            info->best_worker_to_farmer_score = aidudes::F_IR_Worthiness(info, &colony->pop[(uint8_t)info->cur_pop_idx]);
        } else {
            info->best_worker_to_farmer_score = (int16_t)0x8001;
        }
    }

    void __cdecl Assign_Additional_Unblockaded_Farmers() {
        s_aidudes_colony_info* candidates[MAX_COLONIES];
        int num_candidates = 0;
        bool freighter_shortage_flag = false;
        bool allocation_failed = false;

        if (aidudes::n_our_unblockaded > 0) {
            s_aidudes_colony_info* info = aidudes::our_unblockaded;
            s_aidudes_colony_info* end = aidudes::our_unblockaded + aidudes::n_our_unblockaded;

            while (info < end) {
                s_colony* col = &MOX::colony[info->colony_idx];
                info->cur_pop_idx = info->first_assignable_pop_idx;
                info->pop_end_idx = info->pop_tail_idx;

                if ((uint8_t)info->cur_pop_idx < (uint8_t)info->pop_tail_idx &&
                    (uint8_t)info->n_farmers < (uint8_t)col->max_farms &&
                    col->food2_per_farmer != 0) {
                    aidudes::Sort_Pops_colony_data = col;
                    aidudes::Sort_Pops_colony_info = info;

                    qsort(&col->pop[(uint8_t)info->cur_pop_idx],
                        (uint8_t)info->pop_end_idx - (uint8_t)info->cur_pop_idx,
                        4,
                        aidudes::Sort_Pops_F_IR);

                    info->best_worker_to_farmer_score = aidudes::F_IR_Worthiness(info, &col->pop[(uint8_t)info->cur_pop_idx]);
                    candidates[num_candidates++] = info;
                }
                info++;
            }

            COLCALC::Do_Player_Colony_Post_Production(aidudes::i_ai);

            while ((aidudes::p_ai->surplus_food < 0 || aidudes::p_ai->surplus_freighters < 0) &&
                (num_candidates > 0 && !allocation_failed)) {
                int best_candidate_idx;

                if (aidudes::p_ai->surplus_freighters < 1) {
                    best_candidate_idx = -1;
                    freighter_shortage_flag = true;

                    for (int i = 0; i < num_candidates; ++i) {
                        s_aidudes_colony_info* candidate = candidates[i];
                        s_colony* col = &MOX::colony[candidate->colony_idx];

                        int food_needed = (uint8_t)col->food2_needed_for_our_empire +
                            (uint8_t)col->food2_needed_for_assimilated +
                            (uint8_t)col->food2_needed_for_conquered +
                            (uint8_t)col->food2_needed_for_natives;

                        if (food_needed > (col->production[0] * 2)) {
                            if (best_candidate_idx == -1 || candidate->best_worker_to_farmer_score > candidates[best_candidate_idx]->best_worker_to_farmer_score) {
                                best_candidate_idx = i;
                            }
                        }
                    }
                } else {
                    best_candidate_idx = 0;
                    for (int i = num_candidates - 1; i > 0; --i) {
                        if (candidates[i]->best_worker_to_farmer_score > candidates[best_candidate_idx]->best_worker_to_farmer_score) {
                            best_candidate_idx = i;
                        }
                    }
                }

                if (best_candidate_idx == -1) {
                    allocation_failed = true;
                } else {
                    s_aidudes_colony_info* target_info = candidates[best_candidate_idx];
                    aidudes::Make_First_Unassigned_Into_Farmer(target_info);
                    COLCALC::Colony_Calculation(target_info->colony_idx);

                    s_colony* target_col = &MOX::colony[target_info->colony_idx];
                    if ((uint8_t)target_info->cur_pop_idx >= (uint8_t)target_info->pop_end_idx ||
                        (uint8_t)target_info->n_farmers >= (uint8_t)target_col->max_farms) {
                        num_candidates--;
                        if (best_candidate_idx < num_candidates) {
                            memmove(&candidates[best_candidate_idx], &candidates[best_candidate_idx + 1], (num_candidates - best_candidate_idx) * sizeof(s_aidudes_colony_info*));
                        }
                    }
                }
            }
        }

        if (freighter_shortage_flag) {
            if ((int)game_random::Random(10) <= (int)MOX::settings.game_difficulty) {
                aidudes::p_ai->n_freighters += 5;
            }
        }
    }

    int __cdecl F_IR_Worthiness(s_aidudes_colony_info* info, uint32_t* flags) {
        s_colony* colony = &MOX::colony[info->colony_idx];
        int16_t base_production = COLCALC::Colony_Pop_Prod_Produced(colony, flags, 0);
        int worthiness;

        if (POP::Is_Worker(POP::Load(flags))) {
            int16_t mode1_production = COLCALC::Colony_Pop_Prod_Produced(colony, flags, 1);
            worthiness = (int)base_production - (int)mode1_production;

            // If pollution has increased since the start of evaluation
            if (colony->pollution > info->pollution_at_start) {
                return worthiness + 1000;
            }
        } else {
            int16_t mode2_production = COLCALC::Colony_Pop_Prod_Produced(colony, flags, 2);
            worthiness = (int)base_production - (int)mode2_production;
        }

        return worthiness;
    }

    int __cdecl colony_less_industry_cmp(const void* p1, const void* p2) {
        int16_t worth1 = AIBUILD::Industry_Worth(&MOX::colony[*((const int16_t*)p1)]);
        int16_t worth2 = AIBUILD::Industry_Worth(&MOX::colony[*((const int16_t*)p2)]);

        return (int)worth1 - (int)worth2;
    }

    int __cdecl Sort_Pops_FR_I(const void* a, const void* b) {
        const uint32_t* pop1 = (const uint32_t*)a;
        const uint32_t* pop2 = (const uint32_t*)b;

        if (aidudes::Sort_Pops_colony_data->food2_per_farmer == 0) {
            return aidudes::_Sort_Pops_colony(a, b);
        }

        int16_t prod_0_p1 = COLCALC::Colony_Pop_Prod_Produced(aidudes::Sort_Pops_colony_data, pop1, 0);
        int16_t prod_1_p1 = COLCALC::Colony_Pop_Prod_Produced(aidudes::Sort_Pops_colony_data, pop1, 1);
        int16_t prod_2_p1 = COLCALC::Colony_Pop_Prod_Produced(aidudes::Sort_Pops_colony_data, pop1, 2);

        int16_t score_p1 = prod_2_p1 - (prod_1_p1 * 2) + prod_0_p1;

        int16_t prod_0_p2 = COLCALC::Colony_Pop_Prod_Produced(aidudes::Sort_Pops_colony_data, pop2, 0);
        int16_t prod_1_p2 = COLCALC::Colony_Pop_Prod_Produced(aidudes::Sort_Pops_colony_data, pop2, 1);
        int16_t prod_2_p2 = COLCALC::Colony_Pop_Prod_Produced(aidudes::Sort_Pops_colony_data, pop2, 2);

        int16_t score_p2 = prod_2_p2 - (prod_1_p2 * 2) + prod_0_p2;

        if (score_p2 != score_p1) {
            return (int32_t)score_p2 - (int32_t)score_p1;
        }

        if (prod_0_p2 != prod_0_p1) {
            return (int32_t)prod_0_p2 - (int32_t)prod_0_p1;
        }

        if (prod_1_p2 != prod_1_p1) {
            return (int32_t)prod_1_p1 - (int32_t)prod_1_p2;
        }

        return (int)POP::Get_Race(POP::Load(pop1)) - (int)POP::Get_Race(POP::Load(pop2));
    }

    int __cdecl Sort_Pops_R_I(const void* p1, const void* p2) {
        int16_t p1_prod1 = COLCALC::Colony_Pop_Prod_Produced(aidudes::Sort_Pops_colony_data, (const uint32_t*)p1, 1);
        int16_t p2_prod1 = COLCALC::Colony_Pop_Prod_Produced(aidudes::Sort_Pops_colony_data, (const uint32_t*)p2, 1);
        int16_t p1_prod2 = COLCALC::Colony_Pop_Prod_Produced(aidudes::Sort_Pops_colony_data, (const uint32_t*)p1, 2);
        int16_t p2_prod2 = COLCALC::Colony_Pop_Prod_Produced(aidudes::Sort_Pops_colony_data, (const uint32_t*)p2, 2);

        int16_t p2_delta = p2_prod2 - p2_prod1;
        int16_t p1_delta = p1_prod2 - p1_prod1;

        if (p2_delta != p1_delta) {
            return (int)p2_delta - (int)p1_delta;
        }

        if (p2_prod1 == p1_prod1) {
            return (int)POP::Get_Race(POP::Load(p1)) - (int)POP::Get_Race(POP::Load(p2));
        }

        return (int)p1_prod1 - (int)p2_prod1;
    }
} // namespace aidudes