-- ============================================================
-- temporal_agg --1.0.sql
--
-- Recommended usage: keep table columns as normal scalar types and
-- attach an expression GiST index on agg_leaf(id, t_start, t_end, v2).
-- Query through seg_minmax_v2 — no custom column types required.
--
--     CREATE TABLE events (
--         id      int,
--         t_start timestamp,
--         t_end   timestamp,
--         v2      bigint
--     );
--
--     CREATE INDEX events_idx ON events
--         USING gist (agg_leaf(id, t_start, t_end, v2));
--
--     SELECT seg_minmax_v2('events_idx'::regclass,
--                          7, '2024-01-01'::timestamp,
--                             '2024-12-31'::timestamp);
--     -- returns bigint[] of {min(v2), max(v2), sum(v2), count}
--     -- empty result -> {NULL, NULL, NULL, 0}
-- ============================================================

-- ===== Types =====

CREATE TYPE agg_leaf_type;

CREATE FUNCTION agg_leaf_in(cstring) RETURNS agg_leaf_type
    AS 'MODULE_PATHNAME', 'agg_leaf_in'
    LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION agg_leaf_out(agg_leaf_type) RETURNS cstring
    AS 'MODULE_PATHNAME', 'agg_leaf_out'
    LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE agg_leaf_type (
    internallength = 32,
    input          = agg_leaf_in,
    output         = agg_leaf_out,
    alignment      = double,
    storage        = plain
);

CREATE TYPE agg_key_type;

CREATE FUNCTION agg_key_in(cstring) RETURNS agg_key_type
    AS 'MODULE_PATHNAME', 'agg_key_in'
    LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION agg_key_out(agg_key_type) RETURNS cstring
    AS 'MODULE_PATHNAME', 'agg_key_out'
    LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE agg_key_type (
    internallength = 56,
    input          = agg_key_in,
    output         = agg_key_out,
    alignment      = double,
    storage        = plain
);

CREATE TYPE agg_query_type;

CREATE FUNCTION agg_query_in(cstring) RETURNS agg_query_type
    AS 'MODULE_PATHNAME', 'agg_query_in'
    LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION agg_query_out(agg_query_type) RETURNS cstring
    AS 'MODULE_PATHNAME', 'agg_query_out'
    LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE agg_query_type (
    internallength = 24,
    input          = agg_query_in,
    output         = agg_query_out,
    alignment      = double
);

-- ===== Constructors =====

CREATE FUNCTION agg_leaf(int, timestamp, timestamp, bigint)
RETURNS agg_leaf_type
AS 'MODULE_PATHNAME', 'agg_leaf'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION agg_query(int, timestamp, timestamp)
RETURNS agg_query_type
AS 'MODULE_PATHNAME', 'agg_query'
LANGUAGE C IMMUTABLE STRICT;

-- ===== Predicate operator (filter use, not required by traversal) =====

CREATE FUNCTION agg_overlap_query(agg_leaf_type, agg_query_type)
RETURNS bool
AS 'MODULE_PATHNAME', 'agg_overlap_query'
LANGUAGE C IMMUTABLE STRICT;

CREATE OPERATOR && (
    LEFTARG   = agg_leaf_type,
    RIGHTARG  = agg_query_type,
    PROCEDURE = agg_overlap_query
);

-- ===== GiST support =====

CREATE FUNCTION agg_consistent(internal, internal, smallint, oid, internal)
RETURNS bool AS 'MODULE_PATHNAME', 'agg_consistent'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION agg_union(internal, internal)
RETURNS internal AS 'MODULE_PATHNAME', 'agg_union'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION agg_compress(internal)
RETURNS internal AS 'MODULE_PATHNAME', 'agg_compress'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION agg_decompress(internal)
RETURNS internal AS 'MODULE_PATHNAME', 'agg_decompress'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION agg_penalty(internal, internal, internal)
RETURNS internal AS 'MODULE_PATHNAME', 'agg_penalty'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION agg_picksplit(internal, internal)
RETURNS internal AS 'MODULE_PATHNAME', 'agg_picksplit'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION agg_same(internal, internal, internal)
RETURNS internal AS 'MODULE_PATHNAME', 'agg_same'
LANGUAGE C IMMUTABLE STRICT;

CREATE OPERATOR CLASS agg_ops
    DEFAULT FOR TYPE agg_leaf_type USING gist AS
        OPERATOR 1 && (agg_leaf_type, agg_query_type),

        FUNCTION 1 agg_consistent(internal, internal, smallint, oid, internal),
        FUNCTION 2 agg_union(internal, internal),
        FUNCTION 3 agg_compress(internal),
        FUNCTION 4 agg_decompress(internal),
        FUNCTION 5 agg_penalty(internal, internal, internal),
        FUNCTION 6 agg_picksplit(internal, internal),
        FUNCTION 7 agg_same(internal, internal, internal),

        STORAGE agg_key_type;

-- ===== Custom aggregate-traversal entry point =====
-- Returns bigint[] = {min(v2), max(v2), sum(v2), count} over rows where
-- id = q.id and the row's time interval overlaps [q.start, q.end].
-- For an empty result: {NULL, NULL, NULL, 0} — matches SQL aggregate
-- semantics for an empty set.

CREATE FUNCTION seg_minmax_v2(regclass, agg_query_type)
RETURNS bigint[]
AS 'MODULE_PATHNAME', 'seg_minmax_v2'
LANGUAGE C STRICT;

-- Convenience wrapper: callers pass plain (id, start, end) — no need to
-- construct an agg_query_type explicitly.
CREATE FUNCTION seg_minmax_v2(regclass, int, timestamp, timestamp)
RETURNS bigint[]
AS $$ SELECT seg_minmax_v2($1, agg_query($2, $3, $4)) $$
LANGUAGE SQL STABLE STRICT;
