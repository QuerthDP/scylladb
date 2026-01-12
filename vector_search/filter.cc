/*
 * Copyright (C) 2026-present ScyllaDB
 */

/*
 * SPDX-License-Identifier: LicenseRef-ScyllaDB-Source-Available-1.0
 */

#include "filter.hh"
#include "utils/rjson.hh"

namespace vector_search {

namespace {

rjson::value to_json(const single_column_restriction& r) {
    auto obj = rjson::empty_object();
    rjson::add(obj, "type", rjson::from_string(to_json_type_string(r.op)));
    rjson::add(obj, "lhs", rjson::from_string(r.column));
    rjson::add(obj, "rhs", rjson::copy(r.value));
    return obj;
}

rjson::value to_json(const in_restriction& r) {
    auto obj = rjson::empty_object();
    rjson::add(obj, "type", rjson::from_string("IN"));
    rjson::add(obj, "lhs", rjson::from_string(r.column));
    auto rhs_arr = rjson::empty_array();
    for (const auto& v : r.values) {
        rjson::push_back(rhs_arr, rjson::copy(v));
    }
    rjson::add(obj, "rhs", std::move(rhs_arr));
    return obj;
}

rjson::value to_json(const multi_column_restriction& r) {
    auto obj = rjson::empty_object();
    rjson::add(obj, "type", rjson::from_string(to_json_type_string(r.op)));
    auto lhs_arr = rjson::empty_array();
    for (const auto& col : r.columns) {
        rjson::push_back(lhs_arr, rjson::from_string(col));
    }
    rjson::add(obj, "lhs", std::move(lhs_arr));
    auto rhs_arr = rjson::empty_array();
    for (const auto& v : r.values) {
        rjson::push_back(rhs_arr, rjson::copy(v));
    }
    rjson::add(obj, "rhs", std::move(rhs_arr));
    return obj;
}

rjson::value to_json(const multi_column_in_restriction& r) {
    auto obj = rjson::empty_object();
    rjson::add(obj, "type", rjson::from_string("()IN()"));
    auto lhs_arr = rjson::empty_array();
    for (const auto& col : r.columns) {
        rjson::push_back(lhs_arr, rjson::from_string(col));
    }
    rjson::add(obj, "lhs", std::move(lhs_arr));
    auto rhs_arr = rjson::empty_array();
    for (const auto& tuple : r.values) {
        auto tuple_arr = rjson::empty_array();
        for (const auto& v : tuple) {
            rjson::push_back(tuple_arr, rjson::copy(v));
        }
        rjson::push_back(rhs_arr, std::move(tuple_arr));
    }
    rjson::add(obj, "rhs", std::move(rhs_arr));
    return obj;
}

single_column_op parse_single_column_op(std::string_view type) {
    if (type == "==") {
        return single_column_op::eq;
    } else if (type == "<") {
        return single_column_op::lt;
    } else if (type == "<=") {
        return single_column_op::le;
    } else if (type == ">") {
        return single_column_op::gt;
    } else if (type == ">=") {
        return single_column_op::ge;
    }
    throw rjson::error(fmt::format("Unknown single-column operator: {}", type));
}

multi_column_op parse_multi_column_op(std::string_view type) {
    if (type == "()==()") {
        return multi_column_op::eq;
    } else if (type == "()<()") {
        return multi_column_op::lt;
    } else if (type == "()<=()") {
        return multi_column_op::le;
    } else if (type == "()>()") {
        return multi_column_op::gt;
    } else if (type == "()>=()") {
        return multi_column_op::ge;
    }
    throw rjson::error(fmt::format("Unknown multi-column operator: {}", type));
}

bool is_multi_column_op(std::string_view type) {
    return type.starts_with("()");
}

std::vector<sstring> parse_column_names(const rjson::value& arr, std::string_view context) {
    std::vector<sstring> columns;
    for (const auto& col : arr.GetArray()) {
        if (!col.IsString()) {
            throw rjson::error(fmt::format("{} 'lhs' elements must be strings", context));
        }
        columns.push_back(rjson::to_sstring(col));
    }
    return columns;
}

std::vector<rjson::copyable_value> parse_values(const rjson::value& arr) {
    std::vector<rjson::copyable_value> values;
    for (const auto& v : arr.GetArray()) {
        values.push_back(rjson::copy(v));
    }
    return values;
}

in_restriction parse_in_restriction(const rjson::value& lhs, const rjson::value& rhs) {
    if (!lhs.IsString()) {
        throw rjson::error("IN restriction 'lhs' must be a string");
    }
    if (!rhs.IsArray()) {
        throw rjson::error("IN restriction 'rhs' must be an array");
    }
    return in_restriction{
            .column = rjson::to_sstring(lhs),
            .values = parse_values(rhs),
    };
}

multi_column_in_restriction parse_multi_column_in_restriction(const rjson::value& lhs, const rjson::value& rhs) {
    if (!lhs.IsArray()) {
        throw rjson::error("Multi-column IN restriction 'lhs' must be an array");
    }
    if (!rhs.IsArray()) {
        throw rjson::error("Multi-column IN restriction 'rhs' must be an array");
    }

    std::vector<std::vector<rjson::copyable_value>> tuple_values;
    for (const auto& tuple : rhs.GetArray()) {
        if (!tuple.IsArray()) {
            throw rjson::error("Multi-column IN restriction 'rhs' elements must be arrays");
        }
        tuple_values.push_back(parse_values(tuple));
    }

    return multi_column_in_restriction{
            .columns = parse_column_names(lhs, "Multi-column IN restriction"),
            .values = std::move(tuple_values),
    };
}

multi_column_restriction parse_multi_column_restriction(std::string_view type, const rjson::value& lhs, const rjson::value& rhs) {
    if (!lhs.IsArray()) {
        throw rjson::error("Multi-column restriction 'lhs' must be an array");
    }
    if (!rhs.IsArray()) {
        throw rjson::error("Multi-column restriction 'rhs' must be an array");
    }
    return multi_column_restriction{
            .op = parse_multi_column_op(type),
            .columns = parse_column_names(lhs, "Multi-column restriction"),
            .values = parse_values(rhs),
    };
}

single_column_restriction parse_single_column_restriction(std::string_view type, const rjson::value& lhs, const rjson::value& rhs) {
    if (!lhs.IsString()) {
        throw rjson::error("Single-column restriction 'lhs' must be a string");
    }
    return single_column_restriction{
            .op = parse_single_column_op(type),
            .column = rjson::to_sstring(lhs),
            .value = rjson::copy(rhs),
    };
}

restriction parse_restriction(const rjson::value& json) {
    if (!json.IsObject()) {
        throw rjson::error("Restriction must be an object");
    }

    const auto* type_val = rjson::find(json, "type");
    if (!type_val || !type_val->IsString()) {
        throw rjson::error("Restriction must have a 'type' string field");
    }
    auto type = rjson::to_string_view(*type_val);

    const auto* lhs_val = rjson::find(json, "lhs");
    if (!lhs_val) {
        throw rjson::error("Restriction must have a 'lhs' field");
    }

    const auto* rhs_val = rjson::find(json, "rhs");
    if (!rhs_val) {
        throw rjson::error("Restriction must have a 'rhs' field");
    }

    if (type == "IN") {
        return parse_in_restriction(*lhs_val, *rhs_val);
    }
    if (type == "()IN()") {
        return parse_multi_column_in_restriction(*lhs_val, *rhs_val);
    }
    if (is_multi_column_op(type)) {
        return parse_multi_column_restriction(type, *lhs_val, *rhs_val);
    }
    return parse_single_column_restriction(type, *lhs_val, *rhs_val);
}

} // anonymous namespace

rjson::value to_json(const filter& f) {
    if (f.restrictions.empty() && !f.allow_filtering) {
        return rjson::empty_object();
    }

    auto obj = rjson::empty_object();

    auto restrictions_arr = rjson::empty_array();
    for (const auto& r : f.restrictions) {
        std::visit(
                [&restrictions_arr](const auto& restriction) {
                    rjson::push_back(restrictions_arr, to_json(restriction));
                },
                r);
    }
    rjson::add(obj, "restrictions", std::move(restrictions_arr));
    rjson::add(obj, "allow_filtering", f.allow_filtering);

    return obj;
}

filter parse_filter(const rjson::value& json) {
    if (!json.IsObject()) {
        throw rjson::error("Filter must be an object");
    }

    filter f;

    const auto* restrictions_val = rjson::find(json, "restrictions");
    if (restrictions_val) {
        if (!restrictions_val->IsArray()) {
            throw rjson::error("Filter 'restrictions' must be an array");
        }
        for (const auto& r : restrictions_val->GetArray()) {
            f.restrictions.push_back(parse_restriction(r));
        }
    }

    const auto* allow_filtering_val = rjson::find(json, "allow_filtering");
    if (allow_filtering_val) {
        if (!allow_filtering_val->IsBool()) {
            throw rjson::error("Filter 'allow_filtering' must be a boolean");
        }
        f.allow_filtering = allow_filtering_val->GetBool();
    }

    return f;
}

} // namespace vector_search
