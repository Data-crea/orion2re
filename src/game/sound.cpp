#include "pch.h"
#ifdef ORION2RE_EXT
#include "ext/ext_api.h"  // OrionLayer, open fix 79. A recorded battle sound's changes.
#endif

namespace sound {
    void __cdecl Merge_Free_Blocks();
    int32_t __cdecl Get_Free_Sound_Index();
    void __cdecl Set_Master_Volume(int32_t volume);
    void __cdecl Update_Sound_Channels();
    void __cdecl Update_Sound_Channel_Status(int32_t channel_idx);
    void __cdecl Update_Stream_Status();
    void __cdecl Adjust_Settings();
    void __cdecl Audio_Clear_Cache();
    uint8_t __cdecl Read_Stream_Data(void* buffer, uint32_t bytes_to_read, uint32_t* bytes_read_ptr);
    void __cdecl Remove_Sound_Instance(int32_t instance_id);
    int8_t __cdecl Find_Free_Channel();
    void __cdecl Merge_Free_Sound_Blocks();
    int32_t __cdecl Play_Sound_File(int32_t channel_idx, void* wave_data, uint32_t wave_data_size);
    void* __cdecl Allocate_Sound_Block(int8_t channel_idx, int32_t resource_id, int32_t size);
    void __cdecl Stop_Stream_Loop();

#define STREAM_FILENAME_CAPACITY 260
#define SOUND_TIMER_INTERVAL_MS 160
#define SOUND_FADE_INTERVAL_MS 55
#define SOUND_CHANNEL_COUNT 16
#define SOUND_INDEX_COUNT 33
#define STREAM_PREFILL_BLOCKS 20
#define INFINITE_LOOP_COUNT 0xFFFF

    int16_t volume_table[101] = {
        -10000, -10000, -10000, -10000, -9287, -8643, -8117, -7673, -7287, -6947, -6643, -6368, -6117, -5886, -5673, -5473, -5287, -5112, -4947, -4791, -4643, -4503, -4368, -4240, -4117, -4000, -3886, -3777, -3673, -3571, -3473, -3379, -3287, -3198, -3112, -3029, -2947, -2868, -2791, -2716, -2643, -2572, -2503, -2435, -2368, -2304, -2240, -2178, -2117, -2058, -2000, -1942, -1886, -1831, -1777, -1724, -1673, -1621, -1571, -1522, -1473, -1426, -1379, -1333, -1287, -1242, -1198, -1155, -1112, -1070, -1029, -988, -947, -908, -868, -830, -791, -754, -716, -680, -643, -608, -572, -537, -503, -468, -435, -401, -368, -336, -304, -272, -240, -209, -178, -148, -117, -87, -58, -28, 0};

    uint32_t audioInitialized;
    uint8_t soundOn;
    uint8_t soundErrorOccurred;
    SDL_TimerID soundTimerId;
    SDL_Mutex* soundStateMutex;
    void* soundBuffer;
    uint32_t soundBufferSize;
    uint8_t audioPlaybackActive;
    int32_t global_stream_volume = 100;
    uint8_t loopStream;
    int32_t doubleBufferLoops;
    uint8_t command;
    uint8_t streamIncVal;
    uint32_t streamAvailable2;
    uint32_t streamAvailable1;
    int32_t masterVolume;
    s_sound_index soundIndices[33];
    s_sound_channel soundChannels[16];
    uint8_t close_stream = 1;
    uint8_t musicActive;
    s_sound_stream stream_data;
    char currentStreamFilename[STREAM_FILENAME_CAPACITY] = "stream.lbx";
    int32_t currentStreamId;
    int32_t currentStreamVolume = 100;
    uint8_t streamReadErrorLogged;
    s_vfs_file* streamEntryFile;

    namespace {
        class Sound_State_Guard_ {
          public:
            Sound_State_Guard_() {
                SDL_LockMutex(soundStateMutex);
            }

            ~Sound_State_Guard_() {
                SDL_UnlockMutex(soundStateMutex);
            }

            Sound_State_Guard_(const Sound_State_Guard_&) = delete;
            Sound_State_Guard_& operator=(const Sound_State_Guard_&) = delete;
        };

        void Copy_FourCC_Tag(char* dest, const char* src) {
            memcpy(dest, src, sizeof(uint32_t));
        }

        void Initialize_Sound_Indices(uint32_t buffer_size) {
            memset(soundIndices, 0, sizeof(soundIndices));

            for (int i = 0; i < SOUND_INDEX_COUNT; ++i) {
                soundIndices[i].next_id = -1;
                soundIndices[i].prev_id = -1;
            }

            soundIndices[0].next_id = 1;
            soundIndices[0].resource_id = -1;
            soundIndices[0].num_active_channels = 1000000;
            soundIndices[1].buffer_size = buffer_size;
            soundIndices[1].prev_id = 0;
        }

        SDL_AudioFormat Get_SDL_Audio_Format(uint16_t format_tag, uint16_t bits_per_sample) {
            if (format_tag != 1) {
                return SDL_AUDIO_UNKNOWN;
            }

            if (bits_per_sample == 8) {
                return SDL_AUDIO_U8;
            }

            if (bits_per_sample == 16) {
                return SDL_AUDIO_S16LE;
            }

            return SDL_AUDIO_UNKNOWN;
        }

        bool Channel_Loop_Enabled(const s_sound_channel& channel) {
            return (channel.flags & 0x0C) != 0;
        }

        void Destroy_SDL_Stream(SDL_AudioStream*& stream) {
            if (stream != nullptr) {
                SDL_AudioStream* destroyed_stream = stream;
                stream = nullptr;
                SDL_DestroyAudioStream(destroyed_stream);
            }
        }

        bool Set_SDL_Stream_Gain(SDL_AudioStream* stream, int32_t volume_percent) {
            if (stream == nullptr) {
                return false;
            }

            return SDL_SetAudioStreamGain(stream, (float)volume_percent / 100.0f);
        }

        void Flush_SDL_Stream(SDL_AudioStream* stream) {
            if (stream != nullptr) {
                SDL_FlushAudioStream(stream);
            }
        }

        void Clear_SDL_Stream(SDL_AudioStream* stream) {
            if (stream != nullptr) {
                SDL_ClearAudioStream(stream);
            }
        }

        void Pause_SDL_Stream(SDL_AudioStream* stream) {
            if (stream != nullptr) {
                SDL_PauseAudioStreamDevice(stream);
            }
        }

