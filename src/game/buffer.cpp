#include "pch.h"

#define ORION2RE_BUFFER_FILE_COUNT 50u
#define ORION2RE_BUFFER_SEGMENT_COUNT 410u
#define ORION2RE_BUFFER_METADATA_SIZE (sizeof(s_file_buffer_entry) * ORION2RE_BUFFER_FILE_COUNT)

namespace buffer {
    int32_t global_load_count;
    int16_t file_buffer_count;
    s_file_buffer_entry* file_buffer;
    void* buffer_space_seg;
    int32_t buffer_space_size;
    void* buffer_space_ptr;

    void Reset_Buffer_Cache() {
        if (buffer_space_ptr == nullptr || file_buffer == nullptr) {
            return;
        }
        auto* header = (allocate::s_alloc_header*)buffer_space_ptr;
        header->current = 0;
        header->reserved2 = 0;
        memset(file_buffer, 0, ORION2RE_BUFFER_METADATA_SIZE);
        file_buffer_count = 0;
        global_load_count = 0;
    }

    int8_t __cdecl Flush_Buffer_Space(void* unused_context) {
        (void)unused_context;

        for (int32_t file_index = 0; file_index < (int32_t)ORION2RE_BUFFER_FILE_COUNT; ++file_index) {
            for (int32_t segment_index = 0; segment_index < (int32_t)ORION2RE_BUFFER_SEGMENT_COUNT; ++segment_index) {
                file_buffer[file_index].segments[segment_index].last_use_counter = 0;
            }
        }

        global_load_count = 0;
        return (int8_t)1;
    }

    void __cdecl Free_Buffer_Space() {
        if (buffer_space_ptr != nullptr) {
            free(buffer_space_ptr);
            free(file_buffer);
            buffer_space_ptr = nullptr;
            buffer_space_seg = nullptr;
            file_buffer = nullptr;
            buffer_space_size = 0;
            file_buffer_count = 0;
            global_load_count = 0;
        }
    }

    uint8_t* __cdecl Allocate_Buffer_Space(uint32_t requested_size) {
        uint32_t allocation_error_size = requested_size;
        uint32_t space_size;

        if (requested_size > ORION2RE_BUFFER_METADATA_SIZE) {
            allocation_error_size = requested_size - ORION2RE_BUFFER_METADATA_SIZE;
            space_size = allocation_error_size;
        } else {
            exitm::Exit_With_Message("Not Enough Space For Buffer Space");
            space_size = requested_size;
        }

        uint32_t allocation_size = space_size;
        if ((allocation_size & 1U) != 0) {
            allocation_size = (allocation_size & 0xFFFFFFFCu) + 4U;
        }

        uint8_t* buffer_space = (uint8_t*)allocate::Allocate_Far_Space(allocation_size + 0xC);
        if (buffer_space == nullptr) {
            allocate::Allocation_Error((int32_t)allocation_error_size);
        }

        buffer_space_ptr = buffer_space;

        auto* header = (allocate::s_alloc_header*)buffer_space;
        header->current = 0;
        header->size = space_size;
        header->reserved2 = 0;

        uint8_t* buffer_space_segment = buffer_space + 0xC;
        buffer_space_size = space_size;
        buffer_space_seg = buffer_space_segment;

        file_buffer = (s_file_buffer_entry*)allocate::Allocate_Far_Space(ORION2RE_BUFFER_METADATA_SIZE);
        file_buffer_count = 0;

        for (int32_t file_index = 0; file_index < (int32_t)ORION2RE_BUFFER_FILE_COUNT; ++file_index) {
            s_file_buffer_entry& file_entry = file_buffer[file_index];
            file_entry.segment_count = 0;

            for (int32_t segment_index = 0; segment_index < (int32_t)ORION2RE_BUFFER_SEGMENT_COUNT; ++segment_index) {
                s_buffer_cached_segment& segment = file_entry.segments[segment_index];
                segment.is_loaded = 0;
                segment.buffer_seg_id = 0;
                segment.last_use_counter = 0;
            }
        }

        return buffer_space_segment;
    }

    void __cdecl Get_Lowest_Priority(int32_t* out_file_index, int32_t* out_segment_index) {
        uint32_t lowest_priority = 0xFFFFFFFFu;

        if (buffer::file_buffer_count > 0) {
            for (int32_t file_index = 0; file_index < buffer::file_buffer_count; ++file_index) {
                s_file_buffer_entry& file_entry = buffer::file_buffer[file_index];

                if (file_entry.segment_count > 0) {
                    for (int32_t segment_index = 0; segment_index < file_entry.segment_count; ++segment_index) {
                        s_buffer_cached_segment& segment = file_entry.segments[segment_index];

                        if ((uint32_t)segment.last_use_counter <= lowest_priority) {
                            if (segment.is_loaded == 1) {
                                *out_file_index = file_index;
                                *out_segment_index = segment_index;
                                lowest_priority = (uint32_t)segment.last_use_counter;
                            }
                        }
                    }
                }
            }
        }
    }

