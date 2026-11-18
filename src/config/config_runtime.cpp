#include "config_runtime.h"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cstring>
#include <string_view>
#include <utility>

#include "config/config_bits.h"
#include "config/config_decode.h"
#include "config/config_profile.h"
#include "game/arena.h"
#include "unaligned.h"

namespace config {
    namespace {
        arena::s_arena* active_string_arenas[CONFIG_GROUP_COUNT];
        s_config_state captured_defaults;
        bool defaults_captured;
        bool defaults_validated;
        s_config_state initial_native_state;
        s_config_state published_state;
        std::vector<t_variable_id> arena_string_variables[CONFIG_GROUP_COUNT];

        constexpr size_t TRACKING_WORD_BITS = sizeof(uint64_t) * 8;

        void Set_Non_Default(s_config_state* state,
            t_variable_id variable_id,
            bool non_default) {
            const uint64_t mask = UINT64_C(1) << (variable_id % TRACKING_WORD_BITS);
            uint64_t& word = state->non_default_values[variable_id / TRACKING_WORD_BITS];
            if (non_default) {
                if ((word & mask) != 0) return;
                word |= mask;
                state->non_default_variable_positions[variable_id] =
                    static_cast<uint32_t>(state->non_default_variable_ids.size());
                state->non_default_variable_ids.push_back(variable_id);
            } else {
                if ((word & mask) == 0) return;
                word &= ~mask;
                const uint32_t position =
                    state->non_default_variable_positions[variable_id];
                const t_variable_id moved = state->non_default_variable_ids.back();
                state->non_default_variable_ids[position] = moved;
                state->non_default_variable_positions[moved] = position;
                state->non_default_variable_ids.pop_back();
                state->non_default_variable_positions[variable_id] = UINT32_MAX;
            }
        }

        s_non_default_aggregate* Get_Non_Default_Aggregate(
            s_config_state* state,
            t_variable_id variable_id) {
            if (state->non_default_aggregate_positions.size() != variable_count) {
                return nullptr;
            }
            const uint32_t position =
                state->non_default_aggregate_positions.Read(variable_id);
            return position < state->non_default_aggregates.size()
                ? &state->non_default_aggregates[position]
                : nullptr;
        }

        s_non_default_aggregate* Ensure_Non_Default_Aggregate(
            s_config_state* state,
            t_variable_id variable_id) {
            s_non_default_aggregate* aggregate =
                Get_Non_Default_Aggregate(state, variable_id);
            if (aggregate != nullptr) return aggregate;
            state->non_default_aggregate_positions[variable_id] =
                static_cast<uint32_t>(state->non_default_aggregates.size());
            state->non_default_aggregates.emplace_back();
            aggregate = &state->non_default_aggregates.back();
            aggregate->variable_id = variable_id;
            return aggregate;
        }

        void Remove_Non_Default_Aggregate(s_config_state* state,
            t_variable_id variable_id) {
            const uint32_t position =
                state->non_default_aggregate_positions.Read(variable_id);
            if (position >= state->non_default_aggregates.size()) return;
            const size_t last = state->non_default_aggregates.size() - 1;
            if (position != last) {
                state->non_default_aggregates[position] =
                    std::move(state->non_default_aggregates[last]);
                state->non_default_aggregate_positions[state->non_default_aggregates[position].variable_id] = position;
            }
            state->non_default_aggregates.pop_back();
            state->non_default_aggregate_positions[variable_id] = UINT32_MAX;
        }

        uint32_t Aggregate_Non_Default_Leaf_Count(
            const s_typed_value& value,
            const s_typed_value& default_value,
            std::vector<uint64_t>* leaves) {
            const size_t common_size = std::min(value.aggregate_values.size(),
                default_value.aggregate_values.size());
            const size_t maximum_size = std::max(value.aggregate_values.size(),
                default_value.aggregate_values.size());
            leaves->assign((maximum_size + TRACKING_WORD_BITS - 1) /
                    TRACKING_WORD_BITS,
                0);
            size_t count = value.aggregate_values.size() > common_size
                ? value.aggregate_values.size() - common_size
                : default_value.aggregate_values.size() - common_size;
            const auto compare = [&](size_t index, const s_typed_value& leaf) {
                if (index < common_size && !Config_Values_Equal(leaf,
                        default_value.aggregate_values[index])) {
                    (*leaves)[index / TRACKING_WORD_BITS] |=
                        UINT64_C(1) << (index % TRACKING_WORD_BITS);
                    ++count;
                }
                return true;
            };
            if (value.aggregate_values.Shares_Baseline(default_value.aggregate_values) &&
                default_value.aggregate_values.Stored_Value_Count() == 0) {
                value.aggregate_values.Visit_Stored(compare);
            } else {
                for (size_t index = 0; index < common_size; ++index) {
                    compare(index, value.aggregate_values[index]);
                }
            }
            for (size_t index = common_size; index < maximum_size; ++index) {
                (*leaves)[index / TRACKING_WORD_BITS] |=
                    UINT64_C(1) << (index % TRACKING_WORD_BITS);
            }
            return count > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(count);
        }

        std::vector<t_state_validator>& State_Validators() {
            static std::vector<t_state_validator> validators;
            return validators;
        }

        std::vector<t_commit_listener>& Commit_Listeners() {
            static std::vector<t_commit_listener> listeners;
            return listeners;
        }

        std::vector<uint32_t> Build_Root_Nodes() {
            std::vector<uint32_t> nodes;
            for (uint32_t index = 0; index < schema_node_count; ++index) {
                if (schema_nodes[index].parent == CONFIG_NO_NODE) {
                    nodes.push_back(index);
                }
            }
            return nodes;
        }

        const std::vector<uint32_t>& Root_Nodes() {
            static const std::vector<uint32_t> roots = Build_Root_Nodes();
            return roots;
        }

        std::vector<std::vector<t_variable_id>> Build_Group_Variables() {
            std::vector<std::vector<t_variable_id>> groups(CONFIG_GROUP_COUNT);
            for (t_variable_id variable_id = 0;
                 variable_id < variable_count;
                 ++variable_id) {
                const t_config_group group = Get_Variable_Group(variable_id);
                if (group >= 0 && group < CONFIG_GROUP_COUNT) {
                    groups[group].push_back(variable_id);
                }
            }
            return groups;
        }

        const std::vector<std::vector<t_variable_id>>& Group_Variables() {
            static const std::vector<std::vector<t_variable_id>> groups =
                Build_Group_Variables();
            return groups;
        }

        uint32_t Find_Root_Node(t_variable_id variable_id) {
            for (uint32_t index : Root_Nodes()) {
                const s_schema_node_descriptor& node = schema_nodes[index];
                if (variable_id >= node.first_variable &&
                    variable_id <= node.last_variable) {
                    return index;
                }
            }
            return CONFIG_NO_NODE;
        }

        uint32_t Find_Root_Node(std::string_view id) {
            for (uint32_t index : Root_Nodes()) {
                if (id == schema_nodes[index].id) return index;
            }
            return CONFIG_NO_NODE;
        }

        // Config targets may live inside packed structures, so copy values
        // bytewise rather than through typed pointers.
        bool Read_Integer(const void* address, e_value_type type, int64_t* value) {
            switch (type) {
                case CONFIG_VALUE_BOOL:
                case CONFIG_VALUE_UINT8: {
                    *value = unaligned::Load<uint8_t>(address);
                    return true;
                }
                case CONFIG_VALUE_INT8: {
                    *value = unaligned::Load<int8_t>(address);
                    return true;
                }
                case CONFIG_VALUE_INT16: {
                    *value = unaligned::Load<int16_t>(address);
                    return true;
                }
                case CONFIG_VALUE_UINT16: {
                    *value = unaligned::Load<uint16_t>(address);
                    return true;
                }
                case CONFIG_VALUE_INT32: {
                    *value = unaligned::Load<int32_t>(address);
                    return true;
                }
                case CONFIG_VALUE_UINT32: {
                    *value = unaligned::Load<uint32_t>(address);
                    return true;
                }
                default: return false;
            }
        }

        bool Write_Integer(void* address, size_t size, e_value_type type, int64_t value) {
            switch (type) {
                case CONFIG_VALUE_BOOL: {
                    if (size != sizeof(uint8_t)) return false;
                    unaligned::Store<uint8_t>(address, value != 0);
                    return true;
                }
                case CONFIG_VALUE_INT8: {
                    if (size != sizeof(int8_t)) return false;
                    unaligned::Store<int8_t>(address, (int8_t)value);
                    return true;
                }
                case CONFIG_VALUE_UINT8: {
                    if (size != sizeof(uint8_t)) return false;
                    unaligned::Store<uint8_t>(address, (uint8_t)value);
                    return true;
                }
                case CONFIG_VALUE_INT16: {
                    if (size != sizeof(int16_t)) return false;
                    unaligned::Store<int16_t>(address, (int16_t)value);
                    return true;
                }
                case CONFIG_VALUE_UINT16: {
                    if (size != sizeof(uint16_t)) return false;
                    unaligned::Store<uint16_t>(address, (uint16_t)value);
                    return true;
                }
                case CONFIG_VALUE_INT32: {
                    if (size != sizeof(int32_t)) return false;
                    unaligned::Store<int32_t>(address, (int32_t)value);
                    return true;
                }
                case CONFIG_VALUE_UINT32: {
                    if (size != sizeof(uint32_t)) return false;
                    unaligned::Store<uint32_t>(address, (uint32_t)value);
                    return true;
                }
                default: return false;
            }
        }

        bool Fail(const s_config_write* write, const char* message, s_cfg_diagnostic* diagnostic) {
            diagnostic->location = write->provenance;
            diagnostic->message = message;
            return false;
        }

        bool Tracked_Values_Equal(const s_config_state* left,
            const s_config_state* right,
            t_variable_id variable_id) {
            const s_typed_value& left_value = left->values[variable_id];
            const s_typed_value& right_value = right->values[variable_id];
            if (left_value.type != CONFIG_VALUE_AGGREGATE ||
                right_value.type != CONFIG_VALUE_AGGREGATE) {
                return Config_Values_Equal(left_value, right_value);
            }
            if (left_value.aggregate_values.size() !=
                right_value.aggregate_values.size()) {
                return false;
            }
            const s_non_default_aggregate* left_aggregate =
                Get_State_Non_Default_Aggregate(left, variable_id);
            const s_non_default_aggregate* right_aggregate =
                Get_State_Non_Default_Aggregate(right, variable_id);
            static const std::vector<uint64_t> empty;
            const std::vector<uint64_t>& left_leaves = left_aggregate != nullptr
                ? left_aggregate->leaves
                : empty;
            const std::vector<uint64_t>& right_leaves = right_aggregate != nullptr
                ? right_aggregate->leaves
                : empty;
            const size_t word_count = std::max(
                left_leaves.size(), right_leaves.size());
            for (size_t word_index = 0;
                 word_index < word_count;
                 ++word_index) {
                uint64_t word = word_index < left_leaves.size()
                    ? left_leaves[word_index]
                    : 0;
                if (word_index < right_leaves.size()) {
                    word |= right_leaves[word_index];
                }
                while (word != 0) {
                    const unsigned int bit = Count_Trailing_Zero_Bits(word);
                    const size_t leaf = word_index * TRACKING_WORD_BITS + bit;
                    if (leaf >= left_value.aggregate_values.size() ||
                        !Config_Values_Equal(
                            left_value.aggregate_values[leaf],
                            right_value.aggregate_values[leaf])) {
                        return false;
                    }
                    word &= word - 1;
                }
            }
            return true;
        }

