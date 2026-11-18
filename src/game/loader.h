#pragma once

namespace LOADER {
    /** Measures persistent artwork and scratch storage from the currently mounted LBX entries. */
    uint32_t Persistent_Picture_Space();
    /** Reloads persistent pictures into an independent, exactly sized arena. Call only between screens. */
    void __cdecl Load_Main_Screen_Pictures();
    /** Releases the independently owned picture arena at shutdown. */
    void Free_Pictures();
    /** Loads shared UI picture resources and initializes the main screen backing animation buffer. */
    void __cdecl Load_Pictures();
    /** Loads the localized special-ability name strings and indexes them into the global pointer table. */
    void __cdecl Load_Player_Special_Names();
} // namespace LOADER