#pragma once

#include <cstdint>

struct s_file_buffer_entry;

namespace buffer {
    extern int32_t global_load_count;

    extern int16_t file_buffer_count;

    extern s_file_buffer_entry* file_buffer;

    extern void* buffer_space_seg;

    extern int32_t buffer_space_size;

    extern void* buffer_space_ptr;

    /** Frees the allocated cache arena and file-buffer metadata. */
    void __cdecl Free_Buffer_Space();

    /** Invalidates all cached entries without reallocating the arena. Call only between screens. */
    void Reset_Buffer_Cache();

    /** Resets cached segment usage counters and clears the global load count.
     *  @param unused_context Unused legacy callback/context parameter.
     */
    int8_t __cdecl Flush_Buffer_Space(void* unused_context);

    /** Allocates the cache arena and initializes file and segment tracking state.
     *  @param requested_size Requested total cache size.
     */
    uint8_t* __cdecl Allocate_Buffer_Space(uint32_t requested_size);

    /** Loads an LBX entry into the cache arena, evicting older entries when needed.
     *  @param file_name LBX file path to read from.
     *  @param entry_index Zero-based LBX entry index.
     *  @param buffer_base Start of the cache payload area.
     */
    void* __cdecl Farload_Buffer_Library(const char* file_name, int32_t entry_index, uint8_t* buffer_base);

    /** Finds the loaded segment with the lowest usage priority.
     *  @param out_file_index Receives the owning file-buffer index.
     *  @param out_segment_index Receives the segment index within that file buffer.
     */
    void __cdecl Get_Lowest_Priority(int32_t* out_file_index, int32_t* out_segment_index);

    /** Finds the next loaded segment after the current one in cache memory.
     *  @param current_buffer_index File-buffer index of the current segment.
     *  @param current_segment_index Segment index within the current file buffer.
     *  @param next_buffer_index Receives the next file-buffer index or sentinel 1000.
     *  @param next_segment_index Receives the next segment index when found.
     *  @param buffer_data Start of the cache payload area.
     */
    void __cdecl Get_Next_Buffer_Address(int32_t current_buffer_index,
        int32_t current_segment_index,
        int32_t* next_buffer_index,
        int32_t* next_segment_index,
        uint8_t* buffer_data);

    /** Returns a cached segment or reloads it into the cache arena if needed.
     *  @param file_name LBX file path to search.
     *  @param buffer_seg_id Segment identifier within the LBX file.
     *  @param buffer_space_seg Expected cache payload base pointer.
     */
    void* __cdecl Buffer_Reload(const char* file_name, int32_t buffer_seg_id, void* buffer_space_seg);
} // namespace buffer