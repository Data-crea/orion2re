// OrionLayer, open fix 93. The Extension API's LAYOUT message, built from the game's own records.
//
// The member list is src/ext/ext_layout_table.inc, generated from
// src/game/orion2.h by tools/ext_layout_gen.py; every offset and size below
// is the compiler's. No game state is read here: the table describes types,
// so it can be built once and sent from the server thread.
#include "pch.h"
#include "ext/ext_layout.h"
#include <cstddef>
#include <type_traits>

namespace ext {
namespace {

enum : uint8_t { KIND_SIGNED = 0, KIND_UNSIGNED = 1, KIND_TEXT = 2,
                 KIND_RECORD = 3, KIND_OTHER = 4 };

template <typename T>
constexpr uint8_t Kind() {
    using E = std::remove_cv_t<std::remove_all_extents_t<T>>;
    if constexpr (std::is_same_v<E, char>) {
        return KIND_TEXT;
    } else if constexpr (std::is_enum_v<E>) {
        return std::is_signed_v<std::underlying_type_t<E>> ? KIND_SIGNED : KIND_UNSIGNED;
    } else if constexpr (std::is_integral_v<E>) {
        return std::is_signed_v<E> ? KIND_SIGNED : KIND_UNSIGNED;
    } else if constexpr (std::is_class_v<E>) {
        return KIND_RECORD;
    } else {
        return KIND_OTHER;
    }
}

struct Member {
    const char* name;
    uint32_t offset;
    uint32_t size;
    uint32_t element;
    uint8_t kind;
    const char* record;
};

struct Record {
    const char* name;
    uint32_t size;
    const Member* members;
    size_t count;
};

#define LAYOUT_RECORD_BEGIN(S) const Member k_##S[] = {
#define LAYOUT_FIELD(S, M, REC)                                              \
    {#M, (uint32_t)offsetof(S, M), (uint32_t)sizeof(((S*)nullptr)->M),       \
     (uint32_t)sizeof(std::remove_all_extents_t<decltype(S::M)>),           \
     Kind<decltype(S::M)>(), REC},
#define LAYOUT_RECORD_END(S) };
#include "ext/ext_layout_table.inc"
#undef LAYOUT_RECORD_BEGIN
#undef LAYOUT_FIELD
#undef LAYOUT_RECORD_END

#define LAYOUT_RECORD(S) {#S, (uint32_t)sizeof(S), k_##S, sizeof(k_##S) / sizeof(k_##S[0])},
const Record k_records[] = {LAYOUT_RECORDS};
#undef LAYOUT_RECORD

// The fixed counts the snapshot sends records by without a count of its own
// (players, leaders), and the limits a client sizes its tables by.
struct Constant {
    const char* name;
    int32_t value;
};
const Constant k_constants[] = {
    {"MAX_PLAYERS", MAX_PLAYERS}, {"MAX_LEADERS", MAX_LEADERS},
    {"MAX_STARS", MAX_STARS},     {"MAX_SHIPS", MAX_SHIPS},
    {"MAX_PLANETS", MAX_PLANETS}, {"MAX_NEBULAS", MAX_NEBULAS},
};

void Put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back((uint8_t)v);
    b.push_back((uint8_t)(v >> 8));
}

void Put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(v >> (8 * i)));
}

void PutStr8(std::vector<uint8_t>& b, const char* s) {
    size_t n = s ? std::char_traits<char>::length(s) : 0;
    if (n > 255) n = 255;
    b.push_back((uint8_t)n);
    b.insert(b.end(), s, s + n);
}

std::vector<uint8_t> Build() {
    std::vector<uint8_t> b;
    Put16(b, LAYOUT_VERSION);
    Put16(b, (uint16_t)(sizeof(k_records) / sizeof(k_records[0])));
    for (const Record& r : k_records) {
        PutStr8(b, r.name);
        Put32(b, r.size);
        Put16(b, (uint16_t)r.count);
        for (size_t i = 0; i < r.count; ++i) {
            const Member& m = r.members[i];
            PutStr8(b, m.name);
            Put32(b, m.offset);
            Put32(b, m.size);
            Put32(b, m.element);
            b.push_back(m.kind);
            PutStr8(b, m.kind == KIND_RECORD ? m.record : "");
        }
    }
    Put16(b, (uint16_t)(sizeof(k_constants) / sizeof(k_constants[0])));
    for (const Constant& c : k_constants) {
        PutStr8(b, c.name);
        Put32(b, (uint32_t)c.value);
    }
    return b;
}

}  // namespace

const std::vector<uint8_t>& LayoutTable() {
    static const std::vector<uint8_t> table = Build();
    return table;
}

}  // namespace ext
