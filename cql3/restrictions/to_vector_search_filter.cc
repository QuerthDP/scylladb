/*
 * Copyright (C) 2026-present ScyllaDB
 */

/*
 * SPDX-License-Identifier: LicenseRef-ScyllaDB-Source-Available-1.0
 */

#include "to_vector_search_filter.hh"
#include "statement_restrictions.hh"
#include "cql3/query_options.hh"
#include "cql3/expr/expr-utils.hh"
#include "cql3/expr/evaluate.hh"
#include "types/json_utils.hh"
#include "types/list.hh"
#include "types/tuple.hh"

namespace cql3::restrictions {

namespace {

std::optional<vector_search::single_column_op> to_single_column_op(expr::oper_t op) {
    switch (op) {
    case expr::oper_t::EQ:
        return vector_search::single_column_op::eq;
    case expr::oper_t::LT:
        return vector_search::single_column_op::lt;
    case expr::oper_t::LTE:
        return vector_search::single_column_op::le;
    case expr::oper_t::GT:
        return vector_search::single_column_op::gt;
    case expr::oper_t::GTE:
        return vector_search::single_column_op::ge;
    default:
        return std::nullopt;
    }
}

std::optional<vector_search::multi_column_op> to_multi_column_op(expr::oper_t op) {
    switch (op) {
    case expr::oper_t::EQ:
        return vector_search::multi_column_op::eq;
    case expr::oper_t::LT:
        return vector_search::multi_column_op::lt;
    case expr::oper_t::LTE:
        return vector_search::multi_column_op::le;
    case expr::oper_t::GT:
        return vector_search::multi_column_op::gt;
    case expr::oper_t::GTE:
        return vector_search::multi_column_op::ge;
    default:
        return std::nullopt;
    }
}

rjson::copyable_value value_to_json(const data_type& type, const cql3::raw_value& val) {
    if (val.is_null()) {
        return rjson::copyable_value(rjson::null_value());
    }
    auto json_str = to_json_string(*type, to_bytes(val.view()));
    return rjson::copyable_value(rjson::parse(json_str));
}

void process_single_column_in_restriction(const expr::binary_operator& binop, const expr::column_value& col, const query_options& options,
        std::vector<vector_search::restriction>& restrictions) {
    vector_search::in_restriction in_restr;
    in_restr.column = col.col->name_as_text();

    auto rhs_val = expr::evaluate(binop.rhs, options);
    if (!rhs_val.is_null()) {
        auto rhs_type = expr::type_of(binop.rhs);
        auto list_type = dynamic_pointer_cast<const list_type_impl>(rhs_type);
        if (list_type) {
            auto elements = value_cast<list_type_impl::native_type>(list_type->deserialize(to_bytes(rhs_val.view())));
            auto elem_type = list_type->get_elements_type();
            for (const auto& elem : elements) {
                if (!elem.is_null()) {
                    auto elem_json_str = to_json_string(*elem_type, elem_type->decompose(elem));
                    in_restr.values.push_back(rjson::copyable_value(rjson::parse(elem_json_str)));
                }
            }
        }
    }
    restrictions.push_back(std::move(in_restr));
}


void process_single_column_restriction(const expr::binary_operator& binop, const expr::column_value& col, const query_options& options,
        std::vector<vector_search::restriction>& restrictions) {
    if (binop.op == expr::oper_t::IN) {
        process_single_column_in_restriction(binop, col, options, restrictions);
        return;
    }

    auto vs_op = to_single_column_op(binop.op);
    if (!vs_op) {
        return; // Unsupported operator
    }

    vector_search::single_column_restriction single_restr;
    single_restr.op = *vs_op;
    single_restr.column = col.col->name_as_text();

    auto rhs_val = expr::evaluate(binop.rhs, options);
    single_restr.value = value_to_json(col.col->type, rhs_val);

    restrictions.push_back(std::move(single_restr));
}

std::vector<sstring> extract_column_names(const expr::tuple_constructor& lhs_tuple) {
    std::vector<sstring> columns;
    for (const auto& elem : lhs_tuple.elements) {
        if (auto* cv = expr::as_if<expr::column_value>(&elem)) {
            columns.push_back(cv->col->name_as_text());
        }
    }
    return columns;
}

std::vector<data_type> extract_column_types(const expr::tuple_constructor& lhs_tuple) {
    std::vector<data_type> types;
    for (const auto& elem : lhs_tuple.elements) {
        if (auto* cv = expr::as_if<expr::column_value>(&elem)) {
            types.push_back(cv->col->type);
        }
    }
    return types;
}

void process_multi_column_in_restriction(const expr::binary_operator& binop, const expr::tuple_constructor& lhs_tuple, const query_options& options,
        std::vector<vector_search::restriction>& restrictions) {
    vector_search::multi_column_in_restriction in_restr;
    in_restr.columns = extract_column_names(lhs_tuple);
    auto col_types = extract_column_types(lhs_tuple);

    if (in_restr.columns.empty()) {
        return;
    }

    auto rhs_val = expr::evaluate(binop.rhs, options);
    if (rhs_val.is_null()) {
        restrictions.push_back(std::move(in_restr));
        return;
    }

    // The RHS is a list of tuples
    auto rhs_type = expr::type_of(binop.rhs);
    auto list_type = dynamic_pointer_cast<const list_type_impl>(rhs_type);
    if (!list_type) {
        return;
    }
    auto tuple_type = dynamic_pointer_cast<const tuple_type_impl>(list_type->get_elements_type());
    if (!tuple_type) {
        return;
    }

    auto list_elements = value_cast<list_type_impl::native_type>(list_type->deserialize(to_bytes(rhs_val.view())));
    const auto& tuple_elem_types = tuple_type->all_types();

    for (const auto& list_elem : list_elements) {
        if (list_elem.is_null()) {
            continue;
        }
        auto tuple_elements = value_cast<tuple_type_impl::native_type>(tuple_type->deserialize(tuple_type->decompose(list_elem)));

        std::vector<rjson::copyable_value> tuple_values;
        for (size_t i = 0; i < tuple_elements.size() && i < tuple_elem_types.size(); ++i) {
            if (!tuple_elements[i].is_null()) {
                auto elem_json_str = to_json_string(*tuple_elem_types[i], tuple_elem_types[i]->decompose(tuple_elements[i]));
                tuple_values.push_back(rjson::copyable_value(rjson::parse(elem_json_str)));
            } else {
                tuple_values.push_back(rjson::copyable_value(rjson::null_value()));
            }
        }
        in_restr.values.push_back(std::move(tuple_values));
    }

    restrictions.push_back(std::move(in_restr));
}

void process_multi_column_restriction(const expr::binary_operator& binop, const expr::tuple_constructor& lhs_tuple, const query_options& options,
        std::vector<vector_search::restriction>& restrictions) {
    if (binop.op == expr::oper_t::IN) {
        process_multi_column_in_restriction(binop, lhs_tuple, options, restrictions);
        return;
    }

    auto vs_op = to_multi_column_op(binop.op);
    if (!vs_op) {
        return; // Unsupported operator
    }

    vector_search::multi_column_restriction multi_restr;
    multi_restr.op = *vs_op;
    multi_restr.columns = extract_column_names(lhs_tuple);

    if (auto* rhs_tuple = expr::as_if<expr::tuple_constructor>(&binop.rhs)) {
        for (size_t i = 0; i < rhs_tuple->elements.size() && i < lhs_tuple.elements.size(); ++i) {
            auto val = expr::evaluate(rhs_tuple->elements[i], options);
            if (auto* lhs_col = expr::as_if<expr::column_value>(&lhs_tuple.elements[i])) {
                multi_restr.values.push_back(value_to_json(lhs_col->col->type, val));
            }
        }
    } else {
        auto rhs_val = expr::evaluate(binop.rhs, options);
        if (!rhs_val.is_null() && lhs_tuple.type) {
            auto tuple_type = dynamic_pointer_cast<const tuple_type_impl>(lhs_tuple.type);
            if (tuple_type) {
                auto elements = value_cast<tuple_type_impl::native_type>(tuple_type->deserialize(to_bytes(rhs_val.view())));
                const auto& types = tuple_type->all_types();
                for (size_t i = 0; i < elements.size() && i < types.size(); ++i) {
                    if (!elements[i].is_null()) {
                        auto elem_json_str = to_json_string(*types[i], types[i]->decompose(elements[i]));
                        multi_restr.values.push_back(rjson::copyable_value(rjson::parse(elem_json_str)));
                    } else {
                        multi_restr.values.push_back(rjson::copyable_value(rjson::null_value()));
                    }
                }
            }
        }
    }

    if (!multi_restr.columns.empty() && multi_restr.columns.size() == multi_restr.values.size()) {
        restrictions.push_back(std::move(multi_restr));
    }
}

void process_binary_operator(const expr::binary_operator& binop, const query_options& options, std::vector<vector_search::restriction>& restrictions) {
    if (auto* cv = expr::as_if<expr::column_value>(&binop.lhs)) {
        process_single_column_restriction(binop, *cv, options, restrictions);
    }

    if (auto* tuple = expr::as_if<expr::tuple_constructor>(&binop.lhs)) {
        process_multi_column_restriction(binop, *tuple, options, restrictions);
    }
}

void extract_restrictions_from_expression(const expr::expression& expr, const query_options& options, std::vector<vector_search::restriction>& restrictions) {
    expr::for_each_expression<expr::binary_operator>(expr, [&](const expr::binary_operator& binop) {
        process_binary_operator(binop, options, restrictions);
    });
}

} // anonymous namespace

vector_search::filter to_vector_search_filter(const statement_restrictions& restrictions, const query_options& options, bool allow_filtering) {
    vector_search::filter result;

    extract_restrictions_from_expression(restrictions.get_partition_key_restrictions(), options, result.restrictions);
    extract_restrictions_from_expression(restrictions.get_clustering_columns_restrictions(), options, result.restrictions);

    result.allow_filtering = allow_filtering;

    return result;
}

} // namespace cql3::restrictions