        void Resume_SDL_Stream(SDL_AudioStream* stream) {
            if (stream != nullptr) {
                SDL_ResumeAudioStreamDevice(stream);
            }
        }

        bool Queue_Channel_Data(s_sound_channel& channel, uint32_t bytes_to_queue) {
            if (channel.buffer == nullptr || bytes_to_queue == 0) {
                return false;
            }

            if (!SDL_PutAudioStreamData(channel.buffer, &channel.sample_data[channel.sample_offset], (int)bytes_to_queue)) {
                return false;
            }

            channel.sample_offset += bytes_to_queue;
            channel.ds_buffer_size = bytes_to_queue;
            return true;
        }

        bool Restart_Channel_Loop(s_sound_channel& channel) {
            if (!Channel_Loop_Enabled(channel)) {
                return false;
            }

            channel.sample_offset = 0;
            if (channel.loop_count > 0) {
                channel.loop_count--;
                if (channel.loop_count == 0) {
                    channel.flags &= ~0x0C;
                }
            }

            return true;
        }

        bool Create_SDL_Channel_Stream(s_sound_channel& channel, const s_sound_wav_header& info) {
            SDL_AudioSpec spec = {};
            spec.format = Get_SDL_Audio_Format(info.audio_format, info.bits_per_sample);
            spec.channels = info.num_channels;
            spec.freq = (int)info.sample_rate;

            if (spec.format == SDL_AUDIO_UNKNOWN) {
                return false;
            }

            channel.buffer = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (channel.buffer == nullptr) {
                return false;
            }

            channel.sample_rate = info.sample_rate;
            channel.current_volume = masterVolume;
            Set_SDL_Stream_Gain(channel.buffer, channel.current_volume);
            return true;
        }

        bool Rewind_Stream_Source() {
            return vfs::fseek(streamEntryFile, stream_data.data_start_offset, SEEK_SET) == 0;
        }

        bool Queue_Stream_Block(uint32_t bytes_to_queue) {
            if (stream_data.ds_buffer == nullptr || bytes_to_queue == 0) {
                return false;
            }

            uint8_t* block = (uint8_t*)malloc(bytes_to_queue);
            if (block == nullptr) {
                return false;
            }

            uint32_t bytes_read = 0;
            uint8_t read_status = Read_Stream_Data(block, bytes_to_queue, &bytes_read);
            bool queued = false;

            if (bytes_read != 0) {
                queued = SDL_PutAudioStreamData(stream_data.ds_buffer, block, (int)bytes_read);
                if (queued) {
                    stream_data.bytes_written += bytes_read;
                }
            }

            free(block);

            if (!queued) {
                return false;
            }

            return (read_status == 0 || read_status == 4);
        }

        bool Queue_Stream_Until_Target(uint32_t target_queued_bytes) {
            if (stream_data.ds_buffer == nullptr) {
                return false;
            }

            while (SDL_GetAudioStreamQueued(stream_data.ds_buffer) < (int)target_queued_bytes) {
                uint32_t bytes_remaining = stream_data.data_remaining - stream_data.bytes_written;

                if (bytes_remaining == 0) {
                    if (loopStream == 0) {
                        stream_data.status &= ~0x02;
                        Flush_SDL_Stream(stream_data.ds_buffer);
                        return true;
                    }

                    stream_data.bytes_written = 0;
                    if (!Rewind_Stream_Source()) {
                        return false;
                    }

                    doubleBufferLoops--;
                    if (doubleBufferLoops == 0) {
                        loopStream = 0;
                    }

                    bytes_remaining = stream_data.data_remaining;
                    if (bytes_remaining == 0) {
                        stream_data.status &= ~0x02;
                        Flush_SDL_Stream(stream_data.ds_buffer);
                        return true;
                    }
                }

                uint32_t bytes_to_queue = bytes_remaining;
                if (bytes_to_queue > stream_data.block_size) {
                    bytes_to_queue = stream_data.block_size;
                }

                if (!Queue_Stream_Block(bytes_to_queue)) {
                    return false;
                }
            }

            return true;
        }

        bool Stream_File_Exists(const char* filename) {
            s_vfs_file* file;

            if (filename == nullptr) {
                return false;
            }

            file = vfs::fopen(filename, "rb");
            if (file == nullptr) {
                return false;
            }

            vfs::fclose(file);
            return true;
        }

        Uint32 SDLCALL Sound_Timer_Thunk(void* userdata, SDL_TimerID timer_id, Uint32 interval) {
            (void)userdata;
            sound::Sound_Timer_Callback(timer_id, 0, 0, 0, 0);
            return interval;
        }

    } // namespace

    /**
     * @brief Iterates through active sound channels and updates their SDL stream gain.
     * Stops and removes channels if the volume delta is out of bounds.
     */
    void __cdecl Sound_Update() {
        Sound_State_Guard_ guard;
        const uint32_t current_ms = (uint32_t)SDL_GetTicks();

        for (int i = 0; i < SOUND_CHANNEL_COUNT; ++i) {
            s_sound_channel& channel = soundChannels[i];

            // Check if bit 0 is set (active flag) and volume is non-zero
            if ((channel.flags & 1) && channel.volume_index != 0) {
                const uint32_t elapsed_ms = current_ms - channel.fade_update_ms;
                if (elapsed_ms < SOUND_FADE_INTERVAL_MS) {
                    continue;
                }

                const uint32_t fade_steps = elapsed_ms / SOUND_FADE_INTERVAL_MS;
                channel.fade_update_ms += fade_steps * SOUND_FADE_INTERVAL_MS;
                int32_t vol_level = channel.current_volume;
                int64_t diff = (int64_t)vol_level -
                    (int64_t)fade_steps * channel.volume_index;

                // Valid delta range is [1, 100]
                if (diff >= 1 && diff <= 100) {
                    channel.current_volume = (int32_t)diff;
                    Set_SDL_Stream_Gain(channel.buffer, channel.current_volume);
                } else {
                    // Out of range: stop and cleanup
                    channel.flags ^= 1;
                    Remove_Sound_Instance(i);
                    Destroy_SDL_Stream(channel.buffer);
                    channel.volume_index = 0;
                    return; // Function exits immediately after processing one cleanup case
                }
            }
        }
    }

