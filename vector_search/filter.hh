/*
 * Copyright (C) 2026-present ScyllaDB
 */

/*
 * SPDX-License-Identifier: LicenseRef-ScyllaDB-Source-Available-1.0
 */

#pragma once

#include <seastar/core/sstring.hh>
#include <variant>
#include <vector>
#include "utils/rjson.hh"

namespace vector_search {

enum class single_column_op {
    eq, // ==
    lt, // <
    le, // <=
    gt, // >
    ge, // >=
};

enum class multi_column_op {
    eq, // ()==()
    lt, // ()<()
    le, // ()<=()
    gt, // ()>()
    ge, // ()>=()
};

/// A single-column restriction: column op value
struct single_column_restriction {
    single_column_op op;
    sstring column;
    rjson::copyable_value value;
};

/// A single-column IN restriction: column IN (val1, val2, ...)
struct in_restriction {
    sstring column;
    std::vector<rjson::copyable_value> values;
};

/// A multi-column restriction: (col1, col2, ...) op (val1, val2, ...)
struct multi_column_restriction {
    multi_column_op op;
    std::vector<sstring> columns;
    std::vector<rjson::copyable_value> values;
};

/// A multi-column IN restriction: (col1, col2, ...) IN ((val1, val2, ...), ...)
struct multi_column_in_restriction {
    std::vector<sstring> columns;
    std::vector<std::vector<rjson::copyable_value>> values;
};

using restriction = std::variant<single_column_restriction, in_restriction, multi_column_restriction, multi_column_in_restriction>;

struct filter {
    std::vector<restriction> restrictions;
    bool allow_filtering = false;
};

inline sstring to_json_type_string(single_column_op op) {
    switch (op) {
    case single_column_op::eq:
        return "==";
    case single_column_op::lt:
        return "<";
    case single_column_op::le:
        return "<=";
    case single_column_op::gt:
        return ">";
    case single_column_op::ge:
        return ">=";
    }
    __builtin_unreachable();
}

inline sstring to_json_type_string(multi_column_op op) {
    switch (op) {
    case multi_column_op::eq:
        return "()==()";
    case multi_column_op::lt:
        return "()<()";
    case multi_column_op::le:
        return "()<=()";
    case multi_column_op::gt:
        return "()>()";
    case multi_column_op::ge:
        return "()>=()";
    }
    __builtin_unreachable();
}

rjson::value to_json(const filter& f);

filter parse_filter(const rjson::value& json);

} // namespace vector_search