        bool Commit_Changed_Value(t_variable_id variable_id,
            const s_config_state* prior,
            const s_config_state* current,
            arena::s_arena* string_arena) {
            const s_typed_value& prior_value = prior->values[variable_id];
            const s_typed_value& current_value = current->values[variable_id];
            if (current_value.type != CONFIG_VALUE_AGGREGATE) {
                generated::Set_Value(variable_id, &current_value, string_arena);
                return true;
            }
            const s_runtime_variable_descriptor& descriptor =
                runtime_variables[variable_id];
            if (descriptor.vector && descriptor.element_leaf_count != 0 &&
                prior_value.aggregate_values.size() !=
                    current_value.aggregate_values.size() &&
                !generated::Resize_Vector(variable_id,
                    current_value.aggregate_values.size() /
                        descriptor.element_leaf_count)) {
                return false;
            }
            const s_non_default_aggregate* prior_aggregate =
                Get_State_Non_Default_Aggregate(prior, variable_id);
            const s_non_default_aggregate* current_aggregate =
                Get_State_Non_Default_Aggregate(current, variable_id);
            static const std::vector<uint64_t> empty;
            const std::vector<uint64_t>& prior_leaves = prior_aggregate != nullptr
                ? prior_aggregate->leaves
                : empty;
            const std::vector<uint64_t>& current_leaves = current_aggregate != nullptr
                ? current_aggregate->leaves
                : empty;
            const size_t word_count = std::max(
                prior_leaves.size(), current_leaves.size());
            for (size_t word_index = 0;
                 word_index < word_count;
                 ++word_index) {
                uint64_t word = word_index < prior_leaves.size()
                    ? prior_leaves[word_index]
                    : 0;
                if (word_index < current_leaves.size()) {
                    word |= current_leaves[word_index];
                }
                while (word != 0) {
                    const unsigned int bit = Count_Trailing_Zero_Bits(word);
                    const size_t leaf = word_index * TRACKING_WORD_BITS + bit;
                    if (leaf < current_value.aggregate_values.size() &&
                        (leaf >= prior_value.aggregate_values.size() ||
                            !Config_Values_Equal(
                                prior_value.aggregate_values[leaf],
                                current_value.aggregate_values[leaf])) &&
                        !generated::Set_Leaf_Value(variable_id,
                            static_cast<uint32_t>(leaf),
                            &current_value.aggregate_values[leaf],
                            string_arena)) {
                        return false;
                    }
                    word &= word - 1;
                }
            }
            return true;
        }

        void Primitive_Range(e_value_type type, int64_t* minimum, int64_t* maximum) {
            switch (type) {
                case CONFIG_VALUE_INT8:
                    *minimum = INT8_MIN;
                    *maximum = INT8_MAX;
                    break;
                case CONFIG_VALUE_UINT8:
                    *minimum = 0;
                    *maximum = UINT8_MAX;
                    break;
                case CONFIG_VALUE_INT16:
                    *minimum = INT16_MIN;
                    *maximum = INT16_MAX;
                    break;
                case CONFIG_VALUE_UINT16:
                    *minimum = 0;
                    *maximum = UINT16_MAX;
                    break;
                case CONFIG_VALUE_INT32:
                    *minimum = INT32_MIN;
                    *maximum = INT32_MAX;
                    break;
                case CONFIG_VALUE_UINT32:
                    *minimum = 0;
                    *maximum = UINT32_MAX;
                    break;
                default:
                    *minimum = 0;
                    *maximum = 0;
                    break;
            }
        }

        bool Validate_Write(const s_config_write* write, s_cfg_diagnostic* diagnostic,
            const s_typed_value* input = nullptr) {
            const s_typed_value& value = input != nullptr ? *input : write->value;
            const s_type_descriptor* type = Get_Variable_Leaf_Type(
                write->variable_id, write->leaf_index);
            if (type == nullptr) {
                return Fail(write, "unknown typed write variable", diagnostic);
            }
            if (value.type != type->value_type) {
                diagnostic->location = write->provenance;
                std::string name;
                name = write->canonical_path;
                diagnostic->message = "typed write mismatch for " + name;
                return false;
            }
            if (type->value_type == CONFIG_VALUE_BOOL) {
                return true;
            }
            if (type->value_type == CONFIG_VALUE_STRING) {
                const size_t length = value.string_value.size();
                if ((type->has_min_length && length < type->min_length) ||
                    (type->has_max_length && length > type->max_length)) {
                    return Fail(write, "typed write string length error", diagnostic);
                }
                return true;
            }
            if (type->value_type == CONFIG_VALUE_BITMAP) {
                const size_t byte_count = (type->bitmap_bit_count + 7) / 8;
                if (value.bitmap_value.size() != byte_count) {
                    return Fail(write, "typed write bitmap size error", diagnostic);
                }
                for (size_t index = 0; index < byte_count; ++index) {
                    if ((value.bitmap_value[index] &
                            static_cast<uint8_t>(~type->bitmap_known_bits[index])) != 0) {
                        return Fail(write, "typed write unknown bitmap bits", diagnostic);
                    }
                }
                if ((type->bitmap_bit_count & 7) != 0 && !value.bitmap_value.empty()) {
                    const uint8_t final_mask = static_cast<uint8_t>(
                        (1U << (type->bitmap_bit_count & 7)) - 1U);
                    if ((value.bitmap_value.back() & static_cast<uint8_t>(~final_mask)) != 0) {
                        return Fail(write, "typed write bitmap padding bits", diagnostic);
                    }
                }
                return true;
            }
            if (type->value_type == CONFIG_VALUE_RATIO) {
                int64_t minimum = 0;
                int64_t maximum = 0;
                Primitive_Range(type->ratio_component_type, &minimum, &maximum);
                if (value.ratio_denominator <= 0 ||
                    value.ratio_numerator < minimum ||
                    value.ratio_numerator > maximum ||
                    value.ratio_denominator > maximum) {
                    return Fail(write, "typed write ratio component error", diagnostic);
                }
                return true;
            }

            int64_t primitive_minimum = 0;
            int64_t primitive_maximum = 0;
            Primitive_Range(type->value_type, &primitive_minimum, &primitive_maximum);
            const int64_t minimum = type->has_min ? type->min : primitive_minimum;
            const int64_t maximum = type->has_max ? type->max : primitive_maximum;
            if (value.integer_value < minimum || value.integer_value > maximum) {
                diagnostic->location = write->provenance;
                std::string name;
                name = write->canonical_path;
                diagnostic->message = "typed write range error for " + name;
                return false;
            }
            if (type->scalar_kind == CONFIG_SCALAR_ENUM &&
                type->reject_unknown_numeric) {
                bool found = false;
                for (size_t index = 0; index < type->value_count; ++index) {
                    found = found || type->values[index].value == value.integer_value;
                }
                if (!found) {
                    return Fail(write, "typed write unknown enum value", diagnostic);
                }
            }
            if (type->scalar_kind == CONFIG_SCALAR_MASK &&
                type->reject_unknown_numeric &&
                (static_cast<uint64_t>(value.integer_value) &
                    ~type->known_bits) != 0) {
                return Fail(write, "typed write unknown mask bits", diagnostic);
            }
            return true;
        }

        bool Validate_State_Value(t_variable_id variable_id,
            uint32_t leaf_index,
            const s_typed_value& value) {
            thread_local s_config_write write;
            write.variable_id = variable_id;
            write.leaf_index = leaf_index;
            thread_local s_cfg_diagnostic diagnostic;
            return Validate_Write(&write, &diagnostic, &value);
        }

        bool Validate_Node_Terminators(uint32_t node_index,
            t_variable_id variable_offset,
            const s_config_state* state);

        bool Node_Has_Constraints(uint32_t index) {
            static const std::vector<bool> constrained = [] {
                std::vector<bool> result(schema_node_count, false);
                for (uint32_t node = 0; node < schema_node_count; ++node) {
                    if (!schema_nodes[node].has_terminator && !schema_nodes[node].unique_values) continue;
                    for (uint32_t parent = node; parent != CONFIG_NO_NODE; parent = schema_nodes[parent].parent) {
                        result[parent] = true;
                    }
                }
                return result;
            }();
            return constrained[index];
        }

        bool Validate_Aggregate_Node_Terminators(uint32_t node_index,
            t_variable_id variable_id,
            uint32_t leaf_offset,
            const s_config_state* state) {
            if (!Node_Has_Constraints(node_index)) return true;
            const s_schema_node_descriptor& node = schema_nodes[node_index];
            const s_typed_value& aggregate = state->values[variable_id];
            if (node.has_terminator || node.unique_values) {
                bool found_terminator = false;
                bool reached_terminator = false;
                bool found_value = false;
                for (uint32_t leaf = node.first_leaf;
                     leaf <= node.last_leaf;
                     ++leaf) {
                    const uint32_t aggregate_leaf = leaf_offset + leaf;
                    if (aggregate_leaf >= aggregate.aggregate_values.size()) return false;
                    const int64_t value =
                        aggregate.aggregate_values[aggregate_leaf].integer_value;
                    if (node.has_terminator && value == node.terminator) {
                        found_terminator = true;
                        reached_terminator = true;
                        continue;
                    }
                    if (node.implicit_terminator && reached_terminator) return false;
                    found_value = true;
                    if (node.unique_values) {
                        for (uint32_t previous = node.first_leaf;
                             previous < leaf;
                             ++previous) {
                            const int64_t previous_value = aggregate.aggregate_values[leaf_offset + previous].integer_value;
                            if (node.has_terminator &&
                                previous_value == node.terminator) break;
                            if (previous_value == value) return false;
                        }
                    }
                }
                if ((node.has_terminator && !found_terminator) ||
                    (node.implicit_terminator && !found_value)) return false;
            }
            if ((node.kind == CONFIG_NODE_ARRAY || node.kind == CONFIG_NODE_MAP) &&
                node.array_element_variable_count != 0) {
                for (uint32_t element = 0;
                     element < node.array_element_count;
                     ++element) {
                    if (!Validate_Aggregate_Node_Terminators(node.first_child,
                            variable_id,
                            leaf_offset + element * node.array_element_variable_count,
                            state)) return false;
                }
                return true;
            }
            for (uint32_t child = node.first_child; child != CONFIG_NO_NODE;
                 child = schema_nodes[child].next_sibling) {
                if (!Validate_Aggregate_Node_Terminators(
                        child, variable_id, leaf_offset, state)) return false;
            }
            return true;
        }

