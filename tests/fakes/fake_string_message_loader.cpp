#include "pch.h"
#include "game/string_overrides.h"
#include "doctest/doctest.h"

namespace string_overrides {
    void Load_Message(const char* filename, int32_t entry, uint32_t record, uint16_t record_size, size_t, char* dest, size_t capacity) {
        DOCTEST_REQUIRE(capacity >= record_size);
        farload::Farload_Data_Static(filename, entry, dest, record, 1, record_size);
    }
} // namespace string_overrides