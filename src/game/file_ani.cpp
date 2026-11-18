#include "pch.h"

namespace file_ani {

    uint8_t* file_animation_buffer;
    uint32_t file_animation_open;
    uint32_t file_animation_length;
    uint32_t file_animation_end;
    s_vfs_file* file_animation_fptr;
    uint32_t file_animation_opened_flag[FILE_ANI_MULTI_SLOT_COUNT];
    s_vfs_file* file_animation[FILE_ANI_MULTI_SLOT_COUNT];
    uint32_t file_animation_start_multi[FILE_ANI_MULTI_SLOT_COUNT];
    uint32_t file_animation_end_multi[FILE_ANI_MULTI_SLOT_COUNT];
    uint32_t* file_lbx_header;
    uint32_t file_animation_length_multi[FILE_ANI_MULTI_SLOT_COUNT];
    uint32_t file_animation_start;
    uint8_t* file_animation_header;

    int16_t __cdecl Get_File_Animation_Frame() {
        file_animation_header = (uint8_t*)init::io_buffer;
        return *(int16_t*)(file_animation_header + 4);
    }

    void __cdecl Set_File_Animation_Buffer(void* buffer) {
        file_animation_buffer = (uint8_t*)buffer;
    }

    void __cdecl Open_File_Animation(const char* filename, int16_t entry_index) {
        s_vfs_file* file;
        uint32_t bytes_to_read;

        if (entry_index < 0) {
            farload::Error_Handler(filename, 4, entry_index);
        }

        if (file_animation_open == 1) {
            vfs::fclose(file_animation_fptr);
        }

        file = vfs::fopen_lbx(filename, entry_index);
        file_animation_open = 1;

        if (file == nullptr) {
            farload::Error_Handler(filename, 1, entry_index);
        }

        file_animation_fptr = file;
        file_lbx_header = (uint32_t*)init::io_buffer;
        file_animation_start = 0;
        file_animation_length = (uint32_t)vfs::fsize(file_animation_fptr);
        file_animation_end = file_animation_length;
        bytes_to_read = file_animation_length;
        if (bytes_to_read > FILE_ANI_PRELOAD_SIZE) {
            bytes_to_read = FILE_ANI_PRELOAD_SIZE;
        }

        memset(file_lbx_header, 0, FILE_ANI_PRELOAD_SIZE);

        if (bytes_to_read == 0 || vfs::fread(file_lbx_header, bytes_to_read, 1, file_animation_fptr) != 1) {
            farload::Error_Handler(filename, 7, entry_index);
        }
    }

    void __cdecl thunk_Draw_File_Animation() {
        Draw_File_Animation();
    }