        bool Validate_State(const s_config_state* state, bool baseline) {
            if (state == nullptr || state->values.size() != variable_count) {
                return false;
            }
            const bool sparse = defaults_validated &&
                state->values.Shares_Baseline(captured_defaults.values);
            const auto validate = [&](size_t index, const s_typed_value& value) {
                const t_variable_id variable_id = static_cast<t_variable_id>(index);
                if (value.type == CONFIG_VALUE_AGGREGATE) {
                    const s_runtime_variable_descriptor& descriptor =
                        runtime_variables[variable_id];
                    if (!descriptor.aggregate || descriptor.element_leaf_count == 0 ||
                        value.aggregate_values.size() %
                                descriptor.element_leaf_count !=
                            0 ||
                        (!descriptor.vector && value.aggregate_values.size() != descriptor.element_leaf_count) ||
                        (descriptor.vector && value.aggregate_values.size() < static_cast<size_t>(descriptor.default_element_count) * descriptor.element_leaf_count)) {
                        return false;
                    }
                    const auto validate_leaf = [&](size_t leaf, const s_typed_value& entry) {
                        return Validate_State_Value(variable_id, static_cast<uint32_t>(leaf), entry);
                    };
                    if (sparse && value.aggregate_values.Shares_Baseline(
                            captured_defaults.values.Read(variable_id).aggregate_values)) {
                        if (!value.aggregate_values.Visit_Stored(validate_leaf)) return false;
                        const size_t baseline_size = captured_defaults.values.Read(variable_id).aggregate_values.size();
                        for (size_t leaf = baseline_size; leaf < value.aggregate_values.size(); ++leaf) {
                            if (!validate_leaf(leaf, value.aggregate_values[leaf])) return false;
                        }
                    } else {
                        for (uint32_t leaf = 0; leaf < value.aggregate_values.size(); ++leaf) {
                            if (!validate_leaf(leaf, value.aggregate_values[leaf])) return false;
                        }
                    }
                    const size_t element_count = descriptor.vector ? value.aggregate_values.size() / descriptor.element_leaf_count : 1;
                    for (size_t element = 0; element < element_count; ++element) {
                        if (!Validate_Aggregate_Node_Terminators(descriptor.node,
                                variable_id,
                                static_cast<uint32_t>(element) *
                                    descriptor.element_leaf_count,
                                state)) return false;
                    }
                } else if (runtime_variables[variable_id].aggregate ||
                    !Validate_State_Value(variable_id, 0, value)) {
                    return false;
                }
                return true;
            };
            if (sparse) {
                if (!state->values.Visit_Stored(validate)) return false;
            } else {
                for (t_variable_id id = 0; id < variable_count; ++id) {
                    if (!validate(id, state->values[id])) return false;
                }
            }
            static const std::vector<uint32_t> constrained_roots = [] {
                std::vector<uint32_t> roots;
                for (uint32_t index = 0; index < schema_node_count; ++index) {
                    if (schema_nodes[index].parent == CONFIG_NO_NODE && Node_Has_Constraints(index)) roots.push_back(index);
                }
                return roots;
            }();
            for (uint32_t node_index : constrained_roots) {
                if (!Validate_Node_Terminators(node_index, 0, state)) {
                    return false;
                }
            }
            if (!baseline) {
                for (t_state_validator validator : State_Validators()) {
                    if (!validator(state)) return false;
                }
            }
            return true;
        }

        bool Group_Has_Arena_Strings(t_config_group group) {
            if (group < 0 || group >= CONFIG_GROUP_COUNT) return false;
            for (t_variable_id variable_id : Group_Variables()[group]) {
                if (runtime_variables[variable_id].has_arena_strings) return true;
            }
            return false;
        }

        bool Validate_Node_Terminators(uint32_t node_index, t_variable_id variable_offset, const s_config_state* state) {
            if (!Node_Has_Constraints(node_index)) return true;
            const s_schema_node_descriptor& node = schema_nodes[node_index];
            if ((node.kind == CONFIG_NODE_MAP || node.kind == CONFIG_NODE_VECTOR) &&
                node.first_variable <= node.last_variable) {
                for (t_variable_id variable_id = node.first_variable;
                     variable_id <= node.last_variable;
                     ++variable_id) {
                    if (!runtime_variables[variable_id].aggregate) return false;
                }
                return true;
            }
            if (node.has_terminator) {
                bool found = false;
                for (t_variable_id variable_id = node.first_variable + variable_offset;
                     variable_id <= node.last_variable + variable_offset;
                     ++variable_id) {
                    found = found || state->values[variable_id].integer_value == node.terminator;
                }
                if (!found) return false;
            }
            if (node.unique_values) {
                bool reached_terminator = false;
                bool found_value = false;
                for (t_variable_id variable_id = node.first_variable + variable_offset;
                     variable_id <= node.last_variable + variable_offset;
                     ++variable_id) {
                    const int64_t value = state->values[variable_id].integer_value;
                    if (node.has_terminator && value == node.terminator) {
                        reached_terminator = true;
                        continue;
                    }
                    if (node.implicit_terminator && reached_terminator) return false;
                    found_value = true;
                    for (t_variable_id previous = node.first_variable + variable_offset;
                         previous < variable_id;
                         ++previous) {
                        const int64_t previous_value = state->values[previous].integer_value;
                        if (node.has_terminator && previous_value == node.terminator) break;
                        if (previous_value == value) return false;
                    }
                }
                if (node.implicit_terminator && !found_value) return false;
            }
            if (node.kind == CONFIG_NODE_ARRAY && node.array_element_variable_count != 0) {
                for (uint32_t element = 0; element < node.array_element_count; ++element) {
                    if (!Validate_Node_Terminators(
                            node.first_child,
                            variable_offset + element * node.array_element_variable_count,
                            state)) {
                        return false;
                    }
                }
                return true;
            }
            for (uint32_t child = node.first_child; child != CONFIG_NO_NODE;
                 child = schema_nodes[child].next_sibling) {
                if (!Validate_Node_Terminators(child, variable_offset, state)) return false;
            }
            return true;
        }

        void Release_String_Arena(arena::s_arena* target) {
            if (target == nullptr) {
                return;
            }
            const arena::s_stats stats = arena::Get_Stats(target);
            if (stats.reference_count > 1) {
                SDL_LogError(
                    SDL_LOG_CATEGORY_APPLICATION,
                    "configuration: retired string arena retained by %u external references (%zu bytes in %zu chunks)",
                    stats.reference_count - 1,
                    stats.reserved_bytes,
                    stats.chunk_count);
            }
            arena::Release(target);
        }
    } // namespace

    const s_runtime_schema* Get_Runtime_Schema() {
        static const s_runtime_schema schema = {
            schema_nodes,
            schema_node_count,
            variable_count,
            runtime_variables,
            array_index_aliases,
            array_index_alias_count,
        };
        return &schema;
    }

    bool Get_Variable_Location(const s_runtime_schema* schema, t_variable_id variable_id, s_variable_location* location) {
        if (schema == nullptr || location == nullptr || variable_id >= schema->variable_count) {
            return false;
        }
        if (schema->variables != nullptr && schema->variables[variable_id].aggregate) {
            const uint32_t node = schema->variables[variable_id].node;
            if (node >= schema->node_count) return false;
            *location = {&schema->nodes[node], 0};
            return true;
        }
        uint32_t node_index = schema == Get_Runtime_Schema()
            ? Find_Root_Node(variable_id)
            : CONFIG_NO_NODE;
        if (schema != Get_Runtime_Schema()) {
            for (uint32_t index = 0; index < schema->node_count; ++index) {
                const s_schema_node_descriptor& node = schema->nodes[index];
                if (node.parent == CONFIG_NO_NODE &&
                    variable_id >= node.first_variable &&
                    variable_id <= node.last_variable) {
                    node_index = index;
                    break;
                }
            }
        }
        t_variable_id variable_offset = 0;
        while (node_index != CONFIG_NO_NODE) {
            const s_schema_node_descriptor& node = schema->nodes[node_index];
            if (node.kind == CONFIG_NODE_SCALAR) {
                if (node.variable_id + variable_offset != variable_id) return false;
                *location = {&node, variable_offset};
                return true;
            }
            if (node.kind == CONFIG_NODE_ARRAY && node.array_element_variable_count != 0) {
                const t_variable_id first = node.first_variable + variable_offset;
                if (variable_id < first || variable_id > node.last_variable + variable_offset) {
                    return false;
                }
                const t_variable_id element =
                    (variable_id - first) / node.array_element_variable_count;
                variable_offset += element * node.array_element_variable_count;
                node_index = node.first_child;
                continue;
            }
            uint32_t selected = CONFIG_NO_NODE;
            for (uint32_t child = node.first_child; child != CONFIG_NO_NODE;
                 child = schema->nodes[child].next_sibling) {
                const s_schema_node_descriptor& candidate = schema->nodes[child];
                if (variable_id >= candidate.first_variable + variable_offset &&
                    variable_id <= candidate.last_variable + variable_offset) {
                    selected = child;
                    break;
                }
            }
            node_index = selected;
        }
        return false;
    }

    const s_schema_node_descriptor* Get_Variable_Node(const s_runtime_schema* schema,
        t_variable_id variable_id) {
        s_variable_location location = {};
        return Get_Variable_Location(schema, variable_id, &location) ? location.node : nullptr;
    }

    bool Get_Array_Element_Offset(const s_runtime_schema* schema, t_variable_id variable_id, uint32_t leaf_index, uint32_t array_node, uint32_t* element_offset) {
        if (schema == nullptr || element_offset == nullptr || array_node >= schema->node_count) {
            return false;
        }
        const s_schema_node_descriptor& requested = schema->nodes[array_node];
        if (requested.kind == CONFIG_NODE_MAP) {
            if (variable_id < requested.first_variable ||
                variable_id > requested.last_variable) return false;
            *element_offset = variable_id - requested.first_variable;
            return true;
        }
        if (requested.kind == CONFIG_NODE_VECTOR) {
            if (variable_id != requested.first_variable ||
                requested.array_element_variable_count == 0) return false;
            *element_offset = leaf_index / requested.array_element_variable_count;
            return true;
        }
        if (schema->variables != nullptr &&
            schema->variables[variable_id].aggregate) {
            if (requested.array_element_variable_count == 0 ||
                requested.array_element_count == 0 ||
                leaf_index < requested.first_leaf) return false;
            *element_offset = (leaf_index - requested.first_leaf) /
                requested.array_element_variable_count % requested.array_element_count;
            return true;
        }
        uint32_t node_index = schema == Get_Runtime_Schema()
            ? Find_Root_Node(variable_id)
            : CONFIG_NO_NODE;
        if (schema != Get_Runtime_Schema()) {
            for (uint32_t index = 0; index < schema->node_count; ++index) {
                const s_schema_node_descriptor& node = schema->nodes[index];
                if (node.parent == CONFIG_NO_NODE &&
                    variable_id >= node.first_variable &&
                    variable_id <= node.last_variable) {
                    node_index = index;
                    break;
                }
            }
        }
        t_variable_id variable_offset = 0;
        while (node_index != CONFIG_NO_NODE) {
            const s_schema_node_descriptor& node = schema->nodes[node_index];
            if (node.kind == CONFIG_NODE_ARRAY) {
                const t_variable_id first = node.first_variable + variable_offset;
                const uint32_t stride = node.array_element_variable_count;
                if (stride == 0 || variable_id < first ||
                    variable_id > node.last_variable + variable_offset) return false;
                const uint32_t element = (variable_id - first) / stride;
                if (node_index == array_node) {
                    *element_offset = element;
                    return true;
                }
                variable_offset += element * stride;
                node_index = node.first_child;
                continue;
            }
            uint32_t selected = CONFIG_NO_NODE;
            for (uint32_t child = node.first_child; child != CONFIG_NO_NODE;
                 child = schema->nodes[child].next_sibling) {
                const s_schema_node_descriptor& candidate = schema->nodes[child];
                if (variable_id >= candidate.first_variable + variable_offset &&
                    variable_id <= candidate.last_variable + variable_offset) {
                    selected = child;
                    break;
                }
            }
            node_index = selected;
        }
        return false;
    }