    void* __cdecl Buffer_Reload(const char* file_name, int32_t buffer_seg_id, void* buffer_space_seg) {
        auto* pool_header =
            (allocate::s_alloc_header*)((uint8_t*)buffer_space_seg - sizeof(allocate::s_alloc_header));

        if (buffer_space_seg != buffer::buffer_space_seg) {
            exitm::Exit_With_Message("Cache Seg Corrupted...");
        }

        if ((int32_t)pool_header->size != buffer::buffer_space_size) {
            exitm::Exit_With_Message("Cache Seg Corrupted...");
        }

        bool found_file = false;
        int32_t file_index = 0;

        if (buffer::file_buffer_count > 0) {
            for (int32_t i = 0; i < (int32_t)buffer::file_buffer_count; ++i) {
                if (strcasecmp(file_name, buffer::file_buffer[i].file_name) == 0) {
                    found_file = true;
                    file_index = i;
                    break;
                }
            }
        }

        if (found_file) {
            s_file_buffer_entry& file_entry = buffer::file_buffer[file_index];

            for (int32_t segment_index = 0; segment_index < file_entry.segment_count; ++segment_index) {
                s_buffer_cached_segment& segment = file_entry.segments[segment_index];

                if (segment.buffer_seg_id == buffer_seg_id) {
                    if (segment.is_loaded == 1) {
                        ++buffer::global_load_count;
                        segment.last_use_counter = buffer::global_load_count;
                        return segment.data_ptr;
                    }

                    ++buffer::global_load_count;
                    segment.last_use_counter = buffer::global_load_count;
                    segment.data_ptr = Farload_Buffer_Library(file_name, buffer_seg_id, (uint8_t*)buffer_space_seg);
                    segment.is_loaded = 1;
                    return segment.data_ptr;
                }
            }

            if (file_entry.segment_count + 1 >= (int32_t)ORION2RE_BUFFER_SEGMENT_COUNT) {
                exitm::Exit_With_Message("max buffer segs went over..");
            }

            ++buffer::global_load_count;

            s_buffer_cached_segment& segment = file_entry.segments[file_entry.segment_count];
            segment.buffer_seg_id = (int16_t)buffer_seg_id;
            segment.last_use_counter = buffer::global_load_count;
            segment.data_ptr = Farload_Buffer_Library(file_name, buffer_seg_id, (uint8_t*)buffer_space_seg);
            segment.is_loaded = 1;

            ++file_entry.segment_count;
            return file_entry.segments[file_entry.segment_count - 1].data_ptr;
        }

        if ((int32_t)buffer::file_buffer_count + 1 >= (int32_t)ORION2RE_BUFFER_FILE_COUNT) {
            exitm::Exit_With_Message("max file buffer count went over..");
        }

        ++buffer::global_load_count;

        s_file_buffer_entry& file_entry = buffer::file_buffer[buffer::file_buffer_count];
        memcpy(file_entry.file_name, file_name, strlen(file_name) + 1);

        file_entry.segment_count = 0;

        s_buffer_cached_segment& segment = file_entry.segments[file_entry.segment_count];
        segment.buffer_seg_id = (int16_t)buffer_seg_id;
        segment.last_use_counter = buffer::global_load_count;
        segment.data_ptr = Farload_Buffer_Library(file_name, buffer_seg_id, (uint8_t*)buffer_space_seg);
        segment.is_loaded = 1;

        ++file_entry.segment_count;
        ++buffer::file_buffer_count;

        return file_entry.segments[file_entry.segment_count - 1].data_ptr;
    }

