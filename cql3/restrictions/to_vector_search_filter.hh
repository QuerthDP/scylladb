/*
 * Copyright (C) 2026-present ScyllaDB
 */

/*
 * SPDX-License-Identifier: LicenseRef-ScyllaDB-Source-Available-1.0
 */

#pragma once

#include "vector_search/filter.hh"

namespace cql3 {

class query_options;

namespace restrictions {

class statement_restrictions;

/// Converts CQL statement restrictions to a vector_search::filter.
/// This function extracts primary key restrictions
/// from the statement_restrictions and converts them to the filter format
/// expected by the Vector Store service.
vector_search::filter to_vector_search_filter(const statement_restrictions& restrictions, const query_options& options, bool allow_filtering);

} // namespace restrictions
} // namespace cql3