    static const s_schema_node_descriptor* Get_Variable_Leaf_Node_Uncached(
        const s_runtime_schema* schema, t_variable_id variable_id, uint32_t leaf_index) {
        if (schema == nullptr || variable_id >= schema->variable_count) return nullptr;
        if (schema->variables == nullptr) {
            return leaf_index == 0 ? Get_Variable_Node(schema, variable_id) : nullptr;
        }
        const s_runtime_variable_descriptor& descriptor = schema->variables[variable_id];
        if (!descriptor.aggregate) {
            if (leaf_index != 0 || descriptor.node >= schema->node_count ||
                schema->nodes[descriptor.node].kind != CONFIG_NODE_SCALAR) return nullptr;
            return &schema->nodes[descriptor.node];
        }
        const uint32_t resolved_leaf = descriptor.vector && descriptor.element_leaf_count != 0 ? leaf_index % descriptor.element_leaf_count : leaf_index;
        uint32_t node_index = descriptor.node;
        uint32_t current_leaf = resolved_leaf;
        while (node_index < schema->node_count) {
            const s_schema_node_descriptor& node = schema->nodes[node_index];
            if (node.kind == CONFIG_NODE_SCALAR) {
                return current_leaf == node.leaf_index ? &node : nullptr;
            }
            if ((node.kind == CONFIG_NODE_ARRAY || node.kind == CONFIG_NODE_MAP ||
                    node.kind == CONFIG_NODE_VECTOR) &&
                node.array_element_variable_count != 0) {
                if (current_leaf < node.first_leaf || current_leaf > node.last_leaf) {
                    return nullptr;
                }
                const uint32_t element = (current_leaf - node.first_leaf) /
                    node.array_element_variable_count;
                current_leaf -= element * node.array_element_variable_count;
                node_index = node.first_child;
                continue;
            }
            uint32_t selected = CONFIG_NO_NODE;
            for (uint32_t child = node.first_child; child != CONFIG_NO_NODE;
                 child = schema->nodes[child].next_sibling) {
                const s_schema_node_descriptor& candidate = schema->nodes[child];
                if (current_leaf >= candidate.first_leaf &&
                    current_leaf <= candidate.last_leaf) {
                    selected = child;
                    break;
                }
            }
            if (selected == CONFIG_NO_NODE) return nullptr;
            node_index = selected;
        }
        return nullptr;
    }

    const s_schema_node_descriptor* Get_Variable_Leaf_Node(
        const s_runtime_schema* schema, t_variable_id variable_id, uint32_t leaf_index) {
        if (schema != Get_Runtime_Schema() || variable_id >= variable_count ||
            !runtime_variables[variable_id].aggregate) {
            return Get_Variable_Leaf_Node_Uncached(schema, variable_id, leaf_index);
        }
        const auto& descriptor = runtime_variables[variable_id];
        if (descriptor.node >= schema_node_count || descriptor.element_leaf_count == 0) return nullptr;
        const uint32_t leaf = descriptor.vector ? leaf_index % descriptor.element_leaf_count : leaf_index;
        if (leaf >= descriptor.element_leaf_count) return nullptr;
        static std::vector<std::vector<const s_schema_node_descriptor*>> layouts(schema_node_count);
        auto& layout = layouts[descriptor.node];
        if (layout.empty()) {
            layout.resize(descriptor.element_leaf_count);
            for (uint32_t index = 0; index < descriptor.element_leaf_count; ++index) {
                layout[index] = Get_Variable_Leaf_Node_Uncached(schema, variable_id, index);
            }
        }
        return layout[leaf];
    }

    const s_type_descriptor* Get_Variable_Type(t_variable_id variable_id) {
        if (variable_id >= variable_count) return nullptr;
        return Get_Variable_Leaf_Type(variable_id, 0);
    }

    const s_type_descriptor* Get_Variable_Leaf_Type(t_variable_id variable_id,
        uint32_t leaf_index) {
        const s_schema_node_descriptor* node = Get_Variable_Leaf_Node(
            Get_Runtime_Schema(), variable_id, leaf_index);
        return node != nullptr ? node->type : nullptr;
    }

    bool Find_Variable_Accessor(const char* name, size_t name_length, s_variable_accessor* accessor) {
        if (name == nullptr || accessor == nullptr) {
            return false;
        }
        const std::string_view requested(name, name_length);
        size_t offset = 0;
        uint32_t selected = CONFIG_NO_NODE;
        t_variable_id variable_offset = 0;
        t_variable_id aggregate_variable = CONFIG_NO_VARIABLE;
        uint32_t leaf_offset = 0;
        while (offset < requested.size()) {
            const size_t separator = requested.find('.', offset);
            const size_t end = separator == std::string_view::npos ? requested.size() : separator;
            const std::string_view component = requested.substr(offset, end - offset);
            if (component.empty() ||
                (separator != std::string_view::npos && separator + 1 == requested.size())) {
                return false;
            }
            uint32_t match = CONFIG_NO_NODE;
            if (selected == CONFIG_NO_NODE) {
                match = Find_Root_Node(component);
            } else {
                const s_schema_node_descriptor& parent = schema_nodes[selected];
                int32_t external_index = 0;
                if (parent.kind == CONFIG_NODE_ARRAY || parent.kind == CONFIG_NODE_MAP ||
                    parent.kind == CONFIG_NODE_VECTOR) {
                    const char* begin = component.data();
                    const char* component_end = begin + component.size();
                    const std::from_chars_result parsed =
                        std::from_chars(begin, component_end, external_index);
                    if (parsed.ec != std::errc() || parsed.ptr != component_end ||
                        std::to_string(external_index) != component) {
                        return false;
                    }
                    const int64_t index_offset = static_cast<int64_t>(external_index) -
                        parent.array_index_base;
                    if ((parent.kind == CONFIG_NODE_VECTOR ||
                            (index_offset >= 0 && index_offset < parent.array_element_count)) &&
                        parent.array_element_variable_count != 0) {
                        if (index_offset < 0) return false;
                        const uint64_t element_leaf_offset =
                            static_cast<uint64_t>(index_offset) *
                            parent.array_element_variable_count;
                        if (element_leaf_offset > UINT32_MAX ||
                            element_leaf_offset > UINT32_MAX - leaf_offset) return false;
                        if (parent.kind == CONFIG_NODE_MAP) {
                            aggregate_variable = parent.first_variable +
                                static_cast<t_variable_id>(index_offset);
                        } else if (parent.kind == CONFIG_NODE_VECTOR) {
                            aggregate_variable = parent.first_variable;
                            leaf_offset += static_cast<uint32_t>(element_leaf_offset);
                        } else if (aggregate_variable != CONFIG_NO_VARIABLE) {
                            leaf_offset += static_cast<uint32_t>(element_leaf_offset);
                        } else {
                            variable_offset += static_cast<t_variable_id>(index_offset) *
                                parent.array_element_variable_count;
                        }
                        match = parent.first_child;
                    }
                }
                if (match != CONFIG_NO_NODE) {
                    selected = match;
                    offset = end + 1;
                    continue;
                }
                for (uint32_t child = parent.first_child; child != CONFIG_NO_NODE;
                     child = schema_nodes[child].next_sibling) {
                    const s_schema_node_descriptor& node = schema_nodes[child];
                    if (((parent.kind == CONFIG_NODE_ARRAY || parent.kind == CONFIG_NODE_MAP ||
                             parent.kind == CONFIG_NODE_VECTOR) &&
                            node.external_index == external_index) ||
                        ((parent.kind != CONFIG_NODE_ARRAY && parent.kind != CONFIG_NODE_MAP &&
                             parent.kind != CONFIG_NODE_VECTOR) &&
                            component == node.id)) {
                        match = child;
                        break;
                    }
                }
            }
            if (match == CONFIG_NO_NODE) {
                return false;
            }
            selected = match;
            offset = end + 1;
        }
        if (selected == CONFIG_NO_NODE ||
            (schema_nodes[selected].kind != CONFIG_NODE_SCALAR &&
                aggregate_variable == CONFIG_NO_VARIABLE &&
                schema_nodes[selected].kind != CONFIG_NODE_VECTOR)) {
            return false;
        }
        accessor->variable_id = aggregate_variable != CONFIG_NO_VARIABLE ? aggregate_variable : schema_nodes[selected].kind == CONFIG_NODE_VECTOR ? schema_nodes[selected].first_variable
                                                                                                                                                  : schema_nodes[selected].variable_id + variable_offset;
        accessor->leaf_index = aggregate_variable != CONFIG_NO_VARIABLE ? leaf_offset + schema_nodes[selected].leaf_index : 0;
        return true;
    }

    const s_config_state* Get_Default_State() {
        if (!defaults_captured) {
            s_config_state defaults;
            if (!Initialize_Defaults(&defaults)) return nullptr;
        }
        return &captured_defaults;
    }

    bool Snapshot_Contains_Variable(const s_config_snapshot_descriptor* descriptor, t_variable_id id) {
        if (descriptor == nullptr || id >= variable_count || descriptor->variables == nullptr) return false;
        if ((descriptor->group_mask & (UINT32_C(1) << Get_Variable_Group(id))) == 0) return false;
        if (descriptor == &saved_config_snapshot || descriptor == &synchronized_config_snapshot) return true;
        return std::find(descriptor->variables, descriptor->variables + descriptor->variable_count, id) != descriptor->variables + descriptor->variable_count;
    }

    bool Read_State_Leaf_Value(const s_config_state* state, t_variable_id variable_id, uint32_t leaf_index, s_typed_value* value) {
        if (state == nullptr || value == nullptr || variable_id >= state->values.size()) {
            return false;
        }
        const s_typed_value& variable = state->values[variable_id];
        if (variable.type != CONFIG_VALUE_AGGREGATE) {
            if (leaf_index != 0) return false;
            *value = variable;
            return true;
        }
        if (leaf_index >= variable.aggregate_values.size()) return false;
        *value = variable.aggregate_values[leaf_index];
        return true;
    }

    bool Find_Variable(const char* name, size_t name_length, t_variable_id* variable_id) {
        s_variable_accessor accessor;
        if (variable_id == nullptr ||
            !Find_Variable_Accessor(name, name_length, &accessor)) return false;
        *variable_id = accessor.variable_id;
        return true;
    }

