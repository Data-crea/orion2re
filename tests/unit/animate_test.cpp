#include "pch.h"

#include "doctest/doctest.h"
#include "game/animate.h"

DOCTEST_TEST_SUITE("animation frame selection") {

DOCTEST_TEST_CASE("frame tables support unaligned animations including the terminal offset") {
    alignas(uint32_t) uint8_t bytes[80];
    for (size_t offset = 0; offset < alignof(uint32_t); ++offset) {
        memset(bytes, 0xCC, sizeof(bytes));
        auto* animation = new (bytes + offset) s_animation_header{};
        animation->frame_count = 2;
        Set_Animation_Frame_Offset_(animation, 0, 40);
        Set_Animation_Frame_Offset_(animation, 1, 48);
        Set_Animation_Frame_Offset_(animation, 2, 56);
        const s_animation_header* const_animation = animation;
        DOCTEST_CHECK(Animation_Frame_Offset_(const_animation, 0) == 40);
        DOCTEST_CHECK(Animation_Frame_Offset_(const_animation, 1) == 48);
        DOCTEST_CHECK(Animation_Frame_Offset_(const_animation, 2) == 56);
        DOCTEST_CHECK(Animation_Frame_Data_(animation, 0) == bytes + offset + 40);
        DOCTEST_CHECK(Animation_Frame_Data_(const_animation, 1) == bytes + offset + 48);
        const uint32_t expected[] = {40, 48, 56};
        DOCTEST_CHECK(memcmp(Animation_Frame_Table_(animation), expected, sizeof(expected)) == 0);
        const auto* palette = Animation_Palette_Header_(const_animation);
        DOCTEST_CHECK(reinterpret_cast<const uint8_t*>(palette) ==
            bytes + offset + sizeof(s_animation_header) + sizeof(expected));
        DOCTEST_CHECK(bytes[offset + sizeof(s_animation_header) + sizeof(expected)] == 0xCC);
    }
}

DOCTEST_TEST_CASE("frame count uses its unsigned 16-bit value") {
    s_animation_header animation = {};
    animation.frame_count = -32767;

    animate::Set_Animation_Frame(&animation, 1);

    DOCTEST_CHECK(animation.current_frame == 1);
}

DOCTEST_TEST_CASE("high-bit requested frame clamps to the last frame") {
    s_animation_header animation = {};
    animation.frame_count = 3;

    animate::Set_Animation_Frame(&animation, -1);

    DOCTEST_CHECK(animation.current_frame == 2);
}

}