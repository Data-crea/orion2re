#pragma once

namespace sound {
    SDL_AudioStream* __cdecl Open_Playback_Stream(uint16_t bits_per_sample, uint16_t channels, uint32_t sample_rate, int32_t volume_percent);
    void __cdecl Close_Playback_Stream(SDL_AudioStream*& stream);

    /**
     * Drives periodic sound and stream maintenance from the multimedia timer.
     * @param timer_id Multimedia timer identifier.
     * @param message Timer callback message code.
     * @param user_data Caller-supplied timer user data.
     * @param param1 Reserved timer callback parameter.
     * @param param2 Reserved timer callback parameter.
     */
    void __stdcall Sound_Timer_Callback(uint32_t timer_id, uint32_t message, uint32_t user_data, uint32_t param1, uint32_t param2);

    void __cdecl Fade_Stream_Down(uint8_t fade_rate);

    int32_t __cdecl Get_Stream_Volume();

    void __cdecl Stop_Stream();

    void __cdecl Stream_On();

    void __cdecl Set_Stream_Max_Volume(int32_t volume);

    void __cdecl Set_Stream_Filename(const char* filename);

    void __cdecl Play_Stream(int32_t stream_id);

    void __cdecl Loop_Stream(int32_t count);

    void __cdecl Set_Stream_Volume(int32_t volume_percent);

    void __cdecl Fade_Stream_Up(uint8_t fade_increment);

    uint8_t __cdecl Is_Stream_Playing();

    void __cdecl Set_Audio_Buffer(void* buffer);

    /** Stops effects and invalidates cached samples, leaving music and audio settings unchanged. */
    void Reset_Effect_Cache();

    void __cdecl Sound_On();

    void __cdecl Stop_All_Sounds();

    void __cdecl Resume_Sound(int32_t channel_idx);

    void __cdecl Set_Sound_Max_Volume(int32_t volume);

    void __cdecl Stop_Sound(int32_t channel_idx);

    void __cdecl Sound_Update();

    int32_t __cdecl Play_Sound(int32_t resource_id);

    void __cdecl Fadeout_Sound(int16_t channel_idx, int8_t volume_idx);

    void __cdecl Pitch_Bend_Sound(int32_t handle, int32_t pitch);

    void __cdecl Loop_Sound(int32_t channel_idx, int8_t loop_count);

    void __cdecl Destroy_Sound();

    void __cdecl Init_Sound();
} // namespace sound