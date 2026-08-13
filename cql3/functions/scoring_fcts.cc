/*
 * Copyright (C) 2026-present ScyllaDB
 */

/*
 * SPDX-License-Identifier: LicenseRef-ScyllaDB-Source-Available-1.1
 */

#include "scoring_fcts.hh"
#include "native_scalar_function.hh"
#include "utils/log.hh"
#include <seastar/core/on_internal_error.hh>

namespace cql3 {
namespace functions {

extern logging::logger log;

shared_ptr<function> make_bm25_function() {
    // BM25 fulltext scoring function: bm25(column, query) -> float
    // Registered with utf8_type args; ascii is implicitly coerced to utf8 by the type system.
    //
    // BM25 scores depend on document statistics, so the result is not determined by the visible arguments alone.
    // Marked as non-pure (false) to prevent the expression evaluator from constant-folding BM25(literal, literal)
    // at prepare time, which is both semantically correct and avoids a spurious crash path.
    return make_native_scalar_function<false>("bm25", float_type, {utf8_type, utf8_type},
        [] (std::span<const bytes_opt>) -> bytes_opt {
            // BM25() is rejected at prepare time for every valid query path.
            // Reaching the function body means the prepare-time check was bypassed - this is a bug.
            on_internal_error(log, "BM25() reached scalar evaluation; prepare-time check should have prevented this");
        });
}

shared_ptr<function> make_highlight_function() {
    // Fulltext highlighting function: highlight(column, query) -> text
    // Registered with utf8_type args; ascii is implicitly coerced to utf8 by the type system.
    //
    // Like BM25(), the excerpt cannot be derived from the visible arguments - it is computed by
    // the Vector Store, which owns the analyzer and the term statistics. Marked non-pure so the
    // expression evaluator does not constant-fold it at prepare time.
    return make_native_scalar_function<false>("highlight", utf8_type, {utf8_type, utf8_type},
        [] (std::span<const bytes_opt>) -> bytes_opt {
            // HIGHLIGHT() is replaced by an external_value at prepare time for every valid query
            // path. Reaching the function body means that replacement did not happen - a bug.
            on_internal_error(log, "HIGHLIGHT() reached scalar evaluation; prepare-time replacement should have prevented this");
        });
}

} // namespace functions
} // namespace cql3
