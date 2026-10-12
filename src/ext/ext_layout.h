#pragma once
// The Extension API's LAYOUT message: where every member of the records the
// wire carries as raw bytes sits, by name. See docs/extension-api.md.

#include <cstdint>
#include <vector>

namespace ext {

/// The LAYOUT message's version. Bumped only when the message's own format
/// changes; records growing, moving or gaining members do not bump it.
constexpr uint16_t LAYOUT_VERSION = 1;

/// The LAYOUT message's payload, built once from the compiled records:
///   uint16 version, uint16 record count, then per record:
///     str8 name, uint32 size, uint16 member count, then per member:
///       str8 name, uint32 offset, uint32 size, uint32 element size,
///       uint8 kind (0 signed, 1 unsigned, 2 text, 3 record, 4 other),
///       str8 record name (kind 3; empty otherwise)
///   then uint16 constant count, per constant: str8 name, int32 value
///   (MAX_PLAYERS, MAX_LEADERS, MAX_STARS, MAX_SHIPS, MAX_PLANETS, MAX_NEBULAS)
/// (str8: uint8 length, then that many bytes; little-endian throughout).
const std::vector<uint8_t>& LayoutTable();

}  // namespace ext
