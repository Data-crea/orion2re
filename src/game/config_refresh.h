#pragma once

#include "config/generated/config_groups.h"

namespace CONFIG_REFRESH {
    void Resources_Loaded();
    void Resources_Unloaded();
    void Request_Resource_Reload();
    bool Resource_Reload_Pending();
    /** Services pending reloads only after the previous screen has stopped using resource pointers. */
    void Reload_Resources_Between_Screens();
    void Tech_Names_Loaded();
    void Reload_Tech_Names_If_Loaded();
    void Refresh_After_Config_Group_Reload(config::t_config_group group);
    void Refresh_After_Config_Reload(const bool* reloaded_groups = nullptr);
} // namespace CONFIG_REFRESH