    bool Track_State_Non_Default_Values(s_config_state* state) {
        if (state == nullptr || state->values.size() != variable_count) {
            return false;
        }
        s_config_state defaults;
        if (!Initialize_Defaults(&defaults)) return false;
        state->non_default_values.assign(
            (variable_count + TRACKING_WORD_BITS - 1) / TRACKING_WORD_BITS, 0);
        state->non_default_variable_ids.clear();
        state->non_default_variable_positions.assign(variable_count, UINT32_MAX);
        state->non_default_aggregate_positions.assign(variable_count, UINT32_MAX);
        state->non_default_aggregates.clear();
        state->tracks_non_default_values = true;
        state->validated = false;
        for (t_variable_id variable_id = 0;
             variable_id < variable_count;
             ++variable_id) {
            const s_typed_value& value = state->values.Read(variable_id);
            const s_typed_value& default_value = defaults.values.Read(variable_id);
            if (value.type == CONFIG_VALUE_AGGREGATE) {
                s_non_default_aggregate aggregate;
                aggregate.variable_id = variable_id;
                aggregate.leaf_count = Aggregate_Non_Default_Leaf_Count(
                    value, default_value, &aggregate.leaves);
                if (aggregate.leaf_count != 0) {
                    state->non_default_aggregate_positions[variable_id] =
                        static_cast<uint32_t>(
                            state->non_default_aggregates.size());
                    state->non_default_aggregates.push_back(
                        std::move(aggregate));
                }
            }
            Set_Non_Default(state,
                variable_id,
                value.type == CONFIG_VALUE_AGGREGATE
                    ? Get_Non_Default_Aggregate(state, variable_id) != nullptr
                    : !Config_Values_Equal(value, default_value));
        }
        return true;
    }

    bool Get_Variable_Name(const s_runtime_schema* schema, t_variable_id variable_id, std::string* name) {
        if (schema == nullptr || name == nullptr || variable_id >= schema->variable_count) {
            return false;
        }
        if (schema->variables != nullptr) {
            *name = schema->variables[variable_id].name;
            return !name->empty();
        }
        name->clear();
        uint32_t node_index = schema == Get_Runtime_Schema()
            ? Find_Root_Node(variable_id)
            : CONFIG_NO_NODE;
        if (schema != Get_Runtime_Schema()) {
            for (uint32_t index = 0; index < schema->node_count; ++index) {
                const s_schema_node_descriptor& node = schema->nodes[index];
                if (node.parent == CONFIG_NO_NODE &&
                    variable_id >= node.first_variable &&
                    variable_id <= node.last_variable) {
                    node_index = index;
                    break;
                }
            }
        }
        t_variable_id variable_offset = 0;
        while (node_index != CONFIG_NO_NODE) {
            const s_schema_node_descriptor& node = schema->nodes[node_index];
            if (node.id[0] != '\0') {
                if (!name->empty()) name->push_back('.');
                *name += node.id;
            }
            if (node.kind == CONFIG_NODE_SCALAR) {
                return node.variable_id + variable_offset == variable_id;
            }
            if (node.kind == CONFIG_NODE_ARRAY) {
                if (node.array_element_count != 0) {
                    const uint32_t stride = node.array_element_variable_count;
                    if (stride == 0) return false;
                    const uint32_t element =
                        (variable_id - node.first_variable - variable_offset) / stride;
                    if (!name->empty()) name->push_back('.');
                    *name += std::to_string(node.array_index_base + element);
                    variable_offset += element * stride;
                    node_index = node.first_child;
                    continue;
                }
                uint32_t element = CONFIG_NO_NODE;
                for (uint32_t child = node.first_child; child != CONFIG_NO_NODE;
                     child = schema->nodes[child].next_sibling) {
                    const s_schema_node_descriptor& candidate = schema->nodes[child];
                    if (variable_id >= candidate.first_variable + variable_offset &&
                        variable_id <= candidate.last_variable + variable_offset) {
                        element = child;
                        break;
                    }
                }
                if (element == CONFIG_NO_NODE) return false;
                if (!name->empty()) name->push_back('.');
                *name += std::to_string(schema->nodes[element].external_index);
                node_index = element;
                continue;
            }
            uint32_t selected = CONFIG_NO_NODE;
            for (uint32_t child = node.first_child; child != CONFIG_NO_NODE;
                 child = schema->nodes[child].next_sibling) {
                const s_schema_node_descriptor& candidate = schema->nodes[child];
                if (variable_id >= candidate.first_variable + variable_offset &&
                    variable_id <= candidate.last_variable + variable_offset) {
                    selected = child;
                    break;
                }
            }
            node_index = selected;
        }
        return false;
    }

    bool Get_Variable_Name(t_variable_id variable_id, std::string* name) {
        if (name == nullptr || variable_id >= variable_count) return false;
        *name = runtime_variables[variable_id].name;
        return !name->empty();
    }

    bool Get_Variable_Leaf_Name(const s_runtime_schema* schema,
        t_variable_id variable_id,
        uint32_t leaf_index,
        std::string* name) {
        if (schema == nullptr || schema->variables == nullptr || name == nullptr ||
            variable_id >= schema->variable_count) return false;
        if (!schema->variables[variable_id].aggregate) {
            return leaf_index == 0 && Get_Variable_Name(schema, variable_id, name);
        }
        const s_schema_node_descriptor* leaf = Get_Variable_Leaf_Node(
            schema, variable_id, leaf_index);
        if (leaf == nullptr) return false;
        *name = schema->variables[variable_id].name;
        const s_runtime_variable_descriptor& descriptor = schema->variables[variable_id];
        uint32_t current_leaf = descriptor.vector ? leaf_index % descriptor.element_leaf_count : leaf_index;
        if (descriptor.vector) {
            name->push_back('.');
            *name += std::to_string(leaf_index / descriptor.element_leaf_count);
        }
        uint32_t node_index = descriptor.node;
        while (node_index < schema->node_count) {
            const s_schema_node_descriptor& node = schema->nodes[node_index];
            if (node.kind == CONFIG_NODE_SCALAR) {
                return true;
            }
            if ((node.kind == CONFIG_NODE_ARRAY || node.kind == CONFIG_NODE_MAP ||
                    node.kind == CONFIG_NODE_VECTOR) &&
                node.array_element_variable_count != 0) {
                const uint32_t element = (current_leaf - node.first_leaf) /
                    node.array_element_variable_count;
                name->push_back('.');
                *name += std::to_string(node.array_index_base + element);
                current_leaf -= element * node.array_element_variable_count;
                node_index = node.first_child;
                continue;
            }
            uint32_t selected = CONFIG_NO_NODE;
            for (uint32_t child = node.first_child; child != CONFIG_NO_NODE;
                 child = schema->nodes[child].next_sibling) {
                const s_schema_node_descriptor& candidate = schema->nodes[child];
                if (current_leaf >= candidate.first_leaf &&
                    current_leaf <= candidate.last_leaf) {
                    selected = child;
                    break;
                }
            }
            if (selected == CONFIG_NO_NODE) return false;
            if (schema->nodes[selected].id[0] != '\0') {
                name->push_back('.');
                *name += schema->nodes[selected].id;
            }
            node_index = selected;
        }
        return false;
    }

    bool Get_Variable_Leaf_Name(t_variable_id variable_id, uint32_t leaf_index, std::string* name) {
        return Get_Variable_Leaf_Name(
            Get_Runtime_Schema(), variable_id, leaf_index, name);
    }

    bool Resolve_Write_Path(s_config_write* write, s_cfg_diagnostic* diagnostic) {
        if (write == nullptr || diagnostic == nullptr) {
            return false;
        }
        s_variable_accessor accessor;
        if (!Find_Variable_Accessor(write->canonical_path.data(),
                write->canonical_path.size(),
                &accessor)) {
            diagnostic->location = write->provenance;
            diagnostic->message = "unknown configuration variable '" + write->canonical_path + "'";
            return false;
        }
        write->variable_id = accessor.variable_id;
        write->leaf_index = accessor.leaf_index;
        return true;
    }

    bool Initialize_Defaults(s_config_state* state) {
        if (state == nullptr) {
            return false;
        }
        if (!defaults_captured) {
            s_config_profile profile("capture-defaults");
            captured_defaults.values.clear();
            captured_defaults.values.resize(variable_count);
            std::vector<std::pair<t_variable_id, s_typed_value>> native_differences;
            for (auto& ids : arena_string_variables) ids.clear();
            for (t_variable_id variable_id = 0; variable_id < variable_count; ++variable_id) {
                if (runtime_variables[variable_id].has_arena_strings) arena_string_variables[Get_Variable_Group(variable_id)].push_back(variable_id);
                if (!generated::Get_Default_Value(
                        variable_id, &captured_defaults.values[variable_id])) {
                    captured_defaults.values.clear();
                    return false;
                }
                if (!runtime_variables[variable_id].has_arena_strings && !runtime_variables[variable_id].native_defaults) {
                    s_typed_value native;
                    if (!generated::Get_Current_Value(variable_id, &native)) return false;
                    if (!Config_Values_Equal(native, captured_defaults.values.Read(variable_id))) {
                        native_differences.emplace_back(variable_id, std::move(native));
                    }
                }
                if (captured_defaults.values[variable_id].type == CONFIG_VALUE_AGGREGATE) {
                    captured_defaults.values[variable_id].aggregate_values.Freeze();
                }
            }
            captured_defaults.values.Freeze();
            if (!Validate_State(&captured_defaults, true)) {
                captured_defaults = {};
                return false;
            }
            defaults_validated = true;
            captured_defaults.non_default_variable_positions.assign(variable_count, UINT32_MAX);
            captured_defaults.non_default_variable_positions.Freeze();
            captured_defaults.non_default_aggregate_positions = captured_defaults.non_default_variable_positions;
            defaults_captured = true;
            Initialize_Tracked_Defaults(&initial_native_state);
            for (const auto& difference : native_differences) {
                if (!Set_State_Value(&initial_native_state, difference.first, difference.second)) return false;
            }
        }
        *state = captured_defaults;
        state->non_default_values.clear();
        state->non_default_variable_ids.clear();
        state->non_default_variable_positions.clear();
        state->non_default_aggregate_positions.clear();
        state->non_default_aggregates.clear();
        state->tracks_non_default_values = false;
        state->validated = false;
        return true;
    }

    bool Initialize_Tracked_Defaults(s_config_state* state) {
        if (!Initialize_Defaults(state)) return false;
        state->non_default_values.assign(
            (variable_count + TRACKING_WORD_BITS - 1) / TRACKING_WORD_BITS, 0);
        state->non_default_variable_ids.clear();
        state->non_default_variable_positions = captured_defaults.non_default_variable_positions;
        state->non_default_aggregate_positions = captured_defaults.non_default_aggregate_positions;
        state->non_default_aggregates.clear();
        state->tracks_non_default_values = true;
        state->validated = false;
        return true;
    }

    bool Read_Current_State(s_config_state* state) {
        if (state == nullptr) {
            return false;
        }
        state->values.clear();
        state->values.resize(variable_count);
        state->non_default_values.clear();
        state->non_default_variable_ids.clear();
        state->non_default_variable_positions.clear();
        state->non_default_aggregate_positions.clear();
        state->non_default_aggregates.clear();
        state->tracks_non_default_values = false;
        state->validated = false;
        for (t_variable_id variable_id = 0; variable_id < variable_count; ++variable_id) {
            if (!generated::Get_Current_Value(variable_id, &state->values[variable_id])) {
                state->values.clear();
                return false;
            }
        }
        return true;
    }

