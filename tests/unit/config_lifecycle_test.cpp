#include "pch.h"

#include <cstring>

#include "config/cfg_parser.h"
#include "config/config_lifecycle.h"
#include "doctest/doctest.h"

namespace {
    bool Parse_(const char* source, config::s_cfg_document* document,
                config::s_cfg_diagnostic* diagnostic) {
        return config::Parse_Cfg("lifecycle.cfg", source, std::strlen(source),
                                 document, diagnostic);
    }

    bool Apply_(const char* source, const char* package_id,
                config::s_effective_config* effective,
                config::s_cfg_diagnostic* diagnostic) {
        config::s_cfg_document document;
        return Parse_(source, &document, diagnostic) &&
               config::Apply_Config_Document(&document, package_id, effective, diagnostic);
    }

    bool States_Equal_(const config::s_config_state& left,
                       const config::s_config_state& right) {
        if (left.values.size() != right.values.size()) {
            return false;
        }
        for (size_t index = 0; index < left.values.size(); ++index) {
            if (!config::Config_Values_Equal(left.values[index], right.values[index])) {
                return false;
            }
        }
        return true;
    }

    config::t_variable_id Variable_Id_(const char* path) {
        config::t_variable_id id = config::CONFIG_NO_VARIABLE;
        config::Find_Variable(path, std::strlen(path), &id);
        return id;
    }
}