    void __cdecl Draw_File_Animation() {
        animate::animation_header = (s_animation_header*)init::io_buffer;

        if ((animate::animation_header->flags & ANIMATION_FLAG_HAS_PALETTE) != 0) {
            animate::Set_Animation_Palette();
        }

        int16_t current_frame = animate::animation_header->current_frame;
        uint32_t frame_offset = Animation_Frame_Offset_(animate::animation_header, current_frame);
        uint32_t next_frame_offset = Animation_Frame_Offset_(animate::animation_header, current_frame + 1);

        uint32_t frame_size = next_frame_offset - frame_offset;

        int32_t max_block_space = allocate::Get_Max_Block_Space(file_animation_buffer);
        if (max_block_space < (int32_t)frame_size) {
            exitm::Exit_With_Message("File Animation Frame Too Large");
        }

        vfs::fseek(file_animation_fptr, (int32_t)frame_offset, SEEK_SET);
        vfs::fread(file_animation_buffer, frame_size, 1, file_animation_fptr);

        if (video::screen_refresh_flag >= 2) {
            setvesa::Add_Square_To_Scan_List(
                0,
                0,
                animate::animation_header->width - 1,
                animate::animation_header->height - 1);
        }

        uint8_t flags = animate::animation_header->flags;
        if ((flags & ANIMATION_FLAG_GLASSED) != 0) {
            switch (flags & ANIMATION_FLAG_DRAW_MODE_MASK) {
                case ANIMATION_DRAW_MODE_ANIMATED_SPRITE:
                    animate::Draw_Glassed_Animated_Sprite(0, 0, file_animation_buffer);
                    break;
                case ANIMATION_DRAW_MODE_BITMAP:
                    animate::Draw_Glassed_Bitmap_Sprite(0, 0, file_animation_buffer);
                    break;
                case ANIMATION_DRAW_MODE_SCREEN_BACKGROUND:
                    animate::Draw_Glassed_Screen_Background_Off(file_animation_buffer);
                    if (video::screen_refresh_flag >= 2) {
                        setvesa::Add_Square_To_Scan_List(0, 0, 0x27F, 0x1DF);
                    }
                    break;
                case ANIMATION_DRAW_MODE_REFRESH_MASK:
                default:
                    exitm::Exit_With_Message("Refresh Mask can't be drawn");
                    break;
            }
        } else {
            switch (flags & ANIMATION_FLAG_DRAW_MODE_MASK) {
                case ANIMATION_DRAW_MODE_ANIMATED_SPRITE:
                    video::g_screen_res_x = platform::Lock_Appropriate_Render_Buffer();
                    draw::Draw_Animated_Sprite(0, 0, file_animation_buffer);
                    platform::Unlock_Appropriate_Render_Buffer();
                    break;
                case ANIMATION_DRAW_MODE_BITMAP:
                    video::g_screen_res_x = platform::Lock_Appropriate_Render_Buffer();
                    draw::Draw_Bitmap_Sprite(0, 0, file_animation_buffer);
                    platform::Unlock_Appropriate_Render_Buffer();
                    break;
                case ANIMATION_DRAW_MODE_SCREEN_BACKGROUND:
                    animate::Draw_Screen_Background_Off(file_animation_buffer);
                    if (video::screen_refresh_flag >= 2) {
                        setvesa::Add_Square_To_Scan_List(0, 0, 0x27F, 0x1DF);
                    }
                    break;
                case ANIMATION_DRAW_MODE_REFRESH_MASK:
                default:
                    exitm::Exit_With_Message("Refresh Mask can't be drawn");
                    break;
            }
        }

        animate::animation_header->current_frame++;

        if ((animate::animation_header->flags & 0x20) == 0) {
            if (animate::animation_header->current_frame >= animate::animation_header->frame_count) {
                animate::animation_header->current_frame = animate::animation_header->loop_frame;
            }
        } else {
            if (animate::animation_header->current_frame >= animate::animation_header->frame_count) {
                animate::animation_header->current_frame = animate::animation_header->loop_frame;
            }
        }
    }

