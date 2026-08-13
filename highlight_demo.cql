-- Full-Text Search highlighting demo.
--
-- Paste into cqlsh, or run with:  cqlsh -f ~/highlight_demo.cql
--
-- Requires a Scylla built from the `highlighting` branch, running with
-- vector_store_primary_uri pointing at a Vector Store built from its own
-- `highlighting` branch.
--
-- Rows are inserted BEFORE the index is created, so the index is populated by
-- the Vector Store's initial full scan of the table rather than by CDC.

DROP KEYSPACE IF EXISTS fts_demo;

CREATE KEYSPACE fts_demo
  WITH replication = {'class': 'NetworkTopologyStrategy', 'replication_factor': 1}
  AND TABLETS = {'enabled': true};

USE fts_demo;

CREATE TABLE articles (
    id int PRIMARY KEY,
    title text,
    body text
);

-- Long-ish bodies on purpose: the default snippet window is 150 characters, so
-- the excerpt is a real window into the text, not the whole column.

INSERT INTO articles (id, title, body) VALUES (1, 'Scaling vector search',
  'Vector search scalability depends far more on how the index is laid out in memory than on raw CPU. A graph index that no longer fits in RAM will thrash, and recall collapses long before throughput does. Sharding the vector index across nodes keeps each shard resident and lets query latency stay flat as the corpus grows.');

INSERT INTO articles (id, title, body) VALUES (2, 'Full text search internals',
  'A full text search engine keeps an inverted index mapping each term to the documents it occurs in. The original text is normally discarded after tokenization, which is why highlighting has to re-analyze the document to find where the query terms actually landed inside it.');

INSERT INTO articles (id, title, body) VALUES (3, 'Choosing a distance function',
  'Cosine similarity is the usual default for embeddings because it ignores magnitude, but dot product is cheaper when vectors are already normalized. Euclidean distance behaves differently on clustered data and is worth benchmarking before you commit to it.');

INSERT INTO articles (id, title, body) VALUES (4, 'A quick note on quotas',
  'The quick brown fox jumped over the lazy dog while the meadow was still wet with dew. Nothing in this paragraph has anything to do with databases, which makes it useful for checking that irrelevant rows stay out of the result set.');

INSERT INTO articles (id, title, body) VALUES (5, 'Hybrid retrieval',
  'Hybrid search combines lexical scoring with vector similarity. The lexical leg finds documents that literally contain the query terms; the semantic leg finds documents that mean the same thing while sharing no vocabulary at all. Reconciling the two rankings is the hard part.');

-- A row with no body at all: it should never break the highlight round trip.
INSERT INTO articles (id, title) VALUES (6, 'Placeholder with no body');

-- Index created last, after the data is already in place.
CREATE CUSTOM INDEX articles_body_fts ON articles(body) USING 'fulltext_index';

-- The Vector Store needs a moment to scan the table and build the index.
-- If the first query below returns nothing, wait a second and run it again.

EXPAND ON;

-- 1. The basic shape: an excerpt with the matched terms wrapped in <b>...</b>.
SELECT id, title, HIGHLIGHT(body, 'vector search') AS excerpt
FROM articles
WHERE BM25(body, 'vector search') > 0
ORDER BY BM25(body, 'vector search')
LIMIT 5;

-- 2. Relevance score and excerpt side by side.
SELECT id,
       BM25(body, 'index memory') AS score,
       HIGHLIGHT(body, 'index memory') AS excerpt
FROM articles
WHERE BM25(body, 'index memory') > 0
ORDER BY BM25(body, 'index memory')
LIMIT 5;

-- 3. The indexed column does not have to be selected. It is fetched behind the
--    scenes to feed the highlighter, but never reaches the client.
SELECT id, title, HIGHLIGHT(body, 'highlighting') AS excerpt
FROM articles
WHERE BM25(body, 'highlighting') > 0
ORDER BY BM25(body, 'highlighting')
LIMIT 5;

-- 4. Boolean query: only the row matching both terms comes back.
SELECT id, HIGHLIGHT(body, 'fox AND meadow') AS excerpt
FROM articles
WHERE BM25(body, 'fox AND meadow') > 0
ORDER BY BM25(body, 'fox AND meadow')
LIMIT 5;

-- 5. Phrase query. Double quotes inside the CQL string delimit the phrase.
SELECT id, HIGHLIGHT(body, '"full text search"') AS excerpt
FROM articles
WHERE BM25(body, '"full text search"') > 0
ORDER BY BM25(body, '"full text search"')
LIMIT 5;

-- 6. A query whose terms are spread across several documents, so each excerpt
--    is a different window into a different body.
SELECT id, title, HIGHLIGHT(body, 'query OR latency OR ranking') AS excerpt
FROM articles
WHERE BM25(body, 'query OR latency OR ranking') > 0
ORDER BY BM25(body, 'query OR latency OR ranking')
LIMIT 6;

-- 7. Bind markers work too; all three occurrences must resolve to the same term.
--    (In cqlsh, ? cannot be used interactively - this is what a driver sends.)
--    SELECT id, HIGHLIGHT(body, ?) FROM articles
--      WHERE BM25(body, ?) > 0 ORDER BY BM25(body, ?) LIMIT 5;

EXPAND OFF;

-- Rejected at prepare time - uncomment to see the errors.
--
-- HIGHLIGHT() needs a full-text search query to attach to:
--   SELECT HIGHLIGHT(body, 'vector') FROM articles LIMIT 5;
--
-- ...and must use the same column as the BM25() driving the search:
--   SELECT HIGHLIGHT(title, 'vector') FROM articles
--     WHERE BM25(body, 'vector') > 0 ORDER BY BM25(body, 'vector') LIMIT 5;
--
-- ...and the same search term:
--   SELECT HIGHLIGHT(body, 'something else') FROM articles
--     WHERE BM25(body, 'vector') > 0 ORDER BY BM25(body, 'vector') LIMIT 5;