    void* __cdecl Farload_Buffer_Library(const char* file_name, int32_t entry_index, uint8_t* buffer_base) {
        int32_t entry_size;
        int32_t victim_file_buffer_index;
        int32_t victim_segment_index;
        int32_t next_file_buffer_index;
        int32_t next_segment_index;
        int32_t freed_size;
        void* loaded_data;
        s_vfs_file* file;
        int32_t current_offset;

        if (entry_index < 0) {
            farload::Error_Handler(file_name, 4, entry_index);
        }

        file = vfs::fopen_lbx(file_name, entry_index);
        if (file == nullptr) {
            farload::Error_Handler(file_name, 1, entry_index);
        }

        entry_size = vfs::fsize(file);
        if (entry_size < 0) {
            vfs::fclose(file);
            farload::Error_Handler(file_name, 7, entry_index);
        }

        auto* pool_header =
            (allocate::s_alloc_header*)(buffer_base - sizeof(allocate::s_alloc_header));
        loaded_data = allocate::Allocate_Next_Block_No_Header(buffer_base, entry_size);

        while (loaded_data == nullptr && (int32_t)pool_header->current > 0) {
            loaded_data = allocate::Allocate_Next_Block_No_Header(buffer_base, entry_size);

            if (loaded_data == nullptr) {
                current_offset = pool_header->current;
                buffer::Get_Lowest_Priority(&victim_file_buffer_index, &victim_segment_index);
                buffer::Get_Next_Buffer_Address(
                    victim_file_buffer_index,
                    victim_segment_index,
                    &next_file_buffer_index,
                    &next_segment_index,
                    buffer_base);

                uint8_t* used_end = buffer_base + pool_header->current;
                uint8_t* next_data_ptr = used_end;

                if (next_file_buffer_index != 1000) {
                    next_data_ptr = (uint8_t*)(buffer::file_buffer[next_file_buffer_index]
                                                   .segments[next_segment_index]
                                                   .data_ptr);
                }

                uint8_t* victim_data_ptr = (uint8_t*)(buffer::file_buffer[victim_file_buffer_index]
                                                          .segments[victim_segment_index]
                                                          .data_ptr);

                freed_size = static_cast<int32_t>(next_data_ptr - victim_data_ptr);

                if (next_file_buffer_index != 1000) {
                    const int32_t bytes_to_move =
                        (int32_t)(used_end - next_data_ptr);

                    struct_::Copy_Structure(victim_data_ptr, next_data_ptr, bytes_to_move);

                    if (buffer::file_buffer_count > 0) {
                        for (int32_t file_buffer_index = 0;
                             file_buffer_index < (int16_t)buffer::file_buffer_count;
                             ++file_buffer_index) {
                            if (buffer::file_buffer[file_buffer_index].segment_count > 0) {
                                for (int32_t segment_index = 0;
                                     segment_index < buffer::file_buffer[file_buffer_index].segment_count;
                                     ++segment_index) {
                                    s_buffer_cached_segment& segment =
                                        buffer::file_buffer[file_buffer_index].segments[segment_index];

                                    if (segment.is_loaded == 1) {
                                        uint8_t* segment_data_ptr = (uint8_t*)segment.data_ptr;

                                        if (victim_data_ptr < segment_data_ptr) {
                                            segment.data_ptr = segment_data_ptr - freed_size;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                buffer::file_buffer[victim_file_buffer_index]
                    .segments[victim_segment_index]
                    .is_loaded = 0;

                pool_header->current = (uint32_t)(current_offset - freed_size);
            }
        }

        if (loaded_data == nullptr) {
            vfs::fclose(file);
            farload::Error_Handler(file_name, 5, entry_index);
        }

        if (vfs::fread(loaded_data, static_cast<size_t>(entry_size), 1, file) != 1) {
            vfs::fclose(file);
            farload::Error_Handler(file_name, 7, entry_index);
        }

        vfs::fclose(file);
        return loaded_data;
    }

    void __cdecl Get_Next_Buffer_Address(int32_t current_buffer_index,
        int32_t current_segment_index,
        int32_t* next_buffer_index,
        int32_t* next_segment_index,
        uint8_t* buffer_data) {
        auto* pool_header =
            (allocate::s_alloc_header*)(buffer_data - sizeof(allocate::s_alloc_header));
        uint8_t* closest_address =
            buffer_data + pool_header->current;
        const std::uintptr_t current_segment_address =
            (std::uintptr_t)(
                buffer::file_buffer[current_buffer_index]
                    .segments[current_segment_index]
                    .data_ptr);

        *next_buffer_index = 1000;

        if (buffer::file_buffer_count > 0) {
            for (int32_t file_buffer_index = 0;
                 file_buffer_index < buffer::file_buffer_count;
                 ++file_buffer_index) {
                s_file_buffer_entry& file_buffer_entry =
                    buffer::file_buffer[file_buffer_index];

                if (file_buffer_entry.segment_count > 0) {
                    for (int32_t segment_index = 0;
                         segment_index < file_buffer_entry.segment_count;
                         ++segment_index) {
                        if (file_buffer_entry.segments[segment_index].is_loaded == 1) {
                            uint8_t* segment_data = (uint8_t*)(file_buffer_entry.segments[segment_index].data_ptr);

                            if ((current_segment_address <
                                    (std::uintptr_t)segment_data) &&
                                ((std::uintptr_t)segment_data <
                                    (std::uintptr_t)closest_address)) {
                                *next_buffer_index = file_buffer_index;
                                *next_segment_index = segment_index;
                                closest_address = segment_data;
                            }
                        }
                    }
                }
            }
        }
    }
} // namespace buffer
