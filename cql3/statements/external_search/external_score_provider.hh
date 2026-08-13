/*
 * Copyright (C) 2026-present ScyllaDB
 */

/*
 * SPDX-License-Identifier: LicenseRef-ScyllaDB-Source-Available-1.1
 */

#pragma once

#include "cql3/selection/selection.hh"
#include "vector_search/vector_store_client.hh"

#include <optional>

class schema;

namespace cql3::statements {

/// Injects per-row values derived from an external search result into result rows.
///
/// The relevance score is matched against the ranked result list by PK/CK. Highlight excerpts
/// are matched by row position instead: they were computed from the very same base-table rows,
/// visited in the same order, so the n-th row gets the n-th excerpt.
class external_score_provider : public cql3::selection::external_values_provider {
    const vector_search::vector_store_client::primary_keys& _results;
    mutable size_t _current_index;
    const std::optional<size_t> _external_value_index;
    const schema& _schema;

    vector_search::vector_store_client::highlights _highlights;
    mutable size_t _current_row;
    const std::optional<size_t> _highlight_external_value_index;

public:
    external_score_provider(const vector_search::vector_store_client::primary_keys& results, std::optional<size_t> external_value_index,
            const schema& schema, vector_search::vector_store_client::highlights highlights = {},
            std::optional<size_t> highlight_external_value_index = std::nullopt);

    bool try_fill(std::vector<cql3::raw_value>& external_values, std::span<const bytes> partition_key, std::span<const bytes> clustering_key,
            const query::result_row_view& static_row, const query::result_row_view* row) const override;
};

} // namespace cql3::statements