    void __cdecl Draw_File_Animation_Stencil() {
        uint32_t saved_refresh_flag = video::screen_refresh_flag;
        animate::animation_header = init::io_buffer;
        video::screen_refresh_flag = 2;

        if ((int32_t)animate::animation_header->current_frame - (int32_t)animate::animation_header->frame_count == -1) {
            video::screen_refresh_flag = saved_refresh_flag;
        }

        if (animate::animation_header->flags & ANIMATION_FLAG_HAS_PALETTE) {
            animate::Set_Animation_Palette();
        }

        int16_t current_frame = animate::animation_header->current_frame;
        uint32_t frame_offset = Animation_Frame_Offset_(animate::animation_header, current_frame);
        uint32_t next_frame_offset = Animation_Frame_Offset_(animate::animation_header, current_frame + 1);
        uint32_t frame_size = next_frame_offset - frame_offset;

        int32_t available_space = allocate::Get_Max_Block_Space(file_animation_buffer);
        if (available_space < (int32_t)frame_size) {
            exitm::Exit_With_Message("File Animation Frame Too Large");
        }

        vfs::fseek(file_animation_fptr, (int32_t)frame_offset, SEEK_SET);
        vfs::fread(file_animation_buffer, frame_size, 1, file_animation_fptr);

        if (animate::animation_header->flags & ANIMATION_FLAG_GLASSED) {
            exitm::Exit_With_Message("Differentail File Animation Can not be glassed");
        }

        switch (animate::animation_header->flags & ANIMATION_FLAG_DRAW_MODE_MASK) {
            case ANIMATION_DRAW_MODE_ANIMATED_SPRITE:
                animate::Draw_Animated_Sprite_Back_Diff(0, 0, file_animation_buffer);
                break;
            case ANIMATION_DRAW_MODE_BITMAP:
                exitm::Exit_With_Message("Differentail File Animation Can not be bitmap");
                break;
            case ANIMATION_DRAW_MODE_SCREEN_BACKGROUND:
                exitm::Exit_With_Message("Differentail File Animation Can not be masked");
                break;
            case ANIMATION_DRAW_MODE_REFRESH_MASK:
                exitm::Exit_With_Message("Refresh Mask can't be drawn");
                break;
        }

        animate::animation_header->current_frame++;

        if (animate::animation_header->flags & 0x20) {
            if (animate::animation_header->current_frame >= animate::animation_header->frame_count) {
                int16_t loop_frame = animate::animation_header->loop_frame;
                if (loop_frame - animate::animation_header->frame_count != -1) {
                    animate::animation_header->current_frame = loop_frame;
                } else {
                    animate::animation_header->current_frame = 0;
                }
                video::screen_refresh_flag = saved_refresh_flag;
                return;
            }
        } else {
            if (animate::animation_header->current_frame >= animate::animation_header->frame_count) {
                animate::animation_header->current_frame = animate::animation_header->frame_count - 1;
            }
        }

        video::screen_refresh_flag = saved_refresh_flag;
    }

    int16_t __cdecl Get_File_Animation_Total_Frames() {
        if (!file_animation_open) {
            const char* message = HAROLD::H_Message(206);
            exitm::Exit_With_Message(message);
        }

        return init::io_buffer->frame_count - 1;
    }

    void __cdecl Set_File_Animation_Frame(int16_t frame_index) {
        file_animation_header = (uint8_t*)init::io_buffer;

        int16_t max_frames = init::io_buffer->frame_count;
        if (frame_index >= max_frames) {
            frame_index = max_frames - 1;
        }

        init::io_buffer->current_frame = frame_index;
    }

    void __cdecl Open_Multi_File_Animation(const char* filename, int16_t entry_index, int16_t animation_slot) {
        s_vfs_file* file;
        uint32_t bytes_to_read;

        if (animation_slot >= FILE_ANI_MULTI_SLOT_COUNT) {
            exitm::Exit_With_Message("Max 3 file animations");
        }

        int32_t slot = animation_slot;
        file_lbx_header = (uint32_t*)(((uint8_t*)init::io_buffer) + slot * FILE_ANI_MULTI_SLOT_SIZE);

        if (entry_index < 0) {
            farload::Error_Handler(filename, 4, entry_index);
        }

        if (file_animation_opened_flag[slot] == 1) {
            vfs::fclose(file_animation[slot]);
        }

        file = vfs::fopen_lbx(filename, entry_index);
        if (file == nullptr) {
            farload::Error_Handler(filename, 1, entry_index);
        }

        file_animation[slot] = file;

        file_animation_opened_flag[slot] = 1;

        file_animation_start_multi[slot] = 0;
        file_animation_length_multi[slot] = (uint32_t)vfs::fsize(file_animation[slot]);
        file_animation_end_multi[slot] = file_animation_length_multi[slot];
        bytes_to_read = file_animation_length_multi[slot];
        if (bytes_to_read > FILE_ANI_MULTI_SLOT_SIZE) {
            bytes_to_read = FILE_ANI_MULTI_SLOT_SIZE;
        }

        memset(file_lbx_header, 0, FILE_ANI_MULTI_SLOT_SIZE);

        if (bytes_to_read == 0 || vfs::fread(file_lbx_header, bytes_to_read, 1, file_animation[slot]) != 1) {
            farload::Error_Handler(filename, 7, entry_index);
        }
    }

