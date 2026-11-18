#pragma once
#include <cstdint>
#include <vector>

#define ESTRINGS_COUNT 812
#define ESTRINGS_DATA_SIZE 21000

namespace ESTRINGS {
    extern char* strings[ESTRINGS_COUNT];
    extern const char s_0_0055110c[4];
    extern const char s_1_00551110[4];

    extern std::vector<char> string_data;

    /** Populates cached label arrays from the loaded ESTRINGS table. */
    void __cdecl Load_H_And_Pro_File_Strings(void);

    /** Loads the localized string table into `string_data` and refreshes dependent label pointers. */
    void __cdecl Load_E_Strings();

    /** Returns the loaded string for a table entry.
     *  @param string_id ESTRINGS table index to look up.
     */
    char* __cdecl E_Strings(int16_t string_id);
} // namespace ESTRINGS