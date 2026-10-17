#include "pch.h"

namespace bitmap {
    namespace {
        constexpr int32_t kBitmapFrameDataOffset = sizeof(s_animation_header) + sizeof(uint32_t);
        constexpr int32_t kMouseCursorDimension = 24;
        constexpr int32_t kMouseCursorPixelCount =
            kMouseCursorDimension * kMouseCursorDimension;
        constexpr int32_t kPaletteColorCount = 256;

        static_assert(kBitmapFrameDataOffset == 0x10, "Bitmap frame data offset mismatch");
        static_assert(kMouseCursorPixelCount == 0x240, "Mouse cursor image size mismatch");
    } // namespace

    uint32_t pixel_count;
    uint8_t* bitmap_temp1;
    uint32_t bitmap_temp2;
    uint8_t* brighten_palette_seg;
    int8_t brighten_palette_levels;
    int32_t brighten_start_color;

    void __cdecl Get_Bitmap_Actual_Size(s_animation_header* header, int16_t* out_min_x, int16_t* out_min_y, int16_t* out_actual_width, int16_t* out_actual_height) {
        animate::animation_header = header;
        int16_t width = header->width;
        int16_t height = header->height;

        // Pixel data starts at offset 16 in this bitmap format
        uint8_t* pixels = (uint8_t*)header + kBitmapFrameDataOffset;

        *out_min_x = 1000;
        *out_min_y = 1000;
        int16_t max_x = 0;
        int16_t max_y = 0;

        int16_t pixel_idx = 0;
        if (height > 0) {
            for (int16_t y = 0; y < height; ++y) {
                if (width > 0) {
                    for (int16_t x = 0; x < width; ++x) {
                        // Check if pixel is non-zero (not transparent)
                        if (pixels[pixel_idx] != 0) {
                            if (x < *out_min_x) {
                                *out_min_x = x;
                            }
                            if (y < *out_min_y) {
                                *out_min_y = y;
                            }
                            if (x > max_x) {
                                max_x = x;
                            }
                            if (y > max_y) {
                                max_y = y;
                            }
                        }
                        pixel_idx++;
                    }
                }
            }
        }

        *out_actual_height = (max_y - *out_min_y) + 1;
        *out_actual_width = (max_x - *out_min_x) + 1;
    }

    int32_t __cdecl Get_Bitmap_Size(int16_t width, int16_t height) {
        int32_t size_with_offset =
            (int32_t)height * (int32_t)width + kBitmapFrameDataOffset + 3;
        return (size_with_offset / 4) * 4;
    }

    void __cdecl Clear_Mouse_Image(int16_t image_idx) {
        struct_::Set_Memory(
            video::mouse_palette + image_idx * kMouseCursorPixelCount,
            kMouseCursorPixelCount,
            0);
    }

    void __cdecl Create_Bitmap_Header(int16_t width, int16_t height, s_animation_header* header) {
        animate::animation_header = header;
        header->width = width;
        header->height = height;
        header->current_frame = 0;
        header->frame_count = 1;
        header->loop_frame = 0;
        header->keyframe_interval = 0;
        header->flags = ANIMATION_DRAW_MODE_BITMAP;
        Animation_Frame_Offsets_(header)[0] = kBitmapFrameDataOffset;
    }

    void __cdecl Create_Blank_Bitmap(int16_t width, int16_t height, void* bitmap) {
        Create_Bitmap_Header(width, height, (s_animation_header*)bitmap);

        uint8_t* pixels = (uint8_t*)bitmap + kBitmapFrameDataOffset;
        uint32_t pixel_count = (uint32_t)((int32_t)width * (int32_t)height);
        struct_::Set_Memory(pixels, pixel_count, 0);
    }

    void __cdecl Screen_Bitmap_Capture(int16_t x1, int16_t y1, int16_t x2, int16_t y2, s_animation_header* header) {
        if (x2 < x1) {
            strings::Swap_Short(&x1, &x2);
            strings::Swap_Short(&y1, &y2);
        }

        int16_t width = x2 - x1 + 1;
        int16_t height = y2 - y1 + 1;

        Create_Bitmap_Header(width, height, header);

        video::g_screen_res_x = platform::Lock_Appropriate_Render_Buffer();

        uint8_t* dest = (uint8_t*)header + kBitmapFrameDataOffset;
        uint8_t* src = video::current_video_page + ((int32_t)y1 * video::g_screen_res_x) + (int32_t)x1;

        shear::Capture_Screen_Block(src, width, height, dest);

        platform::Unlock_Appropriate_Render_Buffer();
    }

