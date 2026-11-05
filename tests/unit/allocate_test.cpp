#include "pch.h"

#include <string>
#include <array>
#include <vector>

#include "doctest/doctest.h"
#include "game/allocate.h"

namespace {
    struct fatal_exit {};
    std::string fatal_message;
} // namespace

namespace exitm {
    [[noreturn]] void __cdecl Exit_With_Message(const char* message) {
        fatal_message = message;
        throw fatal_exit{};
    }
} // namespace exitm

DOCTEST_TEST_CASE("no-header arena respects exact bounds and does not write payload headers") {
    for (uint32_t size = 0; size < 65; ++size) {
        const uint32_t padded = allocate::Nested_No_Header_Pool_Size(size);
        std::vector<uint8_t> storage(sizeof(allocate::s_alloc_header) + padded + 16, 0xA5);
        auto* header = (allocate::s_alloc_header*)storage.data();
        *header = {0, padded, 0};
        void* pool = header + 1;
        DOCTEST_REQUIRE(allocate::Allocate_Next_Block_No_Header(pool, size) == pool);
        DOCTEST_CHECK(header->current == padded);
        for (size_t i = sizeof(*header); i < storage.size(); ++i) {
            DOCTEST_CHECK(storage[i] == 0xA5);
        }
        DOCTEST_CHECK(allocate::Allocate_Next_Block_No_Header(pool, 1) == nullptr);
        DOCTEST_CHECK(header->current == padded);
    }
}

DOCTEST_TEST_CASE("arena rejects huge and wrapped requests without changing its cursor") {
    std::array<uint8_t, 128> storage{};
    auto* header = (allocate::s_alloc_header*)storage.data();
    *header = {4, 64, 0};
    void* pool = header + 1;
    for (uint32_t size : {UINT32_MAX, UINT32_MAX - 1, UINT32_MAX - 3, (uint32_t)INT32_MAX}) {
        DOCTEST_CHECK(allocate::Allocate_Next_Block_No_Header(pool, size) == nullptr);
        DOCTEST_CHECK(header->current == 4);
        DOCTEST_CHECK_THROWS_AS(allocate::Allocate_Next_Block(pool, size), fatal_exit);
        DOCTEST_CHECK(header->current == 4);
        DOCTEST_CHECK_THROWS_AS(allocate::Allocate_Space(size), fatal_exit);
    }
}

DOCTEST_TEST_CASE("header-backed arena exact fits include header and alignment") {
    for (uint32_t size = 0; size < 65; ++size) {
        const uint32_t required = sizeof(allocate::s_alloc_header) + allocate::Nested_No_Header_Pool_Size(size);
        std::vector<uint8_t> storage(sizeof(allocate::s_alloc_header) + required + 16, 0xA5);
        auto* header = (allocate::s_alloc_header*)storage.data();
        *header = {0, required, 0};
        void* result = allocate::Allocate_Next_Block(header + 1, size);
        DOCTEST_CHECK(((allocate::s_alloc_header*)result - 1)->size == size);
        DOCTEST_CHECK(header->current == required);
        DOCTEST_CHECK_THROWS_AS(allocate::Allocate_Next_Block(header + 1, 1), fatal_exit);
        DOCTEST_CHECK(header->current == required);
        for (size_t i = sizeof(*header) + required; i < storage.size(); ++i) {
            DOCTEST_CHECK(storage[i] == 0xA5);
        }
    }
}

DOCTEST_TEST_CASE("global screen space covers expanded non-AI screen structures") {
    DOCTEST_CHECK(allocate::Global_Screen_Space() == 0x123D668u);
}

DOCTEST_TEST_CASE("mark stack accepts forty pushes and rejects push forty-one") {
    allocate::s_alloc_header header = {};
    void* block = &header + 1;
    allocate::mark_stack_pos[0] = 0;
    fatal_message.clear();

    for (int32_t i = 0; i < MARK_STACK_COUNT; ++i) {
        header.current = static_cast<uint32_t>(i);
        allocate::Push_Block(block);

        DOCTEST_CHECK(allocate::mark_stack_pos[0] == i + 1);
        DOCTEST_CHECK(allocate::mark_stack[i].block == block);
        DOCTEST_CHECK(
            allocate::mark_stack[i].current == static_cast<uint32_t>(i));
    }

    DOCTEST_CHECK_THROWS_AS(allocate::Push_Block(block), fatal_exit);
    DOCTEST_CHECK(fatal_message == "Mark Block Stack Size Exceeded");
    DOCTEST_CHECK(allocate::mark_stack_pos[0] == MARK_STACK_COUNT);

    for (int32_t i = MARK_STACK_COUNT - 1; i >= 0; --i) {
        header.current = UINT32_MAX;
        allocate::Pop_Block(nullptr);

        DOCTEST_CHECK(header.current == static_cast<uint32_t>(i));
    }
}