    void __cdecl Draw_Multi_File_Animation_Stencil(int16_t anim_idx) {
        animate::animation_header = (s_animation_header*)(((uint8_t*)init::io_buffer) + anim_idx * FILE_ANI_MULTI_SLOT_SIZE);

        if (animate::animation_header->frame_count >= 678) {
            exitm::Exit_With_Message("Too many frames for multi file animation");
        }

        if (animate::animation_header->flags & ANIMATION_FLAG_HAS_PALETTE) {
            animate::Set_Animation_Palette();
        }

        int16_t current_frame = animate::animation_header->current_frame;
        uint32_t frame_offset = Animation_Frame_Offset_(animate::animation_header, current_frame);
        uint32_t next_frame_offset = Animation_Frame_Offset_(animate::animation_header, current_frame + 1);

        uint32_t frame_size = next_frame_offset - frame_offset;

        if ((int32_t)allocate::Get_Max_Block_Space(file_animation_buffer) < (int32_t)frame_size) {
            exitm::Exit_With_Message("File Animation Frame Too Large");
        }

        vfs::fseek(file_animation[anim_idx], (int32_t)frame_offset, SEEK_SET);
        vfs::fread(file_animation_buffer, frame_size, 1, file_animation[anim_idx]);

        if (video::screen_refresh_flag >= 2) {
            setvesa::Add_Square_To_Scan_List(
                0,
                0,
                animate::animation_header->width - 1,
                animate::animation_header->height - 1);
        }

        switch (animate::animation_header->flags & ANIMATION_FLAG_DRAW_MODE_MASK) {
            case ANIMATION_DRAW_MODE_ANIMATED_SPRITE:
                animate::Draw_Animated_Sprite_Back_Diff(0, 0, file_animation_buffer);
                break;
            case ANIMATION_DRAW_MODE_BITMAP:
                exitm::Exit_With_Message("Differentail File Animation Can not be bitmap");
                break;
            case ANIMATION_DRAW_MODE_SCREEN_BACKGROUND:
                exitm::Exit_With_Message("Differentail File Animation Can not be masked");
                break;
            case ANIMATION_DRAW_MODE_REFRESH_MASK:
                exitm::Exit_With_Message("Refresh Mask can't be drawn");
                break;
        }

        animate::animation_header->current_frame++;

        if (animate::animation_header->flags & 0x20) {
            if (animate::animation_header->current_frame >= animate::animation_header->frame_count) {
                if (((int32_t)animate::animation_header->loop_frame - animate::animation_header->frame_count) == -1) {
                    animate::animation_header->current_frame = 0;
                } else {
                    animate::animation_header->current_frame = animate::animation_header->loop_frame;
                }
            }
        } else {
            if (animate::animation_header->current_frame >= animate::animation_header->frame_count) {
                animate::animation_header->current_frame = animate::animation_header->frame_count - 1;
            }
        }
    }

