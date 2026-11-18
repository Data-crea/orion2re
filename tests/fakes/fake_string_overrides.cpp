#include "pch.h"
#include "game/string_overrides.h"

// Non-resource unit tests retain their existing LBX fixtures. Real sidecars are
// exercised by the Farload/VFS integration suite.
namespace string_overrides {
    void Reset_Cache() {}
    void Apply_String(const char*, size_t, char*, size_t) {}
    void Apply_Table(const char*, std::vector<char>*, size_t, size_t, bool) {}
} // namespace string_overrides