    void __cdecl Set_Picture_Glassed(s_animation_header* anim) {
        animate::animation_header = anim;
        anim->flags |= ANIMATION_FLAG_GLASSED;
    }

    void __cdecl Flip_Bitmap(s_animation_header* dst_hdr, s_animation_header* src_hdr, int16_t flip_mode) {
        animate::animation_header = src_hdr;

        int32_t height = src_hdr->height;
        int32_t width = src_hdr->width;

        uint8_t* src_pixels = Animation_Frame_Data_(src_hdr, src_hdr->current_frame);

        if (flip_mode == 0 || flip_mode == 1) {
            if (src_hdr == dst_hdr) {
                shear::Flip_Bitmap_Down_Same(src_pixels, width, height);
            } else {
                Create_Bitmap_Header(width, height, dst_hdr);
                uint8_t* dst_pixels = (uint8_t*)dst_hdr + kBitmapFrameDataOffset;
                shear::Flip_Bitmap_Down_Different(src_pixels, width, height, dst_pixels);
            }
        } else {
            if (src_hdr == dst_hdr) {
                shear::Flip_Bitmap_Left_Same(src_pixels, width, height);
            } else {
                Create_Bitmap_Header(width, height, dst_hdr);
                uint8_t* dst_pixels = (uint8_t*)dst_hdr + kBitmapFrameDataOffset;
                shear::Flip_Bitmap_Left_Different(src_pixels, width, height, dst_pixels);
            }
        }
    }

    void __cdecl Copy_Bitmap_To_Bitmap(s_animation_header* dst, s_animation_header* src) {
        animate::animation_header = src;
        int16_t width = src->width;
        int16_t height = src->height;
        uint32_t offset = Animation_Frame_Offsets_(src)[src->current_frame];

        Create_Bitmap_Header(width, height, dst);

        uint8_t* src_ptr = (uint8_t*)src + offset;
        uint8_t* dst_ptr = (uint8_t*)dst + kBitmapFrameDataOffset;
        int32_t size = (int32_t)height * (int32_t)width;

        struct_::Copy_Structure(dst_ptr, src_ptr, size);
    }

    void __cdecl Outline_Bitmap(s_animation_header* dst, s_animation_header* src, int16_t color, int16_t no_glass) {
        animate::animation_header = src;

        int16_t width = src->width;
        int16_t height = src->height;
        int16_t current_frame = src->current_frame;

        uint8_t* src_pixels = Animation_Frame_Data_(src, current_frame);
        uint8_t* dst_pixels = src_pixels;

        if (src != dst) {
            Create_Bitmap_Header(width, height, dst);
            Copy_Bitmap_To_Bitmap(dst, src);
            // The newly created 1-frame destination header takes up 0x10 bytes
            // (0xC bytes header struct + 0x4 bytes for a single frame offset entry).
            dst_pixels = (uint8_t*)dst + kBitmapFrameDataOffset;
        }

        if (no_glass == 0) {
            shear::Outline_Bitmap_Pixels(src_pixels, color, 0, dst_pixels, width, height);
        } else {
            shear::Outline_Bitmap_Pixels_No_Glass(src_pixels, color, 0, dst_pixels, width, height);
        }
    }

    void __cdecl Tint_Bitmap(s_animation_header* dest, s_animation_header* src, int16_t tint_color_index) {
        animate::animation_header = src;
        int16_t width = src->width;
        int16_t height = src->height;

        uint8_t* src_pixels = Animation_Frame_Data_(src, src->current_frame);
        uint8_t* dest_pixels;

        if (src == dest) {
            dest_pixels = src_pixels;
        } else {
            // New header size + 1 frame offset (0xC + 0x4 = 0x10)
            dest_pixels = (uint8_t*)dest + kBitmapFrameDataOffset;
            Create_Bitmap_Header(width, height, dest);
            Copy_Bitmap_To_Bitmap(dest, src);
        }

        uint8_t* remap_table = video::glass_remap_colors[tint_color_index];

        Tint_Bitmap_Pixels(src_pixels, remap_table, 0, dest_pixels, width, height);
    }