    void __cdecl Draw_Multi_File_Animation(int16_t anim_idx) {
        animate::animation_header = (s_animation_header*)(((uint8_t*)init::io_buffer) + (anim_idx * FILE_ANI_MULTI_SLOT_SIZE));

        if (animate::animation_header->frame_count >= 0x2A6) {
            exitm::Exit_With_Message("Too many frames for multi file animation");
        }

        if ((animate::animation_header->flags & ANIMATION_FLAG_HAS_PALETTE) != 0) {
            animate::Set_Animation_Palette();
        }

        int16_t current_frame = animate::animation_header->current_frame;

        uint32_t frame_offset = Animation_Frame_Offset_(animate::animation_header, current_frame);
        uint32_t next_frame_offset = Animation_Frame_Offset_(animate::animation_header, current_frame + 1);

        uint32_t frame_size = next_frame_offset - frame_offset;

        int32_t max_space = allocate::Get_Max_Block_Space(file_animation_buffer);
        if (max_space < (int32_t)frame_size) {
            exitm::Exit_With_Message("File Animation Frame Too Large");
        }

        uint8_t* sprite = file_animation_buffer;
        vfs::fseek(file_animation[anim_idx], (int32_t)frame_offset, SEEK_SET);
        vfs::fread(sprite, frame_size, 1, file_animation[anim_idx]);

        if (video::screen_refresh_flag >= 2) {
            setvesa::Add_Square_To_Scan_List(
                0, 0, animate::animation_header->width - 1, animate::animation_header->height - 1);
        }

        uint8_t mode = animate::animation_header->flags & ANIMATION_FLAG_DRAW_MODE_MASK;
        switch (mode) {
            case ANIMATION_DRAW_MODE_ANIMATED_SPRITE:
                draw::Draw_Animated_Sprite(0, 0, sprite);
                break;
            case ANIMATION_DRAW_MODE_BITMAP:
                draw::Draw_Bitmap_Sprite(0, 0, sprite);
                break;
            case ANIMATION_DRAW_MODE_SCREEN_BACKGROUND:
                animate::Draw_Screen_Background_Off(sprite);
                if (video::screen_refresh_flag >= 2) {
                    setvesa::Add_Square_To_Scan_List(0, 0, 0x27F, 0x1DF);
                }
                break;
            case ANIMATION_DRAW_MODE_REFRESH_MASK:
                exitm::Exit_With_Message("Refresh Mask can't be drawn");
                break;
        }

        animate::animation_header->current_frame++;

        if ((animate::animation_header->flags & 0x20) != 0) {
            if (animate::animation_header->frame_count <= animate::animation_header->current_frame) {
                animate::animation_header->current_frame = animate::animation_header->loop_frame;
            }
        } else {
            if (animate::animation_header->frame_count <= animate::animation_header->current_frame) {
                animate::animation_header->current_frame = animate::animation_header->loop_frame;
            }
        }
    }

    int16_t __cdecl Get_Multi_File_Animation_Frame(int16_t anim_idx) {
        file_animation_header = ((uint8_t*)init::io_buffer) + anim_idx * FILE_ANI_MULTI_SLOT_SIZE;
        return *(int16_t*)(file_animation_header + 4);
    }

    void __cdecl Set_Multi_File_Animation_Frame(int16_t anim_idx, int16_t frame_index) {
        file_animation_header = ((uint8_t*)init::io_buffer) + anim_idx * FILE_ANI_MULTI_SLOT_SIZE;

        int16_t max_frames = *(int16_t*)(file_animation_header + 6);
        if (max_frames <= frame_index) {
            frame_index = max_frames - 1;
        }

        *(int16_t*)(file_animation_header + 4) = frame_index;
    }

    void __cdecl Draw_Multi_File_Animation_Palette(int32_t anim_idx) {
        animate::animation_header = (s_animation_header*)(((uint8_t*)init::io_buffer) + anim_idx * FILE_ANI_MULTI_SLOT_SIZE);

        if (animate::animation_header->frame_count >= 678) {
            exitm::Exit_With_Message("Too many frames for multi file animation");
        }

        if ((animate::animation_header->flags & ANIMATION_FLAG_HAS_PALETTE) != 0) {
            animate::Set_Animation_Palette();
        }
    }

    void __cdecl Reset_File_Animation_Buffer() {
        platform::Lock_Offscreen_Surface();
        file_animation_buffer = platform::offscreen_surface;
        platform::Unlock_Offscreen_Surface();
    }
} // namespace file_ani