    /**
     * Opens a basic PCM playback stream using the already-initialized SDL audio backend.
     */
    SDL_AudioStream* __cdecl Open_Playback_Stream(uint16_t bits_per_sample, uint16_t channels, uint32_t sample_rate, int32_t volume_percent) {
        Sound_State_Guard_ guard;

        if (audioInitialized == 0 || channels == 0 || sample_rate == 0) {
            return nullptr;
        }

        SDL_AudioSpec spec = {};
        spec.format = Get_SDL_Audio_Format(1, bits_per_sample);
        spec.channels = channels;
        spec.freq = (int)sample_rate;

        if (spec.format == SDL_AUDIO_UNKNOWN) {
            return nullptr;
        }

        SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (stream == nullptr) {
            return nullptr;
        }

        if (volume_percent < 0) {
            volume_percent = 0;
        } else if (volume_percent > 100) {
            volume_percent = 100;
        }

        Set_SDL_Stream_Gain(stream, volume_percent);
        Resume_SDL_Stream(stream);
        return stream;
    }

    void __cdecl Close_Playback_Stream(SDL_AudioStream*& stream) {
        Sound_State_Guard_ guard;
        Destroy_SDL_Stream(stream);
    }

    /**
     * @brief Sets the internal audio buffer and updates its available size.
     *
     * @param buffer Pointer to the memory block to be used as the audio buffer.
     */
    void __cdecl Set_Audio_Buffer(void* buffer) {
        Sound_State_Guard_ guard;

        if (buffer == nullptr) {
            soundBufferSize = 0;
            soundBuffer = nullptr;
        } else {
            uint32_t remaining_space = allocate::Get_Remaining_Block_Space(buffer);
            soundBufferSize = remaining_space;

            if (remaining_space != 0) {
                soundBuffer = buffer;
            } else {
                soundBuffer = nullptr;
            }
        }

        Audio_Clear_Cache();
    }

    void __cdecl Reset_Sound_Channel(int8_t channel_idx) {
        soundChannels[channel_idx].sample_data = nullptr;
        soundChannels[channel_idx].sample_size = 0;
        soundChannels[channel_idx].sample_rate = 0;
        soundChannels[channel_idx].sample_offset = 0;
        soundChannels[channel_idx].ds_buffer_size = 0;
        soundChannels[channel_idx].fade_update_ms = 0;
        soundChannels[channel_idx].current_volume = 0;
        soundChannels[channel_idx].write_cursor = 0;
        soundChannels[channel_idx].flags = 0;
        soundChannels[channel_idx].volume_index = 0;
    }

    void __cdecl Audio_Clear_Cache() {
        sound::Stop_All_Sounds();

        uint32_t buffer_size = sound::soundBufferSize;
        if (sound::soundBuffer != nullptr) {
            memset(sound::soundBuffer, 0, buffer_size);
        }

        Initialize_Sound_Indices(buffer_size);
    }

    void __cdecl Fade_Stream_Down(uint8_t fade_rate) {
        Sound_State_Guard_ guard;
        command = 2;
        streamIncVal = fade_rate;
    }

    void __cdecl Stream_On() {
        return;
    }

    void __cdecl Set_Stream_Max_Volume(int32_t volume) {
        Sound_State_Guard_ guard;

        if (volume >= 0 && volume <= 100) {
            global_stream_volume = volume;
            Set_Stream_Volume(volume);
        }
    }

    void __cdecl Set_Sound_Max_Volume(int32_t volume) {
        Sound_State_Guard_ guard;

        if (volume >= 0 && volume <= 100) {
            sound::masterVolume = volume;
            sound::Set_Master_Volume(volume);
        }
    }