    void __cdecl Mask_Out_Bitmap_With_Bitmap(int16_t x, int16_t y, s_animation_header* dest, s_animation_header* src, int16_t type) {
        animate::animation_header = src;

        uint8_t* src_frame_data_orig = Animation_Frame_Data_(src, src->current_frame);
        uint8_t* src_ptr = src_frame_data_orig;

        if (x < 0) {
            if (y < 0) {
                src_ptr -= y * src->width;
            }
            src_ptr -= x;
        } else if (y < 0) {
            src_ptr -= y * src->width;
        }

        animate::animation_header = dest;

        if (y > dest->height) {
            return;
        }
        if (x > dest->width) {
            return;
        }

        uint8_t* dest_frame_data_orig = Animation_Frame_Data_(dest, dest->current_frame);
        uint8_t* dest_ptr = dest_frame_data_orig;

        if (x < 0) {
            if (y < 0) {
                // empty
            } else {
                dest_ptr += y * dest->width;
            }
        } else {
            if (y < 0) {
                dest_ptr += x;
            } else {
                dest_ptr += y * dest->width + x;
            }
        }

        int src_skip = 0;
        if (x < 0) {
            src_skip = -x;
        }

        int draw_width = src->width + x;
        if (draw_width > dest->width) {
            src_skip += (x + src->width) - dest->width;
        }

        int dest_skip = (x < 0) ? 0 : x;
        if (draw_width < dest->width) {
            dest_skip = dest_skip - src->width + (dest->width - x);
        }

        int sub_x = (x < 0) ? 0 : x;
        if (draw_width >= dest->width) {
            draw_width = dest->width;
        }
        draw_width -= sub_x;

        int draw_height = dest->height;
        // BUG: src->width used instead of src->height for boundary check
        if (src->width + y < dest->height) {
            draw_height = src->height + y;
        }
        int sub_y = (y < 0) ? 0 : y;
        draw_height -= sub_y;

        if (src_ptr < src_frame_data_orig) {
            src_ptr = src_frame_data_orig;
        }
        if (dest_ptr < dest_frame_data_orig) {
            dest_ptr = dest_frame_data_orig;
        }

        if (type == 1) {
            shear::Mask_Bitmap_Pixels_Inverse(src_ptr, src_skip, dest_skip, dest_ptr, draw_width, draw_height);
        } else {
            shear::Mask_Bitmap_Pixels(src_ptr, src_skip, dest_skip, dest_ptr, draw_width, draw_height);
        }
    }