    bool Config_Values_Equal(const s_typed_value& left,
        const s_typed_value& right) {
        if (left.type != right.type) return false;
        switch (left.type) {
            case CONFIG_VALUE_BOOL:
                return left.bool_value == right.bool_value;
            case CONFIG_VALUE_INT8:
            case CONFIG_VALUE_UINT8:
            case CONFIG_VALUE_INT16:
            case CONFIG_VALUE_UINT16:
            case CONFIG_VALUE_INT32:
            case CONFIG_VALUE_UINT32:
                return left.integer_value == right.integer_value;
            case CONFIG_VALUE_STRING:
                return left.string_value == right.string_value;
            case CONFIG_VALUE_BITMAP:
                return left.bitmap_value == right.bitmap_value;
            case CONFIG_VALUE_RATIO:
                return left.ratio_numerator == right.ratio_numerator &&
                    left.ratio_denominator == right.ratio_denominator;
            case CONFIG_VALUE_AGGREGATE:
                if (left.aggregate_values.size() != right.aggregate_values.size()) return false;
                if (left.aggregate_values.Shares_Baseline(right.aggregate_values)) {
                    return left.aggregate_values.Visit_Stored([&](size_t index, const s_typed_value& value) {
                        return Config_Values_Equal(value, right.aggregate_values[index]);
                    }) && right.aggregate_values.Visit_Stored([&](size_t index, const s_typed_value& value) {
                        return Config_Values_Equal(left.aggregate_values[index], value);
                    });
                }
                for (size_t index = 0; index < left.aggregate_values.size(); ++index) {
                    if (!Config_Values_Equal(left.aggregate_values[index],
                            right.aggregate_values[index])) return false;
                }
                return true;
        }
        return false;
    }

    bool Set_State_Value(s_config_state* state,
        t_variable_id variable_id,
        const s_typed_value& input) {
        if (state == nullptr || variable_id >= variable_count ||
            state->values.size() != variable_count ||
            state->values.Read(variable_id).type != input.type) {
            return false;
        }
        const bool tracking_valid = state->non_default_values.size() ==
                (variable_count + TRACKING_WORD_BITS - 1) /
                    TRACKING_WORD_BITS &&
            state->non_default_variable_positions.size() == variable_count &&
            state->non_default_aggregate_positions.size() == variable_count &&
            defaults_captured;
        if (state->tracks_non_default_values && !tracking_valid) {
            return false;
        }
        // The input may refer to an edit that is removed when restoring defaults.
        const s_typed_value value = input;
        const s_typed_value* baseline = state->values.Baseline_Value(variable_id);
        if (baseline != nullptr && Config_Values_Equal(value, *baseline)) {
            state->values.Reset(variable_id);
        } else {
            state->values[variable_id] = value;
        }
        state->validated = false;
        if (!state->tracks_non_default_values) return true;
        if (value.type == CONFIG_VALUE_AGGREGATE) {
            Remove_Non_Default_Aggregate(state, variable_id);
            s_non_default_aggregate aggregate;
            aggregate.variable_id = variable_id;
            aggregate.leaf_count = Aggregate_Non_Default_Leaf_Count(value,
                captured_defaults.values.Read(variable_id),
                &aggregate.leaves);
            if (aggregate.leaf_count != 0) {
                state->non_default_aggregate_positions[variable_id] =
                    static_cast<uint32_t>(state->non_default_aggregates.size());
                state->non_default_aggregates.push_back(std::move(aggregate));
            }
        }
        Set_Non_Default(state,
            variable_id,
            value.type == CONFIG_VALUE_AGGREGATE
                ? Get_Non_Default_Aggregate(state, variable_id) != nullptr
                : !Config_Values_Equal(value,
                      captured_defaults.values.Read(variable_id)));
        return true;
    }

    bool Set_State_Leaf_Value(s_config_state* state, t_variable_id variable_id, uint32_t leaf_index, const s_typed_value& input) {
        if (state == nullptr || variable_id >= variable_count ||
            state->values.size() != variable_count) return false;
        const s_typed_value value = input;
        const s_typed_value& current = state->values.Read(variable_id);
        if (current.type != CONFIG_VALUE_AGGREGATE) {
            return leaf_index == 0 && Set_State_Value(state, variable_id, value);
        }
        const s_runtime_variable_descriptor& descriptor = runtime_variables[variable_id];
        if (!descriptor.aggregate) return false;
        const s_type_descriptor* leaf_type = Get_Variable_Leaf_Type(
            variable_id, leaf_index);
        if (leaf_type == nullptr || leaf_type->value_type != value.type) return false;
        if (leaf_index >= current.aggregate_values.size() &&
            (!descriptor.vector || descriptor.element_leaf_count == 0)) {
            return false;
        }
        if (state->tracks_non_default_values &&
            (!defaults_captured ||
                state->non_default_values.size() !=
                    (variable_count + TRACKING_WORD_BITS - 1) /
                        TRACKING_WORD_BITS ||
                state->non_default_variable_positions.size() != variable_count ||
                state->non_default_aggregate_positions.size() !=
                    variable_count)) {
            return false;
        }
        const s_typed_value* default_value = state->tracks_non_default_values
            ? &captured_defaults.values.Read(variable_id)
            : nullptr;
        const bool inherited = &current == state->values.Baseline_Value(variable_id);
        s_typed_value& aggregate = state->values[variable_id];
        s_non_default_aggregate* non_default_aggregate =
            state->tracks_non_default_values
            ? Ensure_Non_Default_Aggregate(state, variable_id)
            : nullptr;
        if (leaf_index >= aggregate.aggregate_values.size()) {
            if (!descriptor.vector || descriptor.element_leaf_count == 0) return false;
            const size_t required = (leaf_index / descriptor.element_leaf_count + 1) *
                descriptor.element_leaf_count;
            const size_t old_size = aggregate.aggregate_values.size();
            aggregate.aggregate_values.resize(required);
            for (size_t index = old_size; index < required; ++index) {
                if (!generated::Get_Default_Leaf_Value(variable_id,
                        static_cast<uint32_t>(index),
                        &aggregate.aggregate_values[index])) {
                    aggregate.aggregate_values.resize(old_size);
                    if (state->tracks_non_default_values &&
                        non_default_aggregate->leaf_count == 0) {
                        Remove_Non_Default_Aggregate(state, variable_id);
                    }
                    if (inherited) state->values.Reset(variable_id);
                    return false;
                }
            }
            if (state->tracks_non_default_values) {
                const size_t default_size = default_value->aggregate_values.size();
                const size_t first_extra = std::max(old_size, default_size);
                if (required > first_extra) {
                    const size_t added = required - first_extra;
                    non_default_aggregate->leaf_count =
                        added > UINT32_MAX - non_default_aggregate->leaf_count
                        ? UINT32_MAX
                        : non_default_aggregate->leaf_count +
                            static_cast<uint32_t>(added);
                }
                non_default_aggregate->leaves.resize(
                    (required + TRACKING_WORD_BITS - 1) / TRACKING_WORD_BITS, 0);
                for (size_t index = first_extra; index < required; ++index) {
                    non_default_aggregate->leaves[index / TRACKING_WORD_BITS] |=
                        UINT64_C(1) << (index % TRACKING_WORD_BITS);
                }
            }
        }
        if (state->tracks_non_default_values &&
            leaf_index < default_value->aggregate_values.size()) {
            const bool was_non_default = !Config_Values_Equal(
                aggregate.aggregate_values.Read(leaf_index),
                default_value->aggregate_values[leaf_index]);
            const bool is_non_default = !Config_Values_Equal(value,
                default_value->aggregate_values[leaf_index]);
            if (!was_non_default && is_non_default) {
                ++non_default_aggregate->leaf_count;
                const size_t word_index = leaf_index / TRACKING_WORD_BITS;
                if (non_default_aggregate->leaves.size() <= word_index) {
                    non_default_aggregate->leaves.resize(word_index + 1, 0);
                }
                non_default_aggregate->leaves[word_index] |=
                    UINT64_C(1) << (leaf_index % TRACKING_WORD_BITS);
            } else if (was_non_default && !is_non_default) {
                if (non_default_aggregate->leaf_count == 0) return false;
                --non_default_aggregate->leaf_count;
                non_default_aggregate->leaves[leaf_index / TRACKING_WORD_BITS] &=
                    ~(UINT64_C(1) << (leaf_index % TRACKING_WORD_BITS));
            }
        }
        const s_typed_value* baseline = aggregate.aggregate_values.Baseline_Value(leaf_index);
        if (baseline != nullptr && Config_Values_Equal(value, *baseline)) {
            aggregate.aggregate_values.Reset(leaf_index);
        } else {
            aggregate.aggregate_values[leaf_index] = value;
        }
        state->validated = false;
        if (!state->tracks_non_default_values) return true;
        const bool non_default = non_default_aggregate->leaf_count != 0;
        if (!non_default) {
            Remove_Non_Default_Aggregate(state, variable_id);
            if (state->values.Baseline_Value(variable_id) == default_value) {
                state->values.Reset(variable_id);
            }
        }
        Set_Non_Default(state, variable_id, non_default);
        return true;
    }

    bool Copy_State_Value(s_config_state* target,
        const s_config_state* source,
        t_variable_id variable_id) {
        if (source == nullptr || variable_id >= source->values.size()) {
            return false;
        }
        return Set_State_Value(target, variable_id, source->values[variable_id]);
    }

    bool Overlay_Config_State(const s_config_snapshot_descriptor* descriptor,
        const s_config_state* source,
        s_config_state* target) {
        if (descriptor == nullptr || descriptor->variables == nullptr ||
            descriptor->variable_count == 0 || source == nullptr ||
            target == nullptr || source->values.size() != variable_count ||
            target->values.size() != variable_count) {
            return false;
        }
        if (source == target) return true;
        const bool sparse = State_Tracks_Non_Default_Values(source) &&
            State_Tracks_Non_Default_Values(target);
        if (sparse) {
            if (descriptor == &saved_config_snapshot || descriptor == &synchronized_config_snapshot) {
                const auto target_ids = target->non_default_variable_ids;
                const auto target_bits = target->non_default_values;
                for (t_variable_id id : target_ids) {
                    if (Snapshot_Contains_Variable(descriptor, id) && !Copy_State_Value(target, source, id)) return false;
                }
                for (t_variable_id id : source->non_default_variable_ids) {
                    if (Snapshot_Contains_Variable(descriptor, id) &&
                        (target_bits[id / TRACKING_WORD_BITS] & (UINT64_C(1) << (id % TRACKING_WORD_BITS))) == 0 &&
                        !Copy_State_Value(target, source, id)) return false;
                }
                return true;
            }
            std::vector<uint64_t> included(
                (variable_count + TRACKING_WORD_BITS - 1) /
                    TRACKING_WORD_BITS,
                0);
            for (uint32_t index = 0;
                 index < descriptor->variable_count;
                 ++index) {
                const t_variable_id variable_id = descriptor->variables[index];
                if (variable_id >= variable_count ||
                    (descriptor->group_mask &
                        (UINT32_C(1) << Get_Variable_Group(variable_id))) == 0) {
                    return false;
                }
                included[variable_id / TRACKING_WORD_BITS] |=
                    UINT64_C(1) << (variable_id % TRACKING_WORD_BITS);
            }
            std::vector<uint64_t> copied(
                included.size(), 0);
            const std::vector<t_variable_id> target_ids =
                target->non_default_variable_ids;
            const std::vector<t_variable_id> source_ids =
                source->non_default_variable_ids;
            for (t_variable_id variable_id : target_ids) {
                const uint64_t mask = UINT64_C(1)
                    << (variable_id % TRACKING_WORD_BITS);
                if ((included[variable_id / TRACKING_WORD_BITS] & mask) == 0) {
                    continue;
                }
                if (!Copy_State_Value(target, source, variable_id)) return false;
                copied[variable_id / TRACKING_WORD_BITS] |= mask;
            }
            for (t_variable_id variable_id : source_ids) {
                const uint64_t mask = UINT64_C(1)
                    << (variable_id % TRACKING_WORD_BITS);
                if ((included[variable_id / TRACKING_WORD_BITS] & mask) == 0 ||
                    (copied[variable_id / TRACKING_WORD_BITS] & mask) != 0) {
                    continue;
                }
                if (!Copy_State_Value(target, source, variable_id)) return false;
            }
            return true;
        }
        for (uint32_t index = 0;
             index < descriptor->variable_count;
             ++index) {
            const t_variable_id variable_id = descriptor->variables[index];
            if (variable_id >= variable_count ||
                (descriptor->group_mask &
                    (UINT32_C(1) << Get_Variable_Group(variable_id))) == 0) {
                return false;
            }
            if (!Copy_State_Value(target, source, variable_id)) return false;
        }
        return true;
    }

