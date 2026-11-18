#pragma once

#include <string>
#include <vector>

#include "config/config_lifecycle.h"
#include "config/mod_manifest.h"

namespace orion2re::lua {
    struct s_lua_registry;
    struct s_lua_save_snapshot;
} // namespace orion2re::lua

namespace config {

    struct s_network_config_candidate;
    struct s_prepared_config_snapshot;

    struct s_active_config {
        s_config_baseline baseline;
        std::string entry_path;
        std::string base_package;
        std::vector<std::string> package_order;
        std::vector<s_mod_requirement> required_mods;
    };

    bool Load_Config_File(const char* entry_path, const char* package_id, s_effective_config* effective, s_cfg_diagnostic* diagnostic);
    bool Load_Startup_Config(const char* entry_path, const char* package_id, s_effective_config* effective, bool* loaded, s_cfg_diagnostic* diagnostic, bool require_file = false);
    bool Commit_Active_Config_Writes(const s_config_write_set* writes,
        s_cfg_diagnostic* diagnostic);
    bool Record_Active_Native_Config_Write(t_variable_id variable_id);
    bool Restore_Active_Config(const s_active_config* snapshot,
        s_cfg_diagnostic* diagnostic);
    const s_active_config* Get_Active_Config();
    bool Restore_Launch_Config(s_cfg_diagnostic* diagnostic);
    void Shutdown_Config_Loader();
    bool Get_Prepared_Active_Config_Snapshot(
        const s_config_snapshot_descriptor* descriptor,
        const s_prepared_config_snapshot** snapshot,
        s_cfg_diagnostic* diagnostic);
    bool Get_Canonical_Active_Config_Identity(
        const std::vector<uint8_t>** identity,
        s_cfg_diagnostic* diagnostic);
    bool Commit_Config_Snapshot(const s_config_snapshot_descriptor* descriptor,
        const s_config_state* state,
        s_cfg_diagnostic* diagnostic);
    bool Reapply_Config_Snapshot(const std::vector<s_mod_requirement>& required_mods,
        const s_config_snapshot_descriptor* descriptor,
        const s_config_state* state,
        s_cfg_diagnostic* diagnostic,
        const orion2re::lua::s_lua_save_snapshot* lua_snapshot = nullptr);
    bool Stage_Network_Config_Snapshot(const void* bytes,
        uint32_t size,
        s_network_config_candidate** candidate,
        s_cfg_diagnostic* diagnostic);
    const orion2re::lua::s_lua_registry* Get_Network_Config_Candidate_Registry(
        const s_network_config_candidate* candidate);
    bool Finalize_Network_Config_Snapshot(
        s_network_config_candidate** candidate,
        const void* lua_snapshot,
        uint32_t lua_snapshot_size,
        std::string* diagnostic);
    bool Commit_Network_Config_Snapshot(
        s_network_config_candidate** candidate,
        s_cfg_diagnostic* diagnostic);
    void Discard_Network_Config_Snapshot(
        s_network_config_candidate** candidate);
    bool Reconstruct_Active_Lua_Runtimes(std::string* diagnostic);
    bool Can_Reload_Config_Group(const s_config_group_descriptor* descriptor,
        bool network_game);
    using t_config_group_merger = bool (*)(t_config_group group,
        const s_effective_config* candidate,
        s_effective_config* effective);
    using t_config_group_reload_policy = bool (*)(
        const s_config_group_descriptor* descriptor, bool network_game);
    bool Reload_Active_Config_Groups(bool network_game,
        s_cfg_diagnostic* diagnostic,
        t_config_group_merger merge_group = nullptr,
        t_config_group_reload_policy reload_policy = nullptr);
    bool Reload_All_Active_Config_Groups(s_cfg_diagnostic* diagnostic);

} // namespace config