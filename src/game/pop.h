#pragma once

#include <cstdint>
#include "unaligned.h"

namespace POP {
    using value_type = uint32_t;

    constexpr value_type MASK_RACE = 0x0000000Fu;
    constexpr value_type MASK_ORIGINAL_OWNER = 0x00000070u;
    constexpr value_type MASK_PROF = 0x00000180u;
    constexpr value_type MASK_ASSIGNED = 0x00000200u;
    constexpr value_type MASK_CONQUERED = 0x00000400u;

    constexpr uint32_t RACE_ANDROID = 8u;
    constexpr uint32_t RACE_NATIVE = 9u;

    enum e_prof : uint32_t {
        PROF_FARMER = 0,
        PROF_WORKER = 1,
        PROF_SCIENTIST = 2,
    };

    // Pop words usually live in packed structures (s_colony::pop), so access
    // them through pointers only via these bytewise helpers.
    inline value_type __cdecl Load(const void* pop) {
        return unaligned::Load<value_type>(pop);
    }

    inline void __cdecl Store(void* pop, value_type value) {
        unaligned::Store<value_type>(pop, value);
    }

    inline uint32_t __cdecl Get_Race(value_type pop) {
        return pop & MASK_RACE;
    }

    inline void __cdecl Set_Race(value_type* pop, uint32_t race) {
        Store(pop, (Load(pop) & ~MASK_RACE) | (race & MASK_RACE));
    }

    inline uint32_t __cdecl Get_Original_Owner(value_type pop) {
        return (pop & MASK_ORIGINAL_OWNER) >> 4;
    }

    inline void __cdecl Set_Original_Owner(value_type* pop, uint32_t owner) {
        Store(pop, (Load(pop) & ~MASK_ORIGINAL_OWNER) | ((owner & 0x07u) << 4));
    }

    inline e_prof __cdecl Get_Prof(value_type pop) {
        return static_cast<e_prof>((pop & MASK_PROF) >> 7);
    }

    inline void __cdecl Set_Prof(value_type* pop, e_prof prof) {
        Store(pop, (Load(pop) & ~MASK_PROF) | ((static_cast<value_type>(prof) & 0x03u) << 7));
    }

    inline bool __cdecl Is_Farmer(value_type pop) {
        return Get_Prof(pop) == PROF_FARMER;
    }

    inline bool __cdecl Is_Worker(value_type pop) {
        return Get_Prof(pop) == PROF_WORKER;
    }

    inline bool __cdecl Is_Scientist(value_type pop) {
        return Get_Prof(pop) == PROF_SCIENTIST;
    }

    inline bool __cdecl Is_Assigned(value_type pop) {
        return (pop & MASK_ASSIGNED) != 0;
    }

    inline void __cdecl Set_Assigned(value_type* pop, bool assigned) {
        if (assigned) {
            Store(pop, Load(pop) | MASK_ASSIGNED);
        } else {
            Store(pop, Load(pop) & ~MASK_ASSIGNED);
        }
    }

    inline bool __cdecl Is_Conquered(value_type pop) {
        return (pop & MASK_CONQUERED) != 0;
    }

    inline void __cdecl Set_Conquered(value_type* pop, bool conquered) {
        if (conquered) {
            Store(pop, Load(pop) | MASK_CONQUERED);
        } else {
            Store(pop, Load(pop) & ~MASK_CONQUERED);
        }
    }

    inline bool __cdecl Is_Android(value_type pop) {
        return Get_Race(pop) == RACE_ANDROID;
    }

    inline bool __cdecl Is_Native(value_type pop) {
        return Get_Race(pop) == RACE_NATIVE;
    }

    inline bool __cdecl Is_Android_Or_Native(value_type pop) {
        const uint32_t race = Get_Race(pop);
        return race == RACE_ANDROID || race == RACE_NATIVE;
    }

    inline uint32_t __cdecl Get_Effective_Player(value_type pop, uint32_t colony_owner) {
        return Is_Android_Or_Native(pop) ? colony_owner : Get_Race(pop);
    }
} // namespace POP