    bool State_Tracks_Non_Default_Values(const s_config_state* state) {
        return state != nullptr && state->tracks_non_default_values &&
            state->non_default_values.size() ==
            (variable_count + TRACKING_WORD_BITS - 1) /
                TRACKING_WORD_BITS &&
            state->non_default_variable_positions.size() == variable_count &&
            state->non_default_aggregate_positions.size() == variable_count;
    }

    bool State_Value_Is_Non_Default(const s_config_state* state,
        t_variable_id variable_id) {
        if (!State_Tracks_Non_Default_Values(state) ||
            variable_id >= variable_count) {
            return false;
        }
        return (state->non_default_values[variable_id / TRACKING_WORD_BITS] &
                   (UINT64_C(1) << (variable_id % TRACKING_WORD_BITS))) != 0;
    }

    bool State_Leaf_Is_Non_Default(const s_config_state* state,
        t_variable_id variable_id,
        uint32_t leaf_index) {
        if (!State_Value_Is_Non_Default(state, variable_id)) return false;
        if (!runtime_variables[variable_id].aggregate) return leaf_index == 0;
        const s_non_default_aggregate* aggregate =
            Get_State_Non_Default_Aggregate(state, variable_id);
        if (aggregate == nullptr) return false;
        const std::vector<uint64_t>& leaves = aggregate->leaves;
        return leaf_index / TRACKING_WORD_BITS < leaves.size() &&
            (leaves[leaf_index / TRACKING_WORD_BITS] &
                (UINT64_C(1) << (leaf_index % TRACKING_WORD_BITS))) != 0;
    }

    const s_non_default_aggregate* Get_State_Non_Default_Aggregate(
        const s_config_state* state,
        t_variable_id variable_id) {
        if (!State_Tracks_Non_Default_Values(state) ||
            variable_id >= variable_count) {
            return nullptr;
        }
        const uint32_t position =
            state->non_default_aggregate_positions[variable_id];
        return position < state->non_default_aggregates.size()
            ? &state->non_default_aggregates[position]
            : nullptr;
    }

    bool Read_State_Value(const s_config_state* state, t_variable_id variable_id, s_typed_value* value) {
        if (state == nullptr || value == nullptr || variable_id >= state->values.size()) {
            return false;
        }
        *value = state->values[variable_id];
        return true;
    }

    bool Apply_Write_Set(const s_config_write_set* write_set,
        s_config_state* state,
        s_cfg_diagnostic* diagnostic) {
        if (write_set == nullptr || state == nullptr || diagnostic == nullptr ||
            state->values.size() != variable_count) {
            return false;
        }
        *diagnostic = {};
        for (const s_config_write& write : write_set->writes) {
            if (!Validate_Write(&write, diagnostic)) {
                return false;
            }
        }
        struct s_leaf_backup {
            t_variable_id variable_id;
            uint32_t leaf_index;
            size_t aggregate_size;
            bool had_value;
            s_typed_value value;
        };
        std::vector<s_leaf_backup> backups;
        backups.reserve(write_set->writes.size());
        const bool was_validated = state->validated;
        for (const s_config_write& write : write_set->writes) {
            const s_typed_value& variable = state->values.Read(write.variable_id);
            s_leaf_backup backup = {write.variable_id, write.leaf_index, variable.aggregate_values.size(), false, {}};
            backup.had_value = Read_State_Leaf_Value(state,
                write.variable_id,
                write.leaf_index,
                &backup.value);
            if (!Set_State_Leaf_Value(
                    state, write.variable_id, write.leaf_index, write.value)) {
                // Only the failure path copies resized aggregates; normal writes
                // retain one leaf of history rather than an entire variable.
                for (auto prior = backups.rbegin(); prior != backups.rend(); ++prior) {
                    if (prior->had_value) {
                        Set_State_Leaf_Value(state, prior->variable_id, prior->leaf_index, prior->value);
                    }
                    const s_typed_value& current = state->values.Read(prior->variable_id);
                    if (current.type == CONFIG_VALUE_AGGREGATE &&
                        current.aggregate_values.size() != prior->aggregate_size) {
                        s_typed_value restored = current;
                        restored.aggregate_values.resize(prior->aggregate_size);
                        Set_State_Value(state, prior->variable_id, restored);
                    }
                }
                state->validated = was_validated;
                return Fail(&write, "unable to apply typed configuration write", diagnostic);
            }
            backups.push_back(std::move(backup));
        }
        return true;
    }

    void Register_State_Validator(t_state_validator validator) {
        if (validator == nullptr) return;
        std::vector<t_state_validator>& validators = State_Validators();
        if (std::find(validators.begin(), validators.end(), validator) == validators.end()) {
            validators.push_back(validator);
        }
    }

    void Register_Commit_Listener(t_commit_listener listener) {
        if (listener == nullptr) return;
        std::vector<t_commit_listener>& listeners = Commit_Listeners();
        if (std::find(listeners.begin(), listeners.end(), listener) == listeners.end()) {
            listeners.push_back(listener);
        }
    }

    bool Validate_State(const s_config_state* state) {
        s_config_profile profile("validate-state");
        const bool valid = Validate_State(state, false);
        if (state != nullptr) state->validated = valid;
        return valid;
    }

    bool Commit_Config_Transition(uint32_t group_mask,
        const s_config_state* prior,
        const s_config_state* current) {
        s_config_profile profile("commit-transition");
        if (prior == nullptr || current == nullptr ||
            prior->values.size() != variable_count ||
            current->values.size() != variable_count ||
            !State_Tracks_Non_Default_Values(prior) ||
            !State_Tracks_Non_Default_Values(current) ||
            !Validate_State(current)) {
            return false;
        }
        std::vector<t_variable_id> changed_ids;
        for (t_variable_id variable_id : prior->non_default_variable_ids) {
            const t_config_group group = Get_Variable_Group(variable_id);
            if ((group_mask & (UINT32_C(1) << group)) == 0 ||
                Tracked_Values_Equal(prior, current, variable_id)) {
                continue;
            }
            changed_ids.push_back(variable_id);
        }
        for (t_variable_id variable_id : current->non_default_variable_ids) {
            const t_config_group group = Get_Variable_Group(variable_id);
            if (State_Value_Is_Non_Default(prior, variable_id) ||
                (group_mask & (UINT32_C(1) << group)) == 0 ||
                Tracked_Values_Equal(prior, current, variable_id)) {
                continue;
            }
            changed_ids.push_back(variable_id);
        }

        bool rebuild_group[CONFIG_GROUP_COUNT] = {};
        for (t_variable_id variable_id : changed_ids) {
            if (runtime_variables[variable_id].has_arena_strings) {
                rebuild_group[Get_Variable_Group(variable_id)] = true;
            }
        }
        arena::s_arena* new_string_arenas[CONFIG_GROUP_COUNT] = {};
        for (int group = 0; group < CONFIG_GROUP_COUNT; ++group) {
            if (rebuild_group[group]) {
                new_string_arenas[group] = arena::Create();
            }
        }
        for (t_variable_id variable_id : changed_ids) {
            const t_config_group group = Get_Variable_Group(variable_id);
            if (runtime_variables[variable_id].has_arena_strings) continue;
            if (!Commit_Changed_Value(variable_id,
                    prior,
                    current,
                    new_string_arenas[group])) {
                for (arena::s_arena* string_arena : new_string_arenas) {
                    Release_String_Arena(string_arena);
                }
                return false;
            }
        }
        for (int group = 0; group < CONFIG_GROUP_COUNT; ++group) {
            if (!rebuild_group[group]) continue;
            for (t_variable_id variable_id : arena_string_variables[group]) {
                generated::Set_Value(variable_id,
                    &current->values[variable_id],
                    new_string_arenas[group]);
            }
        }
        for (int group = 0; group < CONFIG_GROUP_COUNT; ++group) {
            if (!rebuild_group[group]) continue;
            arena::s_arena* old_string_arena = active_string_arenas[group];
            active_string_arenas[group] = new_string_arenas[group];
            Release_String_Arena(old_string_arena);
        }
        for (t_commit_listener listener : Commit_Listeners()) {
            listener();
        }
        if (group_mask == UINT32_MAX || group_mask == (UINT32_C(1) << CONFIG_GROUP_COUNT) - 1) published_state = *current;
        return true;
    }

    bool Commit_Config_Groups(uint32_t group_mask, const s_config_state* state) {
        if (!Validate_State(state)) {
            return false;
        }
        arena::s_arena* new_string_arenas[CONFIG_GROUP_COUNT] = {};
        for (int group = 0; group < CONFIG_GROUP_COUNT; ++group) {
            if ((group_mask & (UINT32_C(1) << group)) != 0 &&
                Group_Has_Arena_Strings((t_config_group)group)) {
                new_string_arenas[group] = arena::Create();
            }
        }
        for (t_variable_id variable_id = 0; variable_id < variable_count; ++variable_id) {
            const t_config_group group = Get_Variable_Group(variable_id);
            if ((group_mask & (UINT32_C(1) << group)) == 0) continue;
            generated::Set_Value(
                variable_id, &state->values[variable_id], new_string_arenas[group]);
        }
        for (int group = 0; group < CONFIG_GROUP_COUNT; ++group) {
            if ((group_mask & (UINT32_C(1) << group)) == 0) continue;
            arena::s_arena* old_string_arena = active_string_arenas[group];
            active_string_arenas[group] = new_string_arenas[group];
            Release_String_Arena(old_string_arena);
        }
        for (t_commit_listener listener : Commit_Listeners()) {
            listener();
        }
        if (group_mask == UINT32_MAX || group_mask == (UINT32_C(1) << CONFIG_GROUP_COUNT) - 1) published_state = *state;
        return true;
    }