    void __cdecl Draw_Bitmap_Line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, s_animation_header* img) {
        int32_t dx = x2 - x1;
        int32_t dy = y2 - y1;
        int32_t line_len = special::isqrt(dy * dy + dx * dx);

        int32_t frame_offset = Animation_Frame_Offsets_(img)[img->current_frame];
        uint8_t* frame_pixels = (uint8_t*)img + frame_offset;

        if (x1 == x2 && y1 == y2) {
            return;
        }

        // OrionLayer, open fix 99. A line shorter than a hundredth of its
        // picture's width scales the picture to 0 % (scale_factor below) and
        // the scaling divides by it: SIGFPE. A tractor beam clipped at the
        // screen's edge to one pixel (Draw_Clipped_Tractor_Line) is such a
        // line. Nothing of it would be drawn: return before locking.
        if (img->width <= 0 || (line_len * 100) / img->width == 0) {
            return;
        }

        video::g_screen_res_x = platform::Lock_Appropriate_Render_Buffer();
        uint8_t* video_page = (uint8_t*)video::current_video_page;

        int32_t mid_x = (x1 + x2) / 2;
        int32_t mid_y = (y1 + y2) / 2;

        int32_t img_w = img->width;
        int32_t img_h = img->height;

        int32_t img_x1 = mid_x - img_w / 2;
        int32_t img_x2 = img_w / 2 + mid_x;
        int32_t img_y1 = mid_y - img_h / 2;
        int32_t img_y2 = img_h / 2 + mid_y;

        while (img_x2 - img_x1 >= img_w) {
            img_x2--;
        }

        while (img_y2 - img_y1 >= img_h) {
            img_y2--;
        }

        int32_t angle_val = angle::Get_Angle(dx, dy);

        int32_t ext_x1 = (img_w - line_len) / 2 + img_x1;
        int32_t ext_x2 = (line_len - img_w) / 2 + img_x2;
        int32_t scale_factor = (line_len * 100) / img_w;

        int32_t min_x = 0x27F; // 639
        int32_t min_y = 0x1DF; // 479
        int32_t max_x = 0;
        int32_t max_y = 0;

        int32_t rx, ry;

        // Point 1
        rx = ext_x1 - mid_x;
        ry = img_y1 - mid_y;
        angle::Rotate_Coordinates(-angle_val, &rx, &ry);
        rx += mid_x;
        ry += mid_y;
        if (rx < min_x) min_x = rx;
        if (ry < min_y) min_y = ry;
        if (rx > max_x) max_x = rx;
        if (ry > max_y) max_y = ry;

        // Point 2
        rx = ext_x1 - mid_x;
        ry = img_y2 - mid_y;
        angle::Rotate_Coordinates(-angle_val, &rx, &ry);
        rx += mid_x;
        ry += mid_y;
        if (rx < min_x) min_x = rx;
        if (ry < min_y) min_y = ry;
        if (rx > max_x) max_x = rx;
        if (ry > max_y) max_y = ry;

        // Point 3
        rx = ext_x2 - mid_x;
        ry = img_y1 - mid_y;
        angle::Rotate_Coordinates(-angle_val, &rx, &ry);
        rx += mid_x;
        ry += mid_y;
        if (rx < min_x) min_x = rx;
        if (ry < min_y) min_y = ry;
        if (rx > max_x) max_x = rx;
        if (ry > max_y) max_y = ry;

        // Point 4
        rx = ext_x2 - mid_x;
        ry = img_y2 - mid_y;
        angle::Rotate_Coordinates(-angle_val, &rx, &ry);
        rx += mid_x;
        ry += mid_y;
        if (rx < min_x) min_x = rx;
        if (ry < min_y) min_y = ry;
        if (rx > max_x) max_x = rx;
        if (ry > max_y) max_y = ry;

        // Point 5
        rx = ext_x1 - mid_x;
        ry = img_y1 - mid_y;
        angle::Rotate_Coordinates(angle_val, &rx, &ry);
        rx += mid_x;
        ry += mid_y;
        if (rx < min_x) min_x = rx;
        if (ry < min_y) min_y = ry;
        if (rx > max_x) max_x = rx;
        if (ry > max_y) max_y = ry;

        // Point 6
        rx = ext_x1 - mid_x;
        ry = img_y2 - mid_y;
        angle::Rotate_Coordinates(angle_val, &rx, &ry);
        rx += mid_x;
        ry += mid_y;
        if (rx < min_x) min_x = rx;
        if (ry < min_y) min_y = ry;
        if (rx > max_x) max_x = rx;
        if (ry > max_y) max_y = ry;

        // Point 7
        rx = ext_x2 - mid_x;
        ry = img_y1 - mid_y;
        angle::Rotate_Coordinates(angle_val, &rx, &ry);
        rx += mid_x;
        ry += mid_y;
        if (rx < min_x) min_x = rx;
        if (ry < min_y) min_y = ry;
        if (rx > max_x) max_x = rx;
        if (ry > max_y) max_y = ry;

        // Point 8
        rx = ext_x2 - mid_x;
        ry = img_y2 - mid_y;
        angle::Rotate_Coordinates(angle_val, &rx, &ry);
        rx += mid_x;
        ry += mid_y;
        if (rx < min_x) min_x = rx;
        if (ry < min_y) min_y = ry;
        if (rx > max_x) max_x = rx;
        if (ry > max_y) max_y = ry;

        if (min_x <= video::screen_window_x1) {
            min_x = video::screen_window_x1;
        }
        if (min_y <= video::screen_window_y1) {
            min_y = video::screen_window_y1;
        }
        if (max_x >= video::screen_window_x2) {
            max_x = video::screen_window_x2;
        }
        if (max_y >= video::screen_window_y2) {
            max_y = video::screen_window_y2;
        }

        setvesa::Add_Square_To_Scan_List(min_x, min_y, max_x, max_y);

        if (min_y <= max_y) {
            for (int32_t y = min_y; y <= max_y; ++y) {
                for (int32_t x = min_x; x <= max_x; ++x) {
                    rx = x - mid_x;
                    ry = y - mid_y;
                    angle::Rotate_Coordinates(-angle_val, &rx, &ry);
                    rx += mid_x;
                    ry += mid_y;

                    if (rx >= ext_x1 && rx <= ext_x2 && ry >= img_y1 && ry <= img_y2) {
                        int32_t scaled_x = (rx - mid_x) * 100 / scale_factor + mid_x;
                        uint8_t pixel = frame_pixels[(ry - img_y1) * img_w - img_x1 + scaled_x];
                        if (pixel != 0) {
                            video_page[y * 640 + x] = pixel;
                        }
                    }
                }
            }
        }

        platform::Unlock_Appropriate_Render_Buffer();
    }

    void __cdecl Draw_Bitmap_To_Mouse(int16_t x, int16_t y, s_animation_header* anim, int16_t mouse_type) {
        animate::animation_header = anim;

        int16_t draw_width = anim->width;
        int16_t draw_height = anim->height;
        int16_t src_skip = 0;

        if (draw_width + x > kMouseCursorDimension) {
            src_skip = draw_width + x - kMouseCursorDimension;
            draw_width = kMouseCursorDimension - x;
        }

        if (draw_height + y > kMouseCursorDimension) {
            draw_height = kMouseCursorDimension - y;
        }

        uint8_t* src = Animation_Frame_Data_(anim, anim->current_frame);
        int32_t src_idx = 0;
        int32_t dst_idx = x + y * kMouseCursorDimension;
        int32_t dst_skip = kMouseCursorDimension - draw_width;

        if (draw_height > 0) {
            for (int16_t h = draw_height; h > 0; --h) {
                if (draw_width > 0) {
                    for (int16_t w = draw_width; w > 0; --w) {
                        uint8_t pixel = src[src_idx];
                        if (pixel != 0) {
                            video::mouse_palette[mouse_type * kMouseCursorPixelCount + dst_idx] = pixel;
                        }
                        src_idx++;
                        dst_idx++;
                    }
                }
                src_idx += src_skip;
                dst_idx += dst_skip;
            }
        }

        platform::Update_Cursor_Image();
    }

    void __cdecl Create_Brighten_Bitmap_Palettes(s_animation_header* anim, int8_t* levels, int8_t level_count) {
        brighten_palette_levels = level_count;
        brighten_palette_seg = (uint8_t*)allocate::Allocate_Next_Block(
            anim,
            level_count * kPaletteColorCount + sizeof(uint32_t));

        if (brighten_palette_seg == nullptr) {
            exitm::Exit_With_Message("Not Enough Space For Brighten Palette Block");
        }

        for (int32_t i = 0; i < kPaletteColorCount; ++i) {
            int32_t h, l, s;
            palette::RGB_To_HLS(
                video::current_palette[i].r,
                video::current_palette[i].g,
                video::current_palette[i].b,
                &h,
                &l,
                &s);

            if (level_count > 0) {
                int32_t level_offset = 0;
                for (int32_t j = 0; j < level_count; ++j) {
                    int8_t level = levels[j];
                    if (level < 0 || level > 100) {
                        exitm::Exit_With_Message("Brighten perecnt needs to be between 0 - 100");
                    }

                    int l_new = l + (level * l * 10) / 1000;
                    if (l_new > 1000) {
                        l_new = 1000;
                    }

                    uint8_t r_new, g_new, b_new;
                    palette::HLS_To_RGB(h, l_new, s, &r_new, &g_new, &b_new);

                    uint8_t closest_color = remap::Find_Closest_Color(r_new, g_new, b_new);

                    brighten_palette_seg[level_offset + i] = closest_color;

                    level_offset += kPaletteColorCount;
                }
            }
        }
    }

    void __cdecl Mask_Out_Colors(s_animation_header* anim, int16_t start_color, int16_t end_color) {
        animate::animation_header = anim;
        uint32_t* pixels = (uint32_t*)Animation_Frame_Data_(anim, anim->current_frame);
        shear::Mask_Bitmap_Colors_Pixels(pixels, start_color, end_color, 0, anim->width, anim->height);
    }

    void __cdecl Mask_Brighten_Bitmap(int32_t x, int32_t y, s_animation_header* dst_anim, s_animation_header* src_anim, uint8_t color) {
        brighten_start_color = (int32_t)(int8_t)color;
        animate::animation_header = src_anim;

        int32_t src_height = src_anim->height;
        int32_t src_width = src_anim->width;

        uint8_t* src_pixels = Animation_Frame_Data_(src_anim, src_anim->current_frame);

        if (x < 0 && -x >= src_width) {
            return;
        }

        if (y < 0 && -y >= src_height) {
            return;
        }

        uint8_t* adjusted_src_pixels = src_pixels;
        if (x < 0) {
            if (y < 0) {
                adjusted_src_pixels -= (y * src_width) + x;
            } else {
                adjusted_src_pixels -= x;
            }
        } else {
            if (y < 0) {
                adjusted_src_pixels -= y * src_width;
            }
        }

        animate::animation_header = dst_anim;

        int32_t dst_width = dst_anim->width;
        int32_t dst_height = dst_anim->height;
        uint8_t* dst_pixels = Animation_Frame_Data_(dst_anim, dst_anim->current_frame);

        if (y > dst_height) {
            return;
        }
        if (x > dst_width) {
            return;
        }

        uint8_t* adjusted_dst_pixels = dst_pixels;
        if (x < 0) {
            if (y < 0) {
                // Do nothing
            } else {
                adjusted_dst_pixels += y * dst_width;
            }
        } else {
            if (y < 0) {
                adjusted_dst_pixels += x;
            } else {
                adjusted_dst_pixels += y * dst_width + x;
            }
        }

        int32_t src_skip = 0;
        if (x < 0) {
            src_skip = -x;
        }
        int32_t src_right = src_width + x;
        if (src_right > dst_width) {
            src_skip += src_right - dst_width;
        }

        int32_t dst_skip = (x >= 0) ? x : 0;
        if (src_right < dst_width) {
            dst_skip = dst_skip - x + (dst_width - src_width);
        }

        int32_t max_x = (x < 0) ? 0 : x;
        int32_t temp_width = src_width + x;
        if (temp_width >= dst_width) {
            temp_width = dst_width;
        }
        int32_t draw_width = temp_width - max_x;

        int32_t max_y = (y < 0) ? 0 : y;
        int32_t temp_height = dst_height;

        // BUG: using src_width instead of src_height for clipping check
        if (src_width + y < dst_height) {
            temp_height = src_height + y;
        }
        int32_t draw_height = temp_height - max_y;

        if (adjusted_src_pixels < src_pixels) {
            adjusted_src_pixels = src_pixels;
        }

        if (adjusted_dst_pixels < dst_pixels) {
            adjusted_dst_pixels = dst_pixels;
        }

        shear::Mask_Brighten_Bitmap_Pixels(adjusted_src_pixels, src_skip, dst_skip, adjusted_dst_pixels, draw_width, draw_height);
    }

    void __cdecl Tint_Bitmap_Pixels(uint8_t* src, uint8_t* tint_table, int32_t unused_param, uint8_t* dest, int32_t width, int32_t height) {
        (void)unused_param;
        bitmap_temp1 = tint_table;
        uint32_t count = (uint32_t)height * (uint32_t)width;
        pixel_count = count;

        do {
            uint8_t pixel = *src++;
            if (pixel != 0) {
                pixel = tint_table[pixel];
            }
            *dest++ = pixel;
            --count;
        } while (count != 0);
    }

    void __cdecl Scale_Bitmap(s_animation_header* dst, s_animation_header* src, int16_t scale_x, int16_t scale_y) {
        animate::animation_header = src;

        int orig_scale_x = scale_x;
        int src_width = src->width;
        int new_width = (orig_scale_x * src_width) / 100;

        int orig_scale_y = scale_y;
        int src_height = src->height;
        int new_height = (orig_scale_y * src_height) / 100;

        if (new_width <= 0 || new_height <= 0) {
            Create_Bitmap_Header(1, 1, dst);
            return;
        }

        uint8_t* src_frame_ptr = Animation_Frame_Data_(src, src->current_frame);
        uint8_t* dst_frame_ptr;

        if (src != dst) {
            Create_Bitmap_Header(new_width, new_height, dst);
            dst_frame_ptr = (uint8_t*)dst + kBitmapFrameDataOffset;
        } else {
            dst_frame_ptr = src_frame_ptr;
        }

        if (scale_x < 100 && scale_y > 100) {
            scale_x = 100;
        }
        if (scale_x > 100 && scale_y < 100) {
            scale_y = 100;
        }

        if (scale_x > 100 || scale_y > 100) {
            shear::Enlarge_Bitmap(src_frame_ptr, scale_x, scale_y, dst_frame_ptr, src_width, src_height, new_width, new_height);
        }

        if (orig_scale_x > 100) {
            orig_scale_x = 100;
        }
        if (orig_scale_y > 100) {
            orig_scale_y = 100;
        }

        // Original buggy behavior intentionally preserved: passes modified parameters
        // to Reduce_Bitmap instead of bounded original values.
        if (orig_scale_x < 100 || orig_scale_y < 100) {
            shear::Reduce_Bitmap(src_frame_ptr, scale_x, scale_y, dst_frame_ptr, src_width, src_height, new_width, new_height);
        }

        if (src == dst) {
            animate::animation_header->width = new_width;
            animate::animation_header->height = new_height;
        }
    }

    void __cdecl Vanish_Bitmap(s_animation_header* dest, s_animation_header* src, int16_t intensity) {
        animate::animation_header = src;
        int16_t current_frame = src->current_frame;
        int16_t width = src->width;
        uint8_t* src_data = Animation_Frame_Data_(src, current_frame);
        int16_t height = src->height;

        uint8_t* dest_data = src_data;
        if (src != dest) {
            bitmap::Create_Bitmap_Header(width, height, dest);
            dest_data = (uint8_t*)dest + kBitmapFrameDataOffset;
        }

        if (intensity >= 100) {
            struct_::Set_Memory(dest, height * width, 0);
            return;
        }

        if (intensity == 0) {
            if (src != dest) {
                struct_::Copy_Structure(dest_data, src_data, height * width);
            }
            return;
        }

        shear::Vanish_Bitmap_Pixels(src_data, intensity, 0, dest_data, width, height);
    }

    void __cdecl Add_Picture_To_Bitmap(int16_t x, int16_t y, s_animation_header* src, s_animation_header* dest) {
        animate::animation_header = dest;
        int16_t dest_width = dest->width;
        int32_t dest_width_i = dest_width;
        int16_t dest_height = dest->height;

        if ((dest->flags & ANIMATION_FLAG_DRAW_MODE_MASK) != ANIMATION_DRAW_MODE_BITMAP) {
            exitm::Exit_With_Message("Add_Picture_To_Bitmap targetmust be a bitmap.");
        }

        animate::animation_header = src;
        int16_t src_width = src->width;
        int32_t x_i = x;
        int16_t end_x = x + src_width - 1;
        int16_t end_y = src->height + y - 1;

        if (x_i < dest_width_i && y < dest_height && end_x >= 0 && end_y >= 0) {
            int32_t frame_offset = Animation_Frame_Offsets_(src)[src->current_frame];

            int32_t dest_x;
            int16_t src_skip_x;
            if (x < 0) {
                dest_x = 0;
                src_skip_x = -x;
            } else {
                src_skip_x = 0;
                dest_x = x;
            }

            int16_t src_skip_y;
            if (y < 0) {
                src_skip_y = -y;
                y = 0;
            } else {
                src_skip_y = 0;
            }

            int16_t draw_width;
            if (end_x < dest_width - 1) {
                draw_width = end_x - dest_x + 1;
            } else {
                draw_width = dest_width - dest_x;
            }

            int16_t draw_height;
            if (end_y < dest_height - 1) {
                draw_height = end_y - y + 1;
            } else {
                draw_height = dest_height - y;
            }

            if ((src->flags & ANIMATION_FLAG_DRAW_MODE_MASK) == ANIMATION_DRAW_MODE_BITMAP) {
                int32_t src_idx = src_skip_x + src_skip_y * src_width;
                int32_t dest_idx = y * dest_width + dest_x;

                if (draw_height > 0) {
                    int32_t rows = draw_height;
                    do {
                        int32_t cols = draw_width;
                        if (draw_width > 0) {
                            do {
                                uint8_t pixel = *((int8_t*)src + frame_offset + src_idx);
                                if (pixel != 0) {
                                    *((uint8_t*)dest + kBitmapFrameDataOffset + dest_idx) = pixel;
                                }
                                src_idx++;
                                dest_idx++;
                                cols--;
                            } while (cols != 0);
                        }
                        src_idx += src_width - draw_width;
                        dest_idx += dest_width - draw_width;
                        rows--;
                    } while (rows != 0);
                }
                return;
            } else {
                int32_t current_y =
                    reinterpret_cast<const s_packed_frame_header*>((uint8_t*)src + frame_offset)->start_y;
                int32_t word_idx = 2;
                int32_t dest_idx = y * dest_width + x;

                while (current_y < src_skip_y) {
                    const s_packed_frame_run* run =
                        reinterpret_cast<const s_packed_frame_run*>(
                            (uint8_t*)src + frame_offset + word_idx * 2);
                    int16_t val = run->pixel_count;
                    if (val == 0) {
                        current_y += static_cast<uint16_t>(run->skip_count);
                    } else {
                        word_idx += (val + 1) / 2;
                    }
                    word_idx += 2;
                }

                if (src_skip_y < current_y) {
                    dest_idx += (current_y - src_skip_y) * dest_width;
                }

                int32_t draw_y_idx = current_y - src_skip_y;
                int32_t clip_left = dest_idx + src_skip_x;
                int32_t clip_right = draw_width + src_skip_x + dest_idx;
                int32_t line_start_idx = dest_idx;
                int32_t run_idx = dest_idx;

                while (draw_y_idx < draw_height) {
                    const s_packed_frame_run* run =
                        reinterpret_cast<const s_packed_frame_run*>(
                            (uint8_t*)src + frame_offset + word_idx * 2);
                    word_idx += 2;

                    int16_t val = run->pixel_count;
                    int32_t segment_offset = run->skip_count;
                    if (val == 0) {
                        draw_y_idx += segment_offset;
                        line_start_idx += segment_offset * dest_width;
                        clip_left = src_skip_x + line_start_idx;
                        clip_right = draw_width + src_skip_x + line_start_idx;
                        run_idx = line_start_idx;
                    } else {
                        run_idx += segment_offset;
                        int32_t pixel_offset = word_idx * 2;
                        int32_t run_len = val;
                        int32_t run_end = run_idx + run_len;

                        if (run_idx < clip_right) {
                            if (run_idx < clip_left) {
                                int32_t over = clip_left - run_idx;
                                if (run_len < over) {
                                    over = run_len;
                                }
                                pixel_offset += over;
                                run_idx += over;
                            }

                            int32_t limit = clip_right;
                            if (run_end <= clip_right) {
                                limit = run_end;
                            }

                            for (; run_idx < limit; ++run_idx) {
                                *((uint8_t*)dest + kBitmapFrameDataOffset + run_idx) =
                                    *((uint8_t*)src + frame_offset + pixel_offset);
                                pixel_offset++;
                            }
                        }
                        word_idx += (run_len + 1) / 2;
                    }
                }
            }
        }
    }

    void __cdecl Draw_Picture_To_Bitmap(s_animation_header* dest, s_animation_header* src) {
        animate::animation_header = src;
        int16_t width = src->width;
        int16_t height = src->height;

        Create_Blank_Bitmap(width, height, dest);

        animate::animation_header = src;

        if ((src->flags & ANIMATION_FLAG_DRAW_MODE_MASK) == ANIMATION_DRAW_MODE_BITMAP) {
            uint8_t* src_pixels = Animation_Frame_Data_(src, src->current_frame);
            uint8_t* dst_pixels = (uint8_t*)dest + kBitmapFrameDataOffset;
            struct_::Copy_Structure(dst_pixels, src_pixels, width * height);
            return;
        }

        int8_t interval = (int8_t)src->keyframe_interval;
        int32_t start_frame = 0;

        if (interval != 0) {
            start_frame = (src->current_frame / interval) * interval;
        }

        if (start_frame <= src->current_frame) {
            uint8_t* dst_pixels = (uint8_t*)dest + kBitmapFrameDataOffset;

            for (int32_t frame = start_frame; frame <= animate::animation_header->current_frame; ++frame) {
                s_animation_header* anim = animate::animation_header;
                uint8_t* frame_data = Animation_Frame_Data_(anim, frame);

                int32_t y = reinterpret_cast<const s_packed_frame_header*>(frame_data)->start_y;
                int32_t word_offset = 2;
                int32_t pixel_offset_base = y * width;
                int32_t pixel_offset = pixel_offset_base;

                while (y < height) {
                    const s_packed_frame_run* run =
                        reinterpret_cast<const s_packed_frame_run*>(frame_data + word_offset * 2);
                    int16_t count = run->pixel_count;
                    int16_t segment_offset = run->skip_count;
                    word_offset += 2;

                    if (count == 0) {
                        y += segment_offset;
                        pixel_offset_base += segment_offset * width;
                        pixel_offset = pixel_offset_base;
                    } else {
                        pixel_offset += segment_offset;
                        int32_t count_ext = count;
                        int32_t end_offset = pixel_offset + count_ext;

                        if (end_offset > pixel_offset) {
                            int32_t src_offset = word_offset * 2;
                            while (pixel_offset < end_offset) {
                                dst_pixels[pixel_offset++] = frame_data[src_offset++];
                            }
                        }

                        word_offset += (count_ext + 1) / 2;
                    }
                }
            }
        }
    }

    void __cdecl Jumble_Bitmap(s_animation_header* dest, s_animation_header* src, int16_t noise_threshold, int16_t jumble_mask) {
        uint32_t seed = game_random::Get_Random_Seed();

        animate::animation_header = src;

        int16_t width = src->width;
        int16_t height = src->height;

        uint8_t* src_pixels = Animation_Frame_Data_(src, src->current_frame);
        uint8_t* dest_pixels;

        if (dest == src) {
            dest_pixels = src_pixels;
        } else {
            Create_Bitmap_Header(width, height, dest);
            Copy_Bitmap_To_Bitmap(dest, src);
            dest_pixels = (uint8_t*)dest + kBitmapFrameDataOffset;
        }

        shear::Jumble_Bitmap_Pixels(src_pixels, jumble_mask, noise_threshold, dest_pixels, width, height);

        game_random::Set_Random_Seed(seed);
    }

    void __cdecl Gray_Scale_Bitmap(
        s_animation_header* animation_header,
        uint32_t,
        int16_t start_color,
        const uint8_t* color_translation_table,
        int16_t end_color) {
        animate::animation_header = animation_header;

        const int32_t frame_index = animation_header->current_frame;
        const int32_t width = animation_header->width;
        const int32_t height = animation_header->height;
        uint8_t* const frame_pixels =
            Animation_Frame_Data_(animation_header, frame_index);

        replace::palette_color_list = replace::graphics_buffer;
        replace::Establish_Color_Gradient(start_color, end_color);

        for (int32_t i = 0; i < kPaletteColorCount; ++i) {
            replace::palette_color_list[i] =
                color_translation_table[replace::palette_color_list[i]];
        }

        shear::Gray_Scale_Fill_Pixels(
            frame_pixels,
            replace::palette_color_list,
            width,
            width,
            height);
    }
} // namespace bitmap