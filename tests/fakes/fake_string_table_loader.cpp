#include "pch.h"
#include "game/string_overrides.h"

namespace string_overrides {
    char* Reload_Raw_Table(const char* filename, int32_t entry, void** pool, size_t, size_t, bool) {
        return (char*)farload::Far_Reload(filename, entry, *pool);
    }
} // namespace string_overrides