    bool Commit_State(const s_config_state* state) {
        static_assert(CONFIG_GROUP_COUNT <= 32);
        const uint32_t group_mask = CONFIG_GROUP_COUNT == 32 ? UINT32_MAX : (UINT32_C(1) << CONFIG_GROUP_COUNT) - 1;
        return Commit_Config_Groups(group_mask, state);
    }

    bool Commit_Initial_State(const s_config_state* state) {
        if (!defaults_captured || !State_Tracks_Non_Default_Values(state) || !Validate_State(state)) return false;
        // A caller may have synchronized native values independently (legacy
        // startup/tests). Refresh only the union of published and requested edits.
        s_config_state prior = State_Tracks_Non_Default_Values(&published_state) ? published_state : initial_native_state;
        std::vector<t_variable_id> ids = prior.non_default_variable_ids;
        ids.insert(ids.end(), state->non_default_variable_ids.begin(), state->non_default_variable_ids.end());
        for (auto id : ids) {
            if (runtime_variables[id].has_arena_strings || runtime_variables[id].aggregate) continue;
            s_typed_value native;
            if (!generated::Get_Current_Value(id, &native) || !Set_State_Value(&prior, id, native)) return false;
        }
        if (State_Tracks_Non_Default_Values(&published_state)) {
            return Commit_Config_Transition(UINT32_MAX, &prior, state);
        }
        for (int group = 0; group < CONFIG_GROUP_COUNT; ++group) {
            if (active_string_arenas[group] != nullptr || arena_string_variables[group].empty()) continue;
            active_string_arenas[group] = arena::Create();
            for (auto id : arena_string_variables[group]) {
                generated::Set_Value(id, &state->values[id], active_string_arenas[group]);
            }
        }
        return Commit_Config_Transition(UINT32_MAX, &prior, state);
    }

    bool Commit_Config_Group(t_config_group group, const s_config_state* state) {
        if (group < 0 || group >= CONFIG_GROUP_COUNT) return false;
        return Commit_Config_Groups(UINT32_C(1) << group, state);
    }

    arena::s_arena* Acquire_String_Arena(t_config_group group) {
        if (group < 0 || group >= CONFIG_GROUP_COUNT) return nullptr;
        if (active_string_arenas[group] != nullptr) {
            arena::Add_Ref(active_string_arenas[group]);
        }
        return active_string_arenas[group];
    }

    bool Is_Active_String(const char* value) {
        for (arena::s_arena* string_arena : active_string_arenas) {
            if (arena::Contains(string_arena, value)) return true;
        }
        return false;
    }

    void Shutdown_Runtime() {
        for (arena::s_arena*& string_arena : active_string_arenas) {
            arena::s_arena* old_string_arena = string_arena;
            string_arena = nullptr;
            Release_String_Arena(old_string_arena);
        }
        captured_defaults = {};
        initial_native_state = {};
        published_state = {};
        defaults_captured = false;
        defaults_validated = false;
    }

    namespace generated {

        void Set_Resolved_Value(const s_resolved_variable* resolved,
            const s_typed_value* value,
            arena::s_arena* string_arena);
        bool Get_Resolved_Value(const s_resolved_variable* resolved,
            s_typed_value* value);

        void Set_Value(t_variable_id variable_id, const s_typed_value* value, arena::s_arena* string_arena) {
            if (value != nullptr && value->type == CONFIG_VALUE_AGGREGATE) {
                const s_runtime_variable_descriptor& descriptor = runtime_variables[variable_id];
                if (descriptor.vector) {
                    const size_t element_count = value->aggregate_values.size() /
                        descriptor.element_leaf_count;
                    if (!Resize_Vector(variable_id, element_count)) {
                        exitm::Exit_With_Message("Invalid generated vector configuration binding");
                    }
                }
                for (uint32_t leaf = 0; leaf < value->aggregate_values.size(); ++leaf) {
                    s_resolved_variable variable = {};
                    const s_typed_value& leaf_value = value->aggregate_values[leaf];
                    if (!Resolve_Variable(variable_id, leaf, &variable) ||
                        leaf_value.type != variable.type->value_type) {
                        exitm::Exit_With_Message("Invalid generated aggregate configuration binding");
                    }
                    Set_Resolved_Value(&variable, &leaf_value, string_arena);
                }
                return;
            }
            s_resolved_variable variable = {};
            if (value == nullptr || !Resolve_Variable(variable_id, 0, &variable) ||
                value->type != variable.type->value_type) {
                exitm::Exit_With_Message("Invalid generated configuration binding");
            }
            Set_Resolved_Value(&variable, value, string_arena);
        }

        bool Set_Leaf_Value(t_variable_id variable_id,
            uint32_t leaf_index,
            const s_typed_value* value,
            arena::s_arena* string_arena) {
            s_resolved_variable variable = {};
            if (value == nullptr ||
                !Resolve_Variable(variable_id, leaf_index, &variable) ||
                value->type != variable.type->value_type) {
                return false;
            }
            Set_Resolved_Value(&variable, value, string_arena);
            return true;
        }

        void Set_Resolved_Value(const s_resolved_variable* resolved,
            const s_typed_value* value,
            arena::s_arena* string_arena) {
            const s_resolved_variable& variable = *resolved;
            if (value->type == CONFIG_VALUE_STRING) {
                char* arena_string = nullptr;
                for (size_t index = 0; index < variable.target_count; ++index) {
                    const s_variable_target::e_storage storage = variable.targets[index].storage;
                    if (storage == s_variable_target::CONFIG_TARGET_ARENA_STRING_POINTER ||
                        (storage == s_variable_target::CONFIG_TARGET_NULLABLE_ARENA_STRING_POINTER &&
                            !value->string_value.empty())) {
                        arena_string = arena::Copy_String_N(
                            string_arena, value->string_value.data(), value->string_value.size());
                        break;
                    }
                }
                for (size_t index = 0; index < variable.target_count; ++index) {
                    const s_variable_target& target = variable.targets[index];
                    if (target.storage == s_variable_target::CONFIG_TARGET_ARENA_STRING_POINTER ||
                        target.storage ==
                            s_variable_target::CONFIG_TARGET_NULLABLE_ARENA_STRING_POINTER) {
                        if (target.size != sizeof(char*)) {
                            exitm::Exit_With_Message("Invalid arena string configuration target");
                        }
                        unaligned::Store<char*>(target.address, arena_string);
                        continue;
                    }
                    if (target.size == 0) continue;
                    const size_t copy_size = std::min(value->string_value.size(), target.size - 1);
                    std::memcpy(target.address, value->string_value.data(), copy_size);
                    ((char*)target.address)[copy_size] = '\0';
                }
                return;
            }
            if (value->type == CONFIG_VALUE_BITMAP) {
                for (size_t index = 0; index < variable.target_count; ++index) {
                    const s_variable_target& target = variable.targets[index];
                    if (target.size == value->bitmap_value.size()) {
                        std::memcpy(target.address, value->bitmap_value.data(), target.size);
                    }
                }
                return;
            }
            if (value->type == CONFIG_VALUE_RATIO) {
                if (variable.target_count == 2) {
                    Write_Integer(variable.targets[0].address, variable.targets[0].size, variable.type->ratio_component_type, value->ratio_numerator);
                    Write_Integer(variable.targets[1].address, variable.targets[1].size, variable.type->ratio_component_type, value->ratio_denominator);
                }
                return;
            }
            const int64_t integer = value->type == CONFIG_VALUE_BOOL ? (value->bool_value ? 1 : 0) : value->integer_value;
            for (size_t index = 0; index < variable.target_count; ++index) {
                Write_Integer(variable.targets[index].address, variable.targets[index].size, value->type, integer);
            }
        }

        bool Get_Current_Value(t_variable_id variable_id, s_typed_value* value) {
            if (value == nullptr || variable_id >= variable_count) return false;
            const s_runtime_variable_descriptor& descriptor = runtime_variables[variable_id];
            if (descriptor.aggregate) {
                const size_t element_count = descriptor.vector ? Get_Vector_Size(variable_id) : 1;
                *value = {};
                value->type = CONFIG_VALUE_AGGREGATE;
                value->aggregate_values.resize(element_count * descriptor.element_leaf_count);
                for (uint32_t leaf = 0; leaf < value->aggregate_values.size(); ++leaf) {
                    s_resolved_variable variable = {};
                    if (!Resolve_Variable(variable_id, leaf, &variable) ||
                        !Get_Resolved_Value(&variable, &value->aggregate_values[leaf])) {
                        return false;
                    }
                }
                return true;
            }
            s_resolved_variable variable = {};
            if (!Resolve_Variable(variable_id, 0, &variable) ||
                variable.target_count == 0) {
                return false;
            }
            return Get_Resolved_Value(&variable, value);
        }

        bool Get_Current_Leaf_Value(t_variable_id variable_id, uint32_t leaf_index, s_typed_value* value) {
            s_resolved_variable variable = {};
            return value != nullptr &&
                Resolve_Variable(variable_id, leaf_index, &variable) &&
                variable.target_count != 0 && Get_Resolved_Value(&variable, value);
        }

        bool Get_Resolved_Value(const s_resolved_variable* resolved,
            s_typed_value* value) {
            const s_resolved_variable& variable = *resolved;
            *value = {};
            value->type = variable.type->value_type;
            const s_variable_target& target = variable.targets[0];
            switch (value->type) {
                case CONFIG_VALUE_BOOL: {
                    int64_t integer = 0;
                    if (!Read_Integer(target.address, value->type, &integer)) return false;
                    value->bool_value = integer != 0;
                    return true;
                }
                case CONFIG_VALUE_INT8:
                case CONFIG_VALUE_UINT8:
                case CONFIG_VALUE_INT16:
                case CONFIG_VALUE_UINT16:
                case CONFIG_VALUE_INT32:
                case CONFIG_VALUE_UINT32:
                    return Read_Integer(target.address, value->type, &value->integer_value);
                case CONFIG_VALUE_STRING:
                    if (target.storage == s_variable_target::CONFIG_TARGET_ARENA_STRING_POINTER ||
                        target.storage ==
                            s_variable_target::CONFIG_TARGET_NULLABLE_ARENA_STRING_POINTER) {
                        const char* current = unaligned::Load<char*>(target.address);
                        value->string_value = current != nullptr ? current : "";
                    } else {
                        value->string_value = (const char*)target.address;
                    }
                    return true;
                case CONFIG_VALUE_BITMAP: {
                    const uint8_t* bytes = (const uint8_t*)target.address;
                    value->bitmap_value.assign(bytes, bytes + target.size);
                    return true;
                }
                case CONFIG_VALUE_RATIO:
                    if (variable.target_count != 2) return false;
                    return Read_Integer(variable.targets[0].address,
                               variable.type->ratio_component_type,
                               &value->ratio_numerator) &&
                        Read_Integer(variable.targets[1].address,
                            variable.type->ratio_component_type,
                            &value->ratio_denominator);
                case CONFIG_VALUE_AGGREGATE:
                    return false;
            }
            return false;
        }

    } // namespace generated

} // namespace config