DOCTEST_TEST_SUITE("configuration lifecycle") {

DOCTEST_TEST_CASE("startup display settings preserve defaults and accept overrides") {
    const auto width = Variable_Id_("interface.window_width");
    const auto height = Variable_Id_("interface.window_height");
    const auto fullscreen = Variable_Id_("interface.fullscreen");
    DOCTEST_REQUIRE(width != config::CONFIG_NO_VARIABLE);
    DOCTEST_REQUIRE(height != config::CONFIG_NO_VARIABLE);
    DOCTEST_REQUIRE(fullscreen != config::CONFIG_NO_VARIABLE);
    config::s_effective_config effective;
    DOCTEST_REQUIRE(config::Initialize_Effective_Config(&effective));
    DOCTEST_CHECK(effective.state.values[width].integer_value == 640);
    DOCTEST_CHECK(effective.state.values[height].integer_value == 480);
    DOCTEST_CHECK_FALSE(effective.state.values[fullscreen].bool_value);

    config::s_cfg_diagnostic diagnostic;
    DOCTEST_REQUIRE(Apply_(
        "interface window_width = 1280; interface window_height = 960; "
        "interface fullscreen = true;",
        "base_game", &effective, &diagnostic));
    DOCTEST_CHECK(platform::g_window_width == 1280);
    DOCTEST_CHECK(platform::g_window_height == 960);
    DOCTEST_CHECK(platform::g_fullscreen == 1);

    const config::s_config_state prior = effective.state;
    for (const char* source : {
             "interface window_width = 0;",
             "interface window_height = -1;",
             "interface window_width = 16385;",
             "interface window_height = 16385;",
             "interface fullscreen = invalid;"}) {
        DOCTEST_CHECK_FALSE(Apply_(source, "base_game", &effective, &diagnostic));
        DOCTEST_CHECK(States_Equal_(effective.state, prior));
        DOCTEST_CHECK(platform::g_window_width == 1280);
        DOCTEST_CHECK(platform::g_window_height == 960);
        DOCTEST_CHECK(platform::g_fullscreen == 1);
    }

    DOCTEST_REQUIRE(Apply_(
        "interface window_width = 640; interface window_height = 480; "
        "interface fullscreen = false;",
        "base_game", &effective, &diagnostic));
    DOCTEST_CHECK(platform::g_window_width == 640);
    DOCTEST_CHECK(platform::g_window_height == 480);
    DOCTEST_CHECK(platform::g_fullscreen == 0);
}

DOCTEST_TEST_CASE("effective state initializes from defaults or current native values") {
    const config::t_variable_id scroll_interval =
        Variable_Id_("interface.scroll_interval");
    const config::t_variable_id no_outpost_prompt =
        Variable_Id_("interface.no_outpost_prompt");
    DOCTEST_REQUIRE(no_outpost_prompt != config::CONFIG_NO_VARIABLE);
    config::s_effective_config defaults;
    config::Initialize_Effective_Config(&defaults);
    DOCTEST_CHECK(defaults.state.values[scroll_interval].integer_value == 1);
    DOCTEST_CHECK_FALSE(defaults.state.values[no_outpost_prompt].bool_value);
    DOCTEST_CHECK(defaults.provenance.size() == config::variable_count);
    for (const config::s_value_provenance& provenance : defaults.provenance) {
        DOCTEST_CHECK_FALSE(provenance.has_override);
    }

    COMBINIT::scroll_interval = 34;
    REPORT::no_outpost_prompt = 1;
    config::s_effective_config native;
    config::Read_Effective_Native_Config(&native);
    DOCTEST_CHECK(native.state.values[scroll_interval].integer_value == 34);
    DOCTEST_CHECK(native.state.values[no_outpost_prompt].bool_value);
}

DOCTEST_TEST_CASE("complete documents commit state and per-value provenance atomically") {
    const config::t_variable_id scroll_interval =
        Variable_Id_("interface.scroll_interval");
    const config::t_variable_id no_outpost_prompt =
        Variable_Id_("interface.no_outpost_prompt");
    const config::t_variable_id warn_blockers =
        Variable_Id_("interface.warn_blockers");
    DOCTEST_REQUIRE(warn_blockers != config::CONFIG_NO_VARIABLE);
    COMBINIT::scroll_interval = 1;
    REPORT::no_outpost_prompt = 0;
    COLBLDG::warn_blockers = true;
    config::s_effective_config effective;
    config::Read_Effective_Native_Config(&effective);
    config::s_cfg_diagnostic diagnostic;

    DOCTEST_REQUIRE(Apply_(
        "interface no_outpost_prompt = true;\n"
        "interface scroll_interval = 75;\n"
        "interface warn_blockers = false;\n",
        "base_game", &effective, &diagnostic));

    DOCTEST_CHECK(effective.state.values[no_outpost_prompt].bool_value);
    DOCTEST_CHECK(effective.state.values[scroll_interval].integer_value == 75);
    DOCTEST_CHECK_FALSE(effective.state.values[warn_blockers].bool_value);
    DOCTEST_CHECK(REPORT::no_outpost_prompt == 1);
    DOCTEST_CHECK(COMBINIT::scroll_interval == 75);
    DOCTEST_CHECK_FALSE(COLBLDG::warn_blockers);

    config::t_variable_id scroll_index = 0;
    DOCTEST_REQUIRE(config::Find_Variable(
        "interface.scroll_interval", std::strlen("interface.scroll_interval"),
        &scroll_index));
    DOCTEST_CHECK(effective.provenance[scroll_index].has_override);
    DOCTEST_CHECK(effective.provenance[scroll_index].package_id == "base_game");
    DOCTEST_CHECK(effective.provenance[scroll_index].location.source == "lifecycle.cfg");
    DOCTEST_CHECK(effective.provenance[scroll_index].location.line == 2);
    DOCTEST_CHECK(effective.provenance[scroll_index].statement == 2);
    DOCTEST_REQUIRE(effective.prior_values[scroll_index].size() == 1);
    DOCTEST_CHECK(effective.prior_values[scroll_index][0].value.integer_value == 1);
    DOCTEST_CHECK_FALSE(effective.prior_values[scroll_index][0].provenance.has_override);

    DOCTEST_REQUIRE(Apply_("interface scroll_interval = 90;", "balance_mod",
                           &effective, &diagnostic));
    DOCTEST_CHECK(effective.state.values[scroll_index].integer_value == 90);
    DOCTEST_CHECK(effective.provenance[scroll_index].package_id == "balance_mod");
    DOCTEST_CHECK(effective.provenance[scroll_index].statement == 1);
    DOCTEST_REQUIRE(effective.prior_values[scroll_index].size() == 2);
    DOCTEST_CHECK(effective.prior_values[scroll_index][1].value.integer_value == 75);
    DOCTEST_CHECK(effective.prior_values[scroll_index][1].provenance.package_id == "base_game");
}

DOCTEST_TEST_CASE("tracked configuration writes set and clear non-default bits") {
    const config::t_variable_id scroll_interval =
        Variable_Id_("interface.scroll_interval");
    DOCTEST_REQUIRE(scroll_interval != config::CONFIG_NO_VARIABLE);
    config::s_effective_config effective;
    DOCTEST_REQUIRE(config::Initialize_Effective_Config(&effective));
    DOCTEST_CHECK(config::State_Tracks_Non_Default_Values(
        &effective.state));
    DOCTEST_CHECK_FALSE(config::State_Value_Is_Non_Default(
        &effective.state, scroll_interval));

    config::s_cfg_diagnostic diagnostic;
    DOCTEST_REQUIRE(Apply_("interface scroll_interval = 75;",
        "base_game",
        &effective,
        &diagnostic));
    DOCTEST_CHECK(config::State_Value_Is_Non_Default(
        &effective.state, scroll_interval));

    DOCTEST_REQUIRE(Apply_("interface scroll_interval = 1;",
        "base_game",
        &effective,
        &diagnostic));
    DOCTEST_CHECK_FALSE(config::State_Value_Is_Non_Default(
        &effective.state, scroll_interval));
}

DOCTEST_TEST_CASE("configuration stages track leaves and roll back to a frozen baseline") {
    config::s_effective_config defaults;
    DOCTEST_REQUIRE(config::Initialize_Effective_Config(&defaults));
    config::s_config_baseline baseline;
    DOCTEST_REQUIRE(config::Freeze_Config_Baseline(&defaults, &baseline));

    config::s_config_stage stage;
    DOCTEST_REQUIRE(config::Begin_Config_Stage(&baseline, &stage));
    config::s_cfg_document document;
    config::s_cfg_diagnostic diagnostic;
    const char source[] =
        "interface scroll_interval = 75;\n"
        "ai autobuild_rules candidates automated_factory weight = "
        "base: 17 formula: additive;\n";
    DOCTEST_REQUIRE(config::Parse_Cfg("stage.cfg",
        source,
        sizeof(source) - 1,
        &document,
        &diagnostic));
    DOCTEST_REQUIRE(config::Apply_Config_Document_To_Stage(
        &document, "test", &stage, &diagnostic));

    config::s_variable_accessor aggregate;
    DOCTEST_REQUIRE(config::Find_Variable_Accessor(
        "ai.autobuild_rules.candidates.7.weight.base",
        std::strlen("ai.autobuild_rules.candidates.7.weight.base"),
        &aggregate));
    const config::t_variable_id scalar =
        Variable_Id_("interface.scroll_interval");
    DOCTEST_CHECK(config::Config_Delta_Contains(
        &stage.touched, scalar, 0));
    DOCTEST_CHECK(config::Config_Delta_Contains(&stage.touched,
        aggregate.variable_id,
        aggregate.leaf_index));
    DOCTEST_CHECK(stage.effective.state.values[scalar].integer_value == 75);

    DOCTEST_REQUIRE(config::Rollback_Config_Stage(&stage));
    DOCTEST_CHECK(States_Equal_(stage.effective.state, baseline.effective.state));
    DOCTEST_CHECK(stage.effective.state.non_default_aggregates.empty());
    DOCTEST_CHECK(stage.effective.prior_values[aggregate.variable_id].empty());
    DOCTEST_CHECK_FALSE(stage.effective.provenance[aggregate.variable_id].has_override);
    DOCTEST_CHECK(stage.effective.state.values[scalar].integer_value ==
        baseline.effective.state.values[scalar].integer_value);
    DOCTEST_CHECK(stage.touched.variable_ids.empty());
    DOCTEST_CHECK_FALSE(config::Config_Delta_Contains(
        &stage.touched, scalar, 0));

    DOCTEST_REQUIRE(config::Apply_Config_Document_To_Stage(
        &document, "test", &stage, &diagnostic));
    config::s_config_baseline frozen;
    DOCTEST_REQUIRE(config::Freeze_Config_Stage(&stage, &frozen));
    DOCTEST_CHECK(frozen.effective.state.values[scalar].integer_value == 75);
    DOCTEST_CHECK(stage.baseline == nullptr);
    DOCTEST_CHECK(stage.touched.variable_ids.empty());
}

DOCTEST_TEST_CASE("document failure rolls back earlier writes provenance and native state") {
    COMBINIT::scroll_interval = 40;
    REPORT::no_outpost_prompt = 0;
    config::s_effective_config effective;
    config::Read_Effective_Native_Config(&effective);
    config::s_cfg_diagnostic diagnostic;
    DOCTEST_REQUIRE(Apply_("interface no_outpost_prompt = true;", "first",
                           &effective, &diagnostic));
    const config::s_config_state prior_state = effective.state;
    const auto prior_provenance = effective.provenance;
    const auto prior_values = effective.prior_values;

    DOCTEST_CHECK_FALSE(Apply_(
        "interface scroll_interval = 80;\n"
        "interface scroll_interval = 256;\n",
        "broken", &effective, &diagnostic));
    DOCTEST_CHECK(diagnostic.message ==
                  "invalid value '256' for 'interface.scroll_interval'; expected uint8 in range 0..255");
    DOCTEST_CHECK(States_Equal_(effective.state, prior_state));
    DOCTEST_CHECK(effective.provenance.size() == prior_provenance.size());
    DOCTEST_CHECK(effective.prior_values.size() == prior_values.size());
    for (size_t index = 0; index < prior_provenance.size(); ++index) {
        DOCTEST_CHECK(effective.provenance[index].has_override ==
                      prior_provenance[index].has_override);
        DOCTEST_CHECK(effective.provenance[index].package_id ==
                      prior_provenance[index].package_id);
        DOCTEST_CHECK(effective.prior_values[index].size() == prior_values[index].size());
    }
    DOCTEST_CHECK(REPORT::no_outpost_prompt == 1);
    DOCTEST_CHECK(COMBINIT::scroll_interval == 40);
}

DOCTEST_TEST_CASE("stop terminates the current document before later statements") {
    const config::t_variable_id scroll_interval =
        Variable_Id_("interface.scroll_interval");
    DOCTEST_REQUIRE(scroll_interval != config::CONFIG_NO_VARIABLE);
    config::s_effective_config effective;
    config::Initialize_Effective_Config(&effective);
    config::s_cfg_diagnostic diagnostic;

    DOCTEST_REQUIRE(Apply_(
        "interface scroll_interval = 70; stop; interface scroll_interval = 99;",
        "base_game", &effective, &diagnostic));
    DOCTEST_CHECK(effective.state.values[scroll_interval].integer_value == 70);
    DOCTEST_CHECK(COMBINIT::scroll_interval == 70);
}

DOCTEST_TEST_CASE("include remains a loader operation and deferred enable is ignored") {
    const config::t_variable_id scroll_interval =
        Variable_Id_("interface.scroll_interval");
    DOCTEST_REQUIRE(scroll_interval != config::CONFIG_NO_VARIABLE);
    config::s_effective_config effective;
    config::Initialize_Effective_Config(&effective);
    config::s_cfg_diagnostic diagnostic;

    DOCTEST_CHECK_FALSE(Apply_(
        "interface scroll_interval = 70; include extra.cfg;",
        "base_game", &effective, &diagnostic));
    DOCTEST_CHECK(diagnostic.message ==
                  "include execution requires the configuration loader");
    DOCTEST_CHECK(effective.state.values[scroll_interval].integer_value == 1);

    DOCTEST_REQUIRE(Apply_(
        "interface scroll_interval = 70; enable balance_mod;",
        "base_game", &effective, &diagnostic));
    DOCTEST_CHECK(effective.state.values[scroll_interval].integer_value == 70);
}

DOCTEST_TEST_CASE("group apply merges only selected native values and metadata") {
    const config::t_variable_id scroll_interval =
        Variable_Id_("interface.scroll_interval");
    const config::t_variable_id maximum_star_count =
        Variable_Id_("map.star_count.4");
    DOCTEST_REQUIRE(maximum_star_count != config::CONFIG_NO_VARIABLE);
    COMBINIT::scroll_interval = 1;
    MAPGEN::maximum_star_count = 99;

    config::s_effective_config effective;
    DOCTEST_REQUIRE(config::Read_Effective_Native_Config(&effective));
    config::s_effective_config candidate = effective;
    candidate.state.values[scroll_interval].integer_value = 12;
    candidate.state.values[maximum_star_count].integer_value = 100;
    candidate.provenance[scroll_interval].has_override = true;
    candidate.provenance[scroll_interval].package_id = "local";
    candidate.provenance[maximum_star_count].has_override = true;
    candidate.provenance[maximum_star_count].package_id = "gameplay";

    DOCTEST_REQUIRE(config::Apply_Config_Group(
        config::CONFIG_GROUP_INTERFACE, &candidate, &effective));
    DOCTEST_CHECK(COMBINIT::scroll_interval == 12);
    DOCTEST_CHECK(MAPGEN::maximum_star_count == 99);
    DOCTEST_CHECK(effective.state.values[scroll_interval].integer_value == 12);
    DOCTEST_CHECK(effective.provenance[scroll_interval].package_id == "local");
    DOCTEST_CHECK(effective.state.values[maximum_star_count].integer_value == 99);
    DOCTEST_CHECK_FALSE(effective.provenance[maximum_star_count].has_override);
}

}