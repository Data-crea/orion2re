#include "pch.h"

#include "doctest/doctest.h"
#include "game/colgcbt.h"

namespace COLONY {
    uint8_t full_draw;
}

namespace {
    struct Ground_Combat_Fixture {
        s_invasion_side_info sides[2] = {};

        Ground_Combat_Fixture() {
            memset(COLGCBT::g_units, 0, sizeof(COLGCBT::g_units));
            COLGCBT::g_attack_ptr = &sides[0];
            COLGCBT::g_defend_ptr = &sides[1];
            COLONY::full_draw = 0;
            for (auto& side : sides) {
                side.current_troop_type_idx = 1;
                side.round_killed_unit_type = -1;
                side.round_hit_unit_type = -1;
            }
        }

        ~Ground_Combat_Fixture() {
            COLGCBT::g_attack_ptr = nullptr;
            COLGCBT::g_defend_ptr = nullptr;
        }

        void Add_Units(int side, int type, int count) {
            for (auto& unit : COLGCBT::g_units[side]) {
                if (count == 0) {
                    break;
                }
                if (unit.state == 0) {
                    unit.side = side;
                    unit.type = type;
                    unit.state = 4;
                    --count;
                }
            }
        }

        int Living_Units(int side, int type) {
            int count = 0;
            for (auto& unit : COLGCBT::g_units[side]) {
                if (unit.type == type && unit.state > 1) {
                    ++count;
                }
            }
            return count;
        }
    };
} // namespace

DOCTEST_TEST_CASE_FIXTURE(Ground_Combat_Fixture, "ground combat reconciles skipped casualties on either side") {
    for (int side = 0; side < 2; ++side) {
        Add_Units(side, 1, 6);
        Add_Units(side, 2, 2);
        sides[side].unit_counts[1] = 2;
        sides[side].unit_counts[2] = 2;
        sides[side].round_killed_unit_type = 1;
        sides[side].round_hit_unit_type = 1;
        COLGCBT::g_units[side][0].target = &COLGCBT::g_units[1 - side][0];
        COLGCBT::g_units[side][0].anim_frame = 3;
        COLGCBT::g_units[side][0].is_hit = 1;
    }

    COLGCBT::Verify_Legal_Totals();

    for (int side = 0; side < 2; ++side) {
        DOCTEST_CHECK(Living_Units(side, 1) == 2);
        DOCTEST_CHECK(Living_Units(side, 2) == 2);
        // Packed fields must not be bound to references by doctest decomposition.
        DOCTEST_CHECK((sides[side].unit_counts[1] == 2));
        DOCTEST_CHECK((sides[side].unit_counts[2] == 2));
        DOCTEST_CHECK((sides[side].round_killed_unit_type == -1));
        DOCTEST_CHECK((sides[side].round_hit_unit_type == -1));
        DOCTEST_CHECK((COLGCBT::g_units[side][0].state == 1));
        DOCTEST_CHECK((COLGCBT::g_units[side][0].anim_frame == 0));
        DOCTEST_CHECK((COLGCBT::g_units[side][0].is_hit == 0));
        DOCTEST_CHECK((COLGCBT::g_units[side][0].target == nullptr));
    }
    DOCTEST_CHECK(COLONY::full_draw == 1);

    COLGCBT::g_units[0][0].anim_frame = 3;
    COLONY::full_draw = 0;
    COLGCBT::Verify_Legal_Totals();
    DOCTEST_CHECK(Living_Units(0, 1) == 2);
    DOCTEST_CHECK((COLGCBT::g_units[0][0].anim_frame == 3));
    DOCTEST_CHECK(COLONY::full_draw == 0);
}

DOCTEST_TEST_CASE_FIXTURE(Ground_Combat_Fixture, "ground combat excludes dying sprites from survivor totals") {
    Add_Units(1, 1, 3);
    sides[1].unit_counts[1] = 2;
    auto& dying = COLGCBT::g_units[1][0];
    dying.state = 1;
    dying.anim_frame = 3;
    dying.is_hit = 1;

    COLGCBT::Verify_Legal_Totals();

    DOCTEST_CHECK(Living_Units(1, 1) == 2);
    DOCTEST_CHECK((dying.state == 1));
    DOCTEST_CHECK((dying.anim_frame == 3));
    DOCTEST_CHECK((dying.is_hit == 1));
    DOCTEST_CHECK(COLONY::full_draw == 0);
}

DOCTEST_TEST_CASE_FIXTURE(Ground_Combat_Fixture, "ground combat preserves pending deaths when reserves exceed sprites") {
    Add_Units(0, 1, 10);
    sides[0].unit_counts[1] = 20;
    sides[0].round_killed_unit_type = 1;

    COLGCBT::Verify_Legal_Totals();

    DOCTEST_CHECK(Living_Units(0, 1) == 10);
    DOCTEST_CHECK((sides[0].round_killed_unit_type == 1));
    DOCTEST_CHECK(COLONY::full_draw == 0);
}

DOCTEST_TEST_CASE_FIXTURE(Ground_Combat_Fixture, "ground combat retires all depleted troop types") {
    for (int type = 0; type < UNIT_COUNT; ++type) {
        Add_Units(0, type, 2);
    }
    sides[0].current_troop_type_idx = UNIT_COUNT;

    COLGCBT::Verify_Legal_Totals();

    for (int type = 0; type < UNIT_COUNT; ++type) {
        DOCTEST_CHECK(Living_Units(0, type) == 0);
    }
}

DOCTEST_TEST_CASE_FIXTURE(Ground_Combat_Fixture, "ground combat reconciles casualties without a pending kill event") {
    Add_Units(1, 1, 7);
    Add_Units(1, 2, 2);
    sides[1].unit_counts[1] = 2;
    sides[1].unit_counts[2] = 2;

    COLGCBT::Verify_Legal_Totals();

    DOCTEST_CHECK(Living_Units(1, 1) == 2);
    DOCTEST_CHECK(Living_Units(1, 2) == 2);
}