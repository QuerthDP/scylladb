/*
 * Copyright (C) 2026-present ScyllaDB
 */

/*
 * SPDX-License-Identifier: LicenseRef-ScyllaDB-Source-Available-1.1
 */

#pragma once

#include "external_index_select_statement.hh"

#include <optional>

namespace cql3::statements {

struct bm25_ordering_info {
    secondary_index::index index;
    expr::expression search_term;
    std::optional<size_t> external_value_index;
    std::vector<expr::expression> selected_bm25_terms;
    std::optional<size_t> highlight_external_value_index;
    std::vector<expr::expression> selected_highlight_terms;
    /// The indexed text column that HIGHLIGHT() excerpts, and its position in the selection.
    /// The column is fetched from the base table so its text can be sent to the Vector Store.
    const column_definition* highlight_column = nullptr;
    std::optional<uint32_t> highlight_column_selector_index;
};

/// Resolves BM25 ordering metadata from the query's ORDER BY clause.
/// Returns std::nullopt if the query does not have BM25 ordering.
std::optional<bm25_ordering_info> get_bm25_ordering_info(
        data_dictionary::database db,
        schema_ptr schema,
        lw_shared_ptr<const raw::select_statement::parameters> parameters,
        prepare_context& ctx);

/// Processes bm25() calls in prepared_selectors:
/// - When ordering_info is absent, throws on the first bm25() occurrence at any nesting level.
/// - When present, validates each against ordering_info (column name at prepare time,
///   constant terms eagerly), replaces with external_value{index, float_type},
///   and stores non-literal search terms for runtime validation.
/// Stores index into ordering_info->external_value_index on first bm25() occurrence.
/// Returns true if any bm25() call was found and processed.
bool prepare_bm25_selectors(std::vector<selection::prepared_selector>& prepared_selectors, std::optional<bm25_ordering_info>& ordering_info, size_t index);

/// Same as prepare_bm25_selectors(), but for highlight() calls. Replaces each with
/// external_value{index, utf8_type}; the slot is filled at execute time with the excerpt
/// the Vector Store computed from the row's own text.
/// Returns true if any highlight() call was found and processed.
bool prepare_highlight_selectors(
        std::vector<selection::prepared_selector>& prepared_selectors, std::optional<bm25_ordering_info>& ordering_info, size_t index);

class fulltext_indexed_table_select_statement : public external_index_select_statement {
    bm25_ordering_info _bm25_ordering_info;

public:
    static constexpr size_t max_fts_query_limit = 1000;
    static ::shared_ptr<cql3::statements::select_statement> prepare(data_dictionary::database db,
            schema_ptr schema,
            uint32_t bound_terms,
            lw_shared_ptr<const parameters> parameters,
            ::shared_ptr<selection::selection> selection,
            ::shared_ptr<const restrictions::statement_restrictions> restrictions,
            ::shared_ptr<std::vector<size_t>> group_by_cell_indices,
            bool is_reversed,
            ordering_comparator_type ordering_comparator,
            std::optional<expr::expression> limit,
            std::optional<expr::expression> per_partition_limit,
            cql_stats& stats,
            std::optional<bm25_ordering_info> ordering_info,
            std::unique_ptr<cql3::attributes> attrs);

    fulltext_indexed_table_select_statement(schema_ptr schema, uint32_t bound_terms, lw_shared_ptr<const parameters> parameters,
            ::shared_ptr<selection::selection> selection, ::shared_ptr<const restrictions::statement_restrictions> restrictions,
            ::shared_ptr<std::vector<size_t>> group_by_cell_indices, bool is_reversed, ordering_comparator_type ordering_comparator,
            std::optional<expr::expression> limit, std::optional<expr::expression> per_partition_limit, cql_stats& stats, bm25_ordering_info ordering_info,
            std::unique_ptr<cql3::attributes> attrs);

private:
    std::string_view index_search_type_name() const override {
        return "Full-Text Search";
    }

    future<::shared_ptr<cql_transport::messages::result_message>> execute_search(
            query_processor& qp, service::query_state& state, const query_options& options, uint64_t limit) const override;

    /// Reads the indexed text out of the already-fetched base-table rows and asks the Vector Store
    /// to excerpt it. Returns one excerpt per row, in row order.
    future<vector_search::vector_store_client::highlights> fetch_highlights(query_processor& qp, const query_options& options,
            const query::result& rows, const query::partition_slice& slice, const sstring& search_term, abort_source& as) const;
};

} // namespace cql3::statements
