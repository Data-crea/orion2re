#include "pch.h"

#include <cstring>
#include <string>

#include "game/config_refresh.h"
#include "game/vfs.h"

namespace TEST_CONFIG_REFRESH {
    int refresh_count;
    int group_refresh_count[config::CONFIG_GROUP_COUNT];
    bool observed_committed_state;
    uint8_t expected_scroll_interval;
    std::string expected_path;
    std::string expected_contents;

    void Reset_() {
        refresh_count = 0;
        memset(group_refresh_count, 0, sizeof(group_refresh_count));
        observed_committed_state = false;
        expected_path.clear();
        expected_contents.clear();
    }

    void Expect_(uint8_t scroll_interval, const char* path, const char* contents) {
        expected_scroll_interval = scroll_interval;
        expected_path = path;
        expected_contents = contents;
    }
} // namespace TEST_CONFIG_REFRESH

namespace CONFIG_REFRESH {
    void Request_Resource_Reload() {
    }

    void Refresh_After_Config_Group_Reload(config::t_config_group group) {
        ++TEST_CONFIG_REFRESH::group_refresh_count[group];
    }

    void Refresh_After_Config_Reload(const bool* reloaded_groups) {
        ++TEST_CONFIG_REFRESH::refresh_count;
        if (reloaded_groups != nullptr) {
            for (int group_index = 0; group_index < config::CONFIG_GROUP_COUNT;
                 ++group_index) {
                if (reloaded_groups[group_index]) {
                    ++TEST_CONFIG_REFRESH::group_refresh_count[group_index];
                }
            }
        }
        s_vfs_file* file = vfs::fopen(TEST_CONFIG_REFRESH::expected_path.c_str(), "rb");
        if (file == nullptr) {
            return;
        }
        const int32_t size = vfs::fsize(file);
        std::string contents(size >= 0 ? (size_t)size : 0, '\0');
        const bool read = contents.empty() ||
            vfs::fread(contents.data(), 1, contents.size(), file) == contents.size();
        const bool closed = vfs::fclose(file) == 0;
        TEST_CONFIG_REFRESH::observed_committed_state =
            COMBINIT::scroll_interval ==
                TEST_CONFIG_REFRESH::expected_scroll_interval &&
            read && closed && contents == TEST_CONFIG_REFRESH::expected_contents;
    }
} // namespace CONFIG_REFRESH