    void __cdecl Set_Stream_Filename(const char* filename) {
        Sound_State_Guard_ guard;

        if (sound::audioInitialized != 0 && filename != nullptr) {
            uint8_t was_music_active = musicActive;

            strlcpy(currentStreamFilename, filename, sizeof(currentStreamFilename));

            if (!Stream_File_Exists(filename)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "stream: unable to open stream file %s", filename);
                musicActive = 0;
                return;
            }

            if (was_music_active == 1) {
                musicActive = 1;
            }
        }
    }

    bool __cdecl Init_Wave_Header_From_Memory(const void* src_data, uint32_t src_data_size, s_sound_wav_header* header) {
        char tag_buf[5];
        tag_buf[4] = '\0';

        if (src_data == nullptr || header == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sound: invalid wave header parse request");
            return false;
        }

        if (src_data_size < sizeof(s_sound_wav_header_disk)) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "sound: wave header too small size=%u expected=%u",
                src_data_size,
                (unsigned int)sizeof(s_sound_wav_header_disk));
            return false;
        }

        memset(header, 0, sizeof(s_sound_wav_header));
        memcpy(header, src_data, sizeof(s_sound_wav_header_disk));

        // Set data pointer to the start of raw audio data in the source buffer
        header->data_ptr = (uint8_t*)src_data + sizeof(s_sound_wav_header_disk);

        Copy_FourCC_Tag(tag_buf, header->riff_tag);
        if (strcasecmp(tag_buf, "RIFF") != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "sound: invalid in-memory RIFF tag %.4s", tag_buf);
            return false;
        }

        Copy_FourCC_Tag(tag_buf, header->wave_tag);
        if (strcasecmp(tag_buf, "WAVE") != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "sound: invalid in-memory WAVE tag %.4s", tag_buf);
            return false;
        }

        Copy_FourCC_Tag(tag_buf, header->fmt_tag);
        if (strcasecmp(tag_buf, "fmt ") != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "sound: invalid in-memory fmt tag %.4s", tag_buf);
            return false;
        }

        Copy_FourCC_Tag(tag_buf, header->data_tag);
        if (strcasecmp(tag_buf, "data") != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "sound: invalid in-memory data tag %.4s", tag_buf);
            return false;
        }

        if (header->data_size > src_data_size - sizeof(s_sound_wav_header_disk)) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "sound: wave data truncated declared=%u available=%u",
                header->data_size,
                src_data_size - (uint32_t)sizeof(s_sound_wav_header_disk));
            return false;
        }

        return true;
    }

    /**
     * Releases SDL audio stream resources and stops the periodic sound timer.
     */
    void __cdecl Destroy_Sound() {
        if (soundTimerId != 0) {
            SDL_RemoveTimer(soundTimerId);
            soundTimerId = 0;
        }

        Sound_State_Guard_ guard;

        for (int i = 0; i < SOUND_CHANNEL_COUNT; ++i) {
            Destroy_SDL_Stream(soundChannels[i].buffer);
        }

        Stop_Stream_Loop();
        SDL_AudioStream* destroyed_stream = stream_data.ds_buffer;
        stream_data.ds_buffer = nullptr;
        audioPlaybackActive = 0;
        stream_data.status = 0;
        Destroy_SDL_Stream(destroyed_stream);
        if (streamEntryFile != nullptr) {
            vfs::fclose(streamEntryFile);
            streamEntryFile = nullptr;
        }

        audioInitialized = 0;

        // SDL_RemoveTimer does not wait for a callback that was already dispatched.
        // Keep the mutex alive until SDL_Quit so a late callback remains safe.
    }

    void __cdecl Init_Sound() {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "sound: Init_Sound start");

        if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                soundErrorOccurred = 1;
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sound: SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s", SDL_GetError());
                return;
            }
        }

        if (soundStateMutex == nullptr) {
            soundStateMutex = SDL_CreateMutex();
            if (soundStateMutex == nullptr) {
                soundErrorOccurred = 1;
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sound: SDL_CreateMutex failed: %s", SDL_GetError());
                return;
            }
        }

        Sound_State_Guard_ guard;

        const char* audio_driver = SDL_GetCurrentAudioDriver();
        SDL_LogInfo(
            SDL_LOG_CATEGORY_APPLICATION,
            "sound: SDL audio subsystem initialized, driver: %s",
            audio_driver != nullptr ? audio_driver : "<unknown>");

        if (soundBuffer != nullptr) {
            soundBufferSize = allocate::Get_Remaining_Block_Space(soundBuffer);
            memset(soundBuffer, 0, soundBufferSize);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "sound: sound buffer ready size=%u", soundBufferSize);
        }

        Initialize_Sound_Indices(soundBufferSize);

        for (int i = 0; i < SOUND_CHANNEL_COUNT; ++i) {
            soundChannels[i].buffer = nullptr;
            Reset_Sound_Channel(i);
        }

        soundTimerId = SDL_AddTimer(SOUND_TIMER_INTERVAL_MS, Sound_Timer_Thunk, nullptr);
        if (soundTimerId != 0) {
            masterVolume = 100;
            soundOn = 1;
            audioPlaybackActive = 1;
            audioInitialized = 1;
            musicActive = 1;
            soundErrorOccurred = 0;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "sound: Init_Sound complete timer=%u", (unsigned int)soundTimerId);
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sound: SDL_AddTimer failed: %s", SDL_GetError());
        }
    }

    void __stdcall Sound_Timer_Callback(uint32_t timer_id, uint32_t message, uint32_t user_data, uint32_t param1, uint32_t param2) {
        Sound_State_Guard_ guard;

        if (audioInitialized != 1) {
            return;
        }

        if (sound::soundOn == 1) {
            sound::Update_Sound_Channels();
        }

        if (sound::audioPlaybackActive == 1 && sound::musicActive == 1) {
            sound::Adjust_Settings();
        }
    }

    void __cdecl Loop_Stream(int32_t count) {
        Sound_State_Guard_ guard;

        if (count == -1) {
            count = INFINITE_LOOP_COUNT;
        }

        loopStream = 1;
        doubleBufferLoops = count;
    }

    void __cdecl Fade_Stream_Up(uint8_t fade_increment) {
        Sound_State_Guard_ guard;
        command = 1;
        streamIncVal = fade_increment;
    }

    /**
     * Sets the volume of the sound stream buffer, capped by the global stream volume limit.
     * @param volume_percent The desired volume as a percentage (0-100).
     */
    void __cdecl Set_Stream_Volume(int32_t volume_percent) {
        Sound_State_Guard_ guard;

        if (sound::audioPlaybackActive != 0 && stream_data.ds_buffer != nullptr) {
            // Validate that the volume percentage is within the expected range
            if (volume_percent >= 0 && volume_percent <= 100) {
                // Cap the requested volume to the global stream volume limit
                if (volume_percent > sound::global_stream_volume) {
                    volume_percent = sound::global_stream_volume;
                }

                currentStreamVolume = volume_percent;
                Set_SDL_Stream_Gain(stream_data.ds_buffer, volume_percent);
            }
        }
    }

    void __cdecl Sound_On() {
        Sound_State_Guard_ guard;

        if (audioInitialized == 1) {
            soundOn = 1;
        }
    }

    /**
     * Sets one channel volume, or updates the master volume when channel_idx is -1.
     */
    void __cdecl Set_Channel_Volume(int32_t channel_idx, int32_t volume) {
        Sound_State_Guard_ guard;

        if (sound::soundOn == 0) {
            return;
        }

        if (channel_idx == -1) {
            sound::Set_Master_Volume(volume);
            return;
        }

        if (volume >= 0 && volume <= 100) {
            if (sound::masterVolume < volume) {
                volume = sound::masterVolume;
            }

            if (channel_idx >= 0 && channel_idx < SOUND_CHANNEL_COUNT) {
                soundChannels[channel_idx].current_volume = volume;
                Set_SDL_Stream_Gain(soundChannels[channel_idx].buffer, volume);
            }
        }
    }

    void __cdecl Stop_Sound(int32_t channel_idx) {
        Sound_State_Guard_ guard;

        if (sound::soundOn == 0) {
            return;
        }

        if (channel_idx == -1) {
            sound::Stop_All_Sounds();
            return;
        }

        if (channel_idx >= 0 && channel_idx < SOUND_CHANNEL_COUNT) {
            s_sound_channel* channel = &sound::soundChannels[channel_idx];
            if ((channel->flags & 1) != 0) {
                channel->flags ^= 1;
                sound::Remove_Sound_Instance(channel_idx);
                Destroy_SDL_Stream(channel->buffer);
            }
        }
    }

    void __cdecl Stop_All_Sounds() {
        Sound_State_Guard_ guard;

        if (sound::soundOn != 0) {
            for (int32_t i = 0; i < SOUND_CHANNEL_COUNT; ++i) {
                sound::Stop_Sound(i);
            }
        }
    }

    /**
     * Placeholder stub for resuming sound playback on a channel.
     * @param channel_idx Channel index to resume.
     */
    void __cdecl Resume_Sound(int32_t channel_idx) {
        (void)channel_idx;
        return;
    }

    int32_t __cdecl Get_Stream_Volume() {
        Sound_State_Guard_ guard;

        if (sound::audioPlaybackActive != 0 && stream_data.ds_buffer != nullptr) {
            return currentStreamVolume;
        }

        return 0;
    }

    void __cdecl Stop_Stream() {
        Sound_State_Guard_ guard;

        if (audioPlaybackActive != 0 && stream_data.ds_buffer != nullptr) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "stream: stopped file=%s entry=%d", currentStreamFilename, currentStreamId);
            Stop_Stream_Loop();
            SDL_AudioStream* destroyed_stream = stream_data.ds_buffer;
            stream_data.ds_buffer = nullptr;
            loopStream = 0;
            audioPlaybackActive = 0;
            Destroy_SDL_Stream(destroyed_stream);
        }
    }

    void __cdecl Pitch_Bend_Sound(int32_t handle, int32_t pitch) {
        Sound_State_Guard_ guard;
#ifdef ORION2RE_EXT
        if (ext::Battle_Sound_Change(handle, 3, pitch)) return;  // OrionLayer, open fix 79. A recorded battle sound: no channel.
#endif

        if (handle >= 1 && handle < SOUND_CHANNEL_COUNT) {
            s_sound_channel& channel = soundChannels[handle];
            if (channel.buffer == nullptr || channel.sample_rate == 0) {
                return;
            }

            SDL_AudioStream* stream = channel.buffer;
            float ratio = SDL_GetAudioStreamFrequencyRatio(stream);
            if (ratio <= 0.0f) {
                ratio = 1.0f;
            }

            float frequency = (float)channel.sample_rate * ratio;
            frequency += (float)pitch;
            if (frequency < 1.0f) {
                frequency = 1.0f;
            }

            SDL_SetAudioStreamFrequencyRatio(stream, frequency / (float)channel.sample_rate);
        }
    }

    int8_t __cdecl Load_Stream_Wave_Header(int32_t entry_index, s_sound_stream* stream) {
        if (entry_index == 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "stream: rejected empty entry file=%s entry=%d", currentStreamFilename, entry_index);
            return 0;
        }

        if (stream == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "stream: null header target file=%s entry=%d", currentStreamFilename, entry_index);
            return 0;
        }

        if (entry_index < 1) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "stream: invalid entry file=%s entry=%d",
                currentStreamFilename,
                entry_index);
            return 0;
        }

        memset(stream, 0, sizeof(s_sound_stream));

        if (streamEntryFile != nullptr) {
            vfs::fclose(streamEntryFile);
            streamEntryFile = nullptr;
        }

        streamEntryFile = vfs::fopen_lbx(currentStreamFilename, entry_index);
        if (streamEntryFile == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "stream: entry open failed file=%s entry=%d", currentStreamFilename, entry_index);
            return 0;
        }

        char tag_buffer[5];
        tag_buffer[4] = '\0';

        s_sound_wav_header_disk disk_header = {};

        if (vfs::fread(&disk_header, sizeof(disk_header), 1, streamEntryFile) != 1) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "stream: header read failed file=%s entry=%d", currentStreamFilename, entry_index);
            vfs::fclose(streamEntryFile);
            streamEntryFile = nullptr;
            return 0;
        }

        memcpy(stream, &disk_header, sizeof(disk_header));
        stream->data_start_offset = (uint32_t)sizeof(s_sound_wav_header_disk);

        Copy_FourCC_Tag(tag_buffer, stream->riff_tag);
        if (strcasecmp(tag_buffer, "RIFF") != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "stream: invalid RIFF tag file=%s entry=%d tag=%.4s", currentStreamFilename, entry_index, tag_buffer);
            vfs::fclose(streamEntryFile);
            streamEntryFile = nullptr;
            return 0;
        }

        Copy_FourCC_Tag(tag_buffer, stream->wave_tag);
        if (strcasecmp(tag_buffer, "WAVE") != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "stream: invalid WAVE tag file=%s entry=%d tag=%.4s", currentStreamFilename, entry_index, tag_buffer);
            vfs::fclose(streamEntryFile);
            streamEntryFile = nullptr;
            return 0;
        }

        Copy_FourCC_Tag(tag_buffer, stream->fmt_tag);
        if (strcasecmp(tag_buffer, "fmt ") != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "stream: invalid fmt tag file=%s entry=%d tag=%.4s", currentStreamFilename, entry_index, tag_buffer);
            vfs::fclose(streamEntryFile);
            streamEntryFile = nullptr;
            return 0;
        }

        Copy_FourCC_Tag(tag_buffer, stream->data_tag);
        if (strcasecmp(tag_buffer, "data") != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "stream: invalid data tag file=%s entry=%d tag=%.4s", currentStreamFilename, entry_index, tag_buffer);
            vfs::fclose(streamEntryFile);
            streamEntryFile = nullptr;
            return 0;
        }

        if (stream->channels == 0 || stream->samples_per_sec == 0 || stream->total_data_size == 0) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "stream: invalid audio params file=%s entry=%d channels=%u rate=%u bytes=%u",
                currentStreamFilename,
                entry_index,
                (unsigned int)stream->channels,
                (unsigned int)stream->samples_per_sec,
                (unsigned int)stream->total_data_size);
            vfs::fclose(streamEntryFile);
            streamEntryFile = nullptr;
            return 0;
        }

        if (vfs::fseek(streamEntryFile, (int32_t)stream->data_start_offset, SEEK_SET) != 0) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "stream: seek to data failed file=%s entry=%d data_offset=%u",
                currentStreamFilename,
                entry_index,
                stream->data_start_offset);
            vfs::fclose(streamEntryFile);
            streamEntryFile = nullptr;
            return 0;
        }

        return 1;
    }

    void __cdecl Play_Stream(int32_t stream_id) {
        Sound_State_Guard_ guard;

        if (stream_id == 0 || musicActive == 0) {
            return;
        }

        currentStreamId = stream_id;
        streamReadErrorLogged = 0;

        int32_t volume_idx = Get_Stream_Volume();
        Stop_Stream_Loop();

        SDL_AudioStream* destroyed_stream = stream_data.ds_buffer;
        stream_data.ds_buffer = nullptr;
        Destroy_SDL_Stream(destroyed_stream);

        if (Load_Stream_Wave_Header(stream_id, &stream_data) == 0) {
            return;
        }

        stream_data.block_size = ((stream_data.avg_bytes_per_sec * 320) / 100) / 20;
        stream_data.data_remaining = stream_data.total_data_size;
        stream_data.ds_buffer_size = stream_data.block_size * 20;
        stream_data.bytes_written = 0;
        stream_data.last_lock_size = stream_data.block_size;
        stream_data.write_cursor = 0;
        stream_data.status = 0;

        SDL_AudioSpec stream_spec = {};
        stream_spec.format = Get_SDL_Audio_Format(stream_data.format_tag, stream_data.bits_per_sample);
        stream_spec.channels = stream_data.channels;
        stream_spec.freq = (int)stream_data.samples_per_sec;

        if (stream_spec.format == SDL_AUDIO_UNKNOWN) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "stream: unsupported audio format file=%s entry=%d format=%u bits=%u", currentStreamFilename, currentStreamId, stream_data.format_tag, stream_data.bits_per_sample);
            return;
        }

        stream_data.ds_buffer = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &stream_spec, nullptr, nullptr);
        if (stream_data.ds_buffer == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "stream: SDL_OpenAudioDeviceStream failed file=%s entry=%d error=%s", currentStreamFilename, currentStreamId, SDL_GetError());
            return;
        }

        stream_data.status |= 0x01;
        if (stream_data.data_remaining > 0) {
            stream_data.status |= 0x02;
        }

        if (!Queue_Stream_Until_Target(stream_data.block_size * STREAM_PREFILL_BLOCKS)) {
            SDL_AudioStream* failed_stream = stream_data.ds_buffer;
            stream_data.ds_buffer = nullptr;
            Destroy_SDL_Stream(failed_stream);
            return;
        }

        currentStreamVolume = volume_idx;
        Set_SDL_Stream_Gain(stream_data.ds_buffer, volume_idx);
        Resume_SDL_Stream(stream_data.ds_buffer);

        audioPlaybackActive = 1;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "stream: started file=%s entry=%d", currentStreamFilename, currentStreamId);
    }

    uint8_t __cdecl Read_Stream_Data(void* buffer, uint32_t bytes_to_read, uint32_t* bytes_read_ptr) {
        size_t bytes_read = vfs::fread(buffer, 1, bytes_to_read, sound::streamEntryFile);
        if (bytes_read == bytes_to_read) {
            *bytes_read_ptr = (uint32_t)bytes_read;
            return 0;
        }

        uint32_t status = 4u;

        if (streamReadErrorLogged == 0) {
            streamReadErrorLogged = 1;
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "stream: read failed file=%s entry=%d status=%u requested=%u copied=%u bytes_written=%u total_data=%u data_offset=%u loop=%u",
                currentStreamFilename,
                currentStreamId,
                status,
                bytes_to_read,
                (uint32_t)bytes_read,
                stream_data.bytes_written,
                stream_data.total_data_size,
                stream_data.data_start_offset,
                loopStream);
        }

        fonts::Disp_PSTR(320 /* 0x140 */, 0, "Cant Read........");
        *bytes_read_ptr = (uint32_t)bytes_read;
        return (uint8_t)status;
    }

    /**
     * Stores the fade target volume index for a channel.
     */
    void __cdecl Fadeout_Sound(int16_t channel_idx, int8_t volume_idx) {
        Sound_State_Guard_ guard;
#ifdef ORION2RE_EXT
        if (ext::Battle_Sound_Change(channel_idx, 1, volume_idx)) return;  // OrionLayer, open fix 79. A recorded battle sound: no channel.
#endif

        if (channel_idx >= 0 && channel_idx < SOUND_CHANNEL_COUNT) {
            if (soundChannels[channel_idx].volume_index == 0) {
                soundChannels[channel_idx].fade_update_ms = (uint32_t)SDL_GetTicks();
            }
            soundChannels[channel_idx].volume_index = volume_idx;
        }
    }

    void __cdecl Loop_Sound(int32_t channel_idx, int8_t loop_count) {
        Sound_State_Guard_ guard;
#ifdef ORION2RE_EXT
        if (ext::Battle_Sound_Change(channel_idx, 2, loop_count)) return;  // OrionLayer, open fix 79. A recorded battle sound: no channel.
#endif

        if (channel_idx >= 1) {
            soundChannels[channel_idx].flags |= 0x08;
            soundChannels[channel_idx].loop_count = loop_count;
        }
    }

    void __cdecl Update_Sound_Channels() {
        Sound_State_Guard_ guard;

        if (sound::soundOn == 1) {
            for (int i = 0; i < SOUND_CHANNEL_COUNT; ++i) {
                if (sound::soundChannels[i].flags & 1) {
                    sound::Update_Sound_Channel_Status(i);
                }
            }
        }
    }

    /**
     * Sets the master playback volume for all active sound channels.
     */
    void __cdecl Set_Master_Volume(int32_t volume) {
        Sound_State_Guard_ guard;

        if (sound::soundOn != 0 && volume >= 0 && volume <= 100) {
            // Cap volume to master volume limit
            if (volume > sound::masterVolume) {
                volume = sound::masterVolume;
            }

            for (int i = 0; i < SOUND_CHANNEL_COUNT; ++i) {
                sound::soundChannels[i].current_volume = volume;
                Set_SDL_Stream_Gain(sound::soundChannels[i].buffer, volume);
            }
        }
    }

    /**
     * @brief Removes a sound instance ID from the active channel lists.
     *
     * Iterates through sound indices (skipping index 0) and searches for the given instance_id.
     * If found, the instance is removed by swapping it with the last active instance
     * in that sound's channel list.
     */
    void __cdecl Remove_Sound_Instance(int32_t instance_id) {
        for (int i = 1; i < SOUND_INDEX_COUNT; ++i) {
            s_sound_index& sound_idx = soundIndices[i];

            for (int j = 0; j < sound_idx.num_active_channels; ++j) {
                // Sign-extended comparison of the byte-sized instance ID
                if ((int8_t)sound_idx.active_channel_ids[j] == instance_id) {
                    if (sound_idx.num_active_channels > 1) {
                        sound_idx.num_active_channels--;
                        // Replace removed instance with the last one in the list
                        sound_idx.active_channel_ids[j] = sound_idx.active_channel_ids[sound_idx.num_active_channels];
                    } else {
                        // If it was the last active channel, set count to -1
                        sound_idx.num_active_channels = -1;
                    }
                    return;
                }
            }
        }
    }

    void __cdecl Adjust_Settings() {
        Sound_State_Guard_ guard;

        if (sound::audioPlaybackActive != 1 || !(sound::stream_data.status & 0x01)) {
            return;
        }

        if (sound::command == 1 /* Fade In */) {
            int32_t current_vol = sound::Get_Stream_Volume();
            if (current_vol < sound::global_stream_volume) {
                current_vol += (int32_t)(int8_t)sound::streamIncVal;
                if (current_vol >= sound::global_stream_volume) {
                    sound::command = 0;
                    current_vol = sound::global_stream_volume;
                }
                sound::Set_Stream_Volume(current_vol);
            } else {
                sound::command = 0;
            }
        }

        if (sound::command == 2 /* Fade Out */) {
            int32_t current_vol = sound::Get_Stream_Volume();
            if (current_vol > 0) {
                current_vol -= (int32_t)(int8_t)sound::streamIncVal;
                if (current_vol <= 0) {
                    current_vol = 0;
                    sound::command = 0;
                }
                sound::Set_Stream_Volume(current_vol);
            } else {
                sound::command = 0;
            }
        }

        if (sound::stream_data.status & 0x02) {
            if (sound::stream_data.ds_buffer == nullptr) {
                return;
            }

            if (!Queue_Stream_Until_Target(sound::stream_data.block_size * STREAM_PREFILL_BLOCKS)) {
                return;
            }
        } else if (!(sound::stream_data.status & 0x08)) {
            sound::Update_Stream_Status();
        }
    }

    int32_t __cdecl Play_Sound_File(int32_t channel_idx, void* wave_data, uint32_t wave_data_size) {
        Sound_State_Guard_ guard;
        s_sound_wav_header info;

        (void)wave_data_size;

        if (!Init_Wave_Header_From_Memory(wave_data, wave_data_size, &info)) {
            exitm::Exit_With_Message("SOUND ERROR: Reading Wave");
        }

        Remove_Sound_Instance(channel_idx);
        Destroy_SDL_Stream(soundChannels[channel_idx].buffer);
        Reset_Sound_Channel(channel_idx);

        if (!Create_SDL_Channel_Stream(soundChannels[channel_idx], info)) {
            exitm::Exit_With_Message("SOUND ERROR: Unable to open SDL audio stream.");
        }

        soundChannels[channel_idx].sample_size = info.data_size;
        soundChannels[channel_idx].sample_data = (uint8_t*)info.data_ptr;
        soundChannels[channel_idx].sample_rate = info.sample_rate;
        soundChannels[channel_idx].volume_index = 0;
        soundChannels[channel_idx].current_volume = masterVolume;

        if (!Queue_Channel_Data(soundChannels[channel_idx], soundChannels[channel_idx].sample_size)) {
            exitm::Exit_With_Message("SOUND ERROR: Unable to queue SDL channel audio.");
        }

        soundChannels[channel_idx].flags |= 0x1;

        if (soundChannels[channel_idx].sample_offset >= soundChannels[channel_idx].sample_size && !Channel_Loop_Enabled(soundChannels[channel_idx])) {
            Flush_SDL_Stream(soundChannels[channel_idx].buffer);
        }

        SDL_SetAudioStreamFrequencyRatio(soundChannels[channel_idx].buffer, 1.0f);
        Resume_SDL_Stream(soundChannels[channel_idx].buffer);

        return channel_idx;
    }

    /**
     * @brief Searches for an available sound channel.
     * 
     * A channel is considered free if its SDL audio stream is null,
     * or if no audio is queued and the internal flags indicate the channel
     * is not busy.
     * 
     * @return The index of the free channel (0-15), or -2 if no channel is available.
     */
    int8_t __cdecl Find_Free_Channel() {
        for (int8_t i = 0; i < SOUND_CHANNEL_COUNT; ++i) {
            s_sound_channel& channel = soundChannels[i];

            if (channel.buffer == nullptr) {
                return i;
            }

            if (SDL_GetAudioStreamQueued(channel.buffer) == 0) {
                if (!(channel.flags & 1)) {
                    return i;
                }
            }
        }

        return -2;
    }

    /**
     * Attempts to play a sound resource by ID. Checks if the resource is already
     * cached in soundIndices; otherwise, loads it from the LBX file.
     */
    int32_t __cdecl Play_Sound(int32_t resource_id) {
        Sound_State_Guard_ guard;

        int32_t channel_id = (int8_t)Find_Free_Channel();
        if (channel_id == -2) {
            return -2;
        }

        if (resource_id == 0 || soundOn == 0) {
            return -2;
        }

        // Search cache for existing sound data
        for (int i = 0; i < SOUND_INDEX_COUNT; ++i) {
            s_sound_index& index = soundIndices[i];
            if (index.resource_id == resource_id && index.num_active_channels != 0) {
                if (index.num_active_channels == -1) {
                    index.num_active_channels = 1;
                    index.active_channel_ids[0] = (uint8_t)channel_id;
                } else if (index.num_active_channels > 0 && index.num_active_channels < SOUND_CHANNEL_COUNT) {
                    index.active_channel_ids[index.num_active_channels] = (uint8_t)channel_id;
                    index.num_active_channels++;
                } else {
                    continue;
                }

                void* buffer = (uint8_t*)soundBuffer + index.buffer_offset;
                return Play_Sound_File(channel_id, buffer, index.buffer_size);
            }
        }

        // Cache miss: Load the selected LBX entry through the VFS.
        s_vfs_file* sound_file = vfs::fopen_lbx("sound.lbx", resource_id);
        if (sound_file == nullptr) {
            return -2;
        }

        int32_t length_i32 = vfs::fsize(sound_file);
        if (length_i32 <= 0) {
            vfs::fclose(sound_file);
            return -2;
        }

        uint32_t length = (uint32_t)length_i32;

        void* buffer = Allocate_Sound_Block(channel_id, resource_id, length);
        if (buffer == nullptr) {
            vfs::fclose(sound_file);
            return -2;
        }

        if (vfs::fread(buffer, 1, length, sound_file) != length) {
            vfs::fclose(sound_file);
            return -2;
        }

        vfs::fclose(sound_file);
        return Play_Sound_File(channel_id, buffer, length);
    }

    /**
     * @brief Checks the status of a sound channel and performs updates based on its current position.
     * Handles both looping and one-shot sounds, as well as cleanup when playback is finished.
     */
    void __cdecl Update_Sound_Channel_Status(int32_t channel_idx) {
        Sound_State_Guard_ guard;

        s_sound_channel& channel = soundChannels[channel_idx];

        if (channel.buffer == nullptr) {
            return;
        }

        int queued = SDL_GetAudioStreamQueued(channel.buffer);
        if (queued > 0) {
            return;
        }

        if (Restart_Channel_Loop(channel)) {
            Clear_SDL_Stream(channel.buffer);
            if (Queue_Channel_Data(channel, channel.sample_size)) {
                Flush_SDL_Stream(channel.buffer);
                Resume_SDL_Stream(channel.buffer);
            }
        } else {
            channel.flags ^= 0x1; // Deactivate active flag
            Remove_Sound_Instance(channel_idx);
            Destroy_SDL_Stream(channel.buffer);
        }
    }

    /**
     * Finds an unused sound-cache index entry.
     */
    int32_t __cdecl Get_Free_Sound_Index() {
        for (int32_t i = 1; i < SOUND_INDEX_COUNT; ++i) {
            if (soundIndices[i].next_id == -1 && soundIndices[i].prev_id == -1) {
                return i;
            }
        }
        return -1;
    }

    /**
     * Allocates a cached sound-data block from the shared sound buffer.
     */
    void* __cdecl Allocate_Sound_Block(int8_t channel_idx, int32_t resource_id, int32_t size) {
        if (size > (int32_t)soundBufferSize) {
            return nullptr;
        }

        Merge_Free_Blocks();

        for (int32_t attempt = 0; attempt < 3; ++attempt) {
            int32_t current_idx = 0;
            s_sound_index* current = &soundIndices[0];

            while (true) {
                bool can_allocate;
                if (attempt == 0) {
                    can_allocate = (current->num_active_channels == 0);
                } else {
                    can_allocate = (current->num_active_channels < 1);
                }

                if (can_allocate && (int32_t)current->buffer_size >= size) {
                    int32_t free_idx = Get_Free_Sound_Index();
                    if (free_idx >= 0) {
                        s_sound_index* new_block = &soundIndices[free_idx];

                        new_block->prev_id = current_idx;
                        new_block->next_id = current->next_id;
                        new_block->buffer_offset = current->buffer_offset + size;
                        new_block->buffer_size = current->buffer_size - size;
                        new_block->num_active_channels = 0;
                        new_block->active_channel_ids[0] = 0;
                        new_block->resource_id = 0;

                        current->next_id = free_idx;
                        current->active_channel_ids[0] = (uint8_t)channel_idx;
                        current->resource_id = resource_id;
                        current->buffer_size = size;
                        current->num_active_channels = 1;

                        return (uint8_t*)soundBuffer + current->buffer_offset;
                    }
                    return nullptr;
                }

                current_idx = current->next_id;
                if (current_idx < 0) {
                    break;
                }
                current = &soundIndices[current_idx];
            }

            if (attempt == 1) {
                Merge_Free_Sound_Blocks();
            }
        }

        return nullptr;
    }

    void __cdecl Merge_Free_Sound_Blocks() {
        int32_t current_idx = 1;

        while (current_idx != -1) {
            // If this is the last block in the linked list, we are done.
            if (soundIndices[current_idx].next_id == -1) {
                return;
            }

            int32_t next_idx = soundIndices[current_idx].next_id;

            // If both the current block and the next block are inactive (num_active_channels < 1), merge them.
            if (soundIndices[current_idx].num_active_channels < 1 &&
                soundIndices[next_idx].num_active_channels < 1) {
                // Link current block to the block after the next one.
                soundIndices[current_idx].next_id = soundIndices[next_idx].next_id;

                // Add next block's buffer size to the current block.
                soundIndices[current_idx].buffer_size += soundIndices[next_idx].buffer_size;

                // Explicitly reset status fields of the newly merged block.
                soundIndices[current_idx].num_active_channels = 0;
                soundIndices[current_idx].active_channel_ids[0] = 0;
                soundIndices[current_idx].resource_id = 0;

                // Completely clear the now-merged-away next block entry.
                memset(&soundIndices[next_idx], 0, sizeof(s_sound_index));
                soundIndices[next_idx].next_id = -1;
                soundIndices[next_idx].prev_id = -1;
            }

            // Move to the next block in the list.
            current_idx = soundIndices[current_idx].next_id;
        }
    }

    /**
     * @brief Iterates through the linked list of sound blocks and merges adjacent free blocks.
     * 
     * A block is considered free if its num_active_channels is 0.
     */
    void __cdecl Merge_Free_Blocks() {
        int32_t current_idx = 1;

        while (true) {
            // Check if there is a next block to potentially merge with
            if (soundIndices[current_idx].next_id == -1) {
                break;
            }

            int32_t next_idx = soundIndices[current_idx].next_id;

            // If both current block and next block are inactive, merge them
            if (soundIndices[current_idx].num_active_channels == 0 &&
                soundIndices[next_idx].num_active_channels == 0) {
                // Point current block to the block after next_idx
                soundIndices[current_idx].next_id = soundIndices[next_idx].next_id;

                // Add the size of the merged block to current
                soundIndices[current_idx].buffer_size += soundIndices[next_idx].buffer_size;

                // Reset status fields for the merged current block
                soundIndices[current_idx].num_active_channels = 0;
                soundIndices[current_idx].active_channel_ids[0] = 0;
                soundIndices[current_idx].resource_id = 0;

                // Clear the vacated sound index record
                memset(&soundIndices[next_idx], 0, sizeof(s_sound_index));
                soundIndices[next_idx].next_id = -1;
                soundIndices[next_idx].prev_id = -1;
            }

            // Move to the next block in the linked list
            current_idx = soundIndices[current_idx].next_id;

            // If we've reached the end of the list, exit
            if (current_idx == -1) {
                break;
            }
        }
    }

    void __cdecl Update_Stream_Status() {
        Sound_State_Guard_ guard;

        if (stream_data.ds_buffer == nullptr) {
            return;
        }

        if (SDL_GetAudioStreamQueued(stream_data.ds_buffer) == 0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "stream: ended file=%s entry=%d", currentStreamFilename, currentStreamId);
            stream_data.status ^= 1;
            audioPlaybackActive = 0;
        }
    }

    void __cdecl Stop_Stream_Loop() {
        if (stream_data.ds_buffer != nullptr) {
            Clear_SDL_Stream(stream_data.ds_buffer);
        }

        if (streamEntryFile != nullptr) {
            vfs::fclose(streamEntryFile);
            streamEntryFile = nullptr;
        }

        loopStream = 0;
        stream_data.data_remaining = 0;
        stream_data.bytes_written = 0;
        stream_data.last_lock_size = 0;
        stream_data.field20_0x3f = 0;
        stream_data.write_cursor = 0;
        stream_data.status = 0;
    }

    uint8_t __cdecl Is_Stream_Playing() {
        Sound_State_Guard_ guard;
        return audioPlaybackActive;
    }
} // namespace sound