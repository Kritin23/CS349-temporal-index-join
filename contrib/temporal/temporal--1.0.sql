-- ===============================
-- temporal_key_type
-- ===============================

CREATE TYPE temporal_key_type;

CREATE FUNCTION temporal_in(cstring)
RETURNS temporal_key_type
AS 'MODULE_PATHNAME', 'temporal_in'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION temporal_out(temporal_key_type)
RETURNS cstring
AS 'MODULE_PATHNAME', 'temporal_out'
LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE temporal_key_type (
    internallength = 24,
    input = temporal_in,
    output = temporal_out,
    alignment = double,
    storage = plain
);

-- ===============================
-- leaf_key_type
-- ===============================

CREATE TYPE leaf_key_type;

CREATE FUNCTION leaf_in(cstring)
RETURNS leaf_key_type
AS 'MODULE_PATHNAME', 'leaf_in'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION leaf_out(leaf_key_type)
RETURNS cstring
AS 'MODULE_PATHNAME', 'leaf_out'
LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE leaf_key_type (
    internallength = 24,
    input = leaf_in,
    output = leaf_out,
    alignment = double,
    storage = plain
);

-- ===============================
-- time_itv_query
-- ===============================

CREATE TYPE time_itv_query;

CREATE FUNCTION time_itv_in(cstring)
RETURNS time_itv_query
AS 'MODULE_PATHNAME', 'time_itv_in'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION time_itv_out(time_itv_query)
RETURNS cstring
AS 'MODULE_PATHNAME', 'time_itv_out'
LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE time_itv_query (
    internallength = 16,
    input = time_itv_in,
    output = time_itv_out,
    alignment = double
);

-- ===============================
-- idx_point_query
-- ===============================

CREATE TYPE idx_point_query;

CREATE FUNCTION idx_point_in(cstring)
RETURNS idx_point_query
AS 'MODULE_PATHNAME', 'idx_point_in'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION idx_point_out(idx_point_query)
RETURNS cstring
AS 'MODULE_PATHNAME', 'idx_point_out'
LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE idx_point_query (
    internallength = 16,
    input = idx_point_in,
    output = idx_point_out,
    alignment = double
);

-- ===============================
-- idx_range_query
-- ===============================

CREATE TYPE idx_range_query;

CREATE FUNCTION idx_range_in(cstring)
RETURNS idx_range_query
AS 'MODULE_PATHNAME', 'idx_range_in'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION idx_range_out(idx_range_query)
RETURNS cstring
AS 'MODULE_PATHNAME', 'idx_range_out'
LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE idx_range_query (
    internallength = 24,
    input = idx_range_in,
    output = idx_range_out,
    alignment = double
);

-- ===============================
-- Constructor functions
-- ===============================

CREATE FUNCTION temporal_key(int, timestamp, timestamp)
RETURNS leaf_key_type
AS 'MODULE_PATHNAME', 'temporal_key'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION temporal_point(int, timestamp)
RETURNS idx_point_query
AS 'MODULE_PATHNAME', 'temporal_point'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION temporal_range(int, timestamp, timestamp)
RETURNS idx_range_query
AS 'MODULE_PATHNAME', 'temporal_range'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION temporal_time_range(timestamp, timestamp)
RETURNS time_itv_query
AS 'MODULE_PATHNAME', 'temporal_time_range'
LANGUAGE C IMMUTABLE STRICT;

-- ===============================
-- OPERATOR EXECUTION FUNCTIONS (NEW)
-- ===============================

-- OVERLAP (&&)
CREATE FUNCTION temporal_overlap_time_itv(leaf_key_type, time_itv_query) RETURNS boolean AS 'MODULE_PATHNAME', 'temporal_overlap_time_itv' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION temporal_overlap_timestamp(leaf_key_type, timestamp) RETURNS boolean AS 'MODULE_PATHNAME', 'temporal_overlap_timestamp' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION temporal_overlap_idx_point(leaf_key_type, idx_point_query) RETURNS boolean AS 'MODULE_PATHNAME', 'temporal_overlap_idx_point' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION temporal_overlap_idx_range(leaf_key_type, idx_range_query) RETURNS boolean AS 'MODULE_PATHNAME', 'temporal_overlap_idx_range' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION temporal_overlap_leaf_key(leaf_key_type, leaf_key_type) RETURNS boolean AS 'MODULE_PATHNAME', 'temporal_overlap_leaf_key' LANGUAGE C IMMUTABLE STRICT;

-- CONTAINS (@>)
CREATE FUNCTION temporal_contains_time_itv(leaf_key_type, time_itv_query) RETURNS boolean AS 'MODULE_PATHNAME', 'temporal_contains_time_itv' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION temporal_contains_timestamp(leaf_key_type, timestamp) RETURNS boolean AS 'MODULE_PATHNAME', 'temporal_contains_timestamp' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION temporal_contains_idx_point(leaf_key_type, idx_point_query) RETURNS boolean AS 'MODULE_PATHNAME', 'temporal_contains_idx_point' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION temporal_contains_idx_range(leaf_key_type, idx_range_query) RETURNS boolean AS 'MODULE_PATHNAME', 'temporal_contains_idx_range' LANGUAGE C IMMUTABLE STRICT;

-- CONTAINED BY (<@)
CREATE FUNCTION temporal_contained_time_itv(leaf_key_type, time_itv_query) RETURNS boolean AS 'MODULE_PATHNAME', 'temporal_contained_time_itv' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION temporal_contained_timestamp(leaf_key_type, timestamp) RETURNS boolean AS 'MODULE_PATHNAME', 'temporal_contained_timestamp' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION temporal_contained_idx_point(leaf_key_type, idx_point_query) RETURNS boolean AS 'MODULE_PATHNAME', 'temporal_contained_idx_point' LANGUAGE C IMMUTABLE STRICT;
CREATE FUNCTION temporal_contained_idx_range(leaf_key_type, idx_range_query) RETURNS boolean AS 'MODULE_PATHNAME', 'temporal_contained_idx_range' LANGUAGE C IMMUTABLE STRICT;


-- ===============================
-- GiST SUPPORT FUNCTIONS
-- ===============================

CREATE FUNCTION temporal_consistent(internal, internal, smallint, oid, internal)
RETURNS boolean AS 'MODULE_PATHNAME', 'temporal_consistent' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION temporal_union(internal, internal)
RETURNS internal AS 'MODULE_PATHNAME', 'temporal_union' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION temporal_compress(internal)
RETURNS internal AS 'MODULE_PATHNAME', 'temporal_compress' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION temporal_decompress(internal)
RETURNS internal AS 'MODULE_PATHNAME', 'temporal_decompress' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION temporal_penalty(internal, internal, internal)
RETURNS internal AS 'MODULE_PATHNAME', 'temporal_penalty' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION temporal_picksplit(internal, internal)
RETURNS internal AS 'MODULE_PATHNAME', 'temporal_picksplit' LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION temporal_same(internal, internal, internal)
RETURNS internal AS 'MODULE_PATHNAME', 'temporal_same' LANGUAGE C IMMUTABLE STRICT;

-- ===============================
-- OPERATORS (Mapped to specific functions)
-- ===============================

CREATE OPERATOR && (LEFTARG = leaf_key_type, RIGHTARG = time_itv_query, PROCEDURE = temporal_overlap_time_itv);
CREATE OPERATOR && (LEFTARG = leaf_key_type, RIGHTARG = timestamp, PROCEDURE = temporal_overlap_timestamp);
CREATE OPERATOR && (LEFTARG = leaf_key_type, RIGHTARG = idx_point_query, PROCEDURE = temporal_overlap_idx_point);
CREATE OPERATOR && (LEFTARG = leaf_key_type, RIGHTARG = idx_range_query, PROCEDURE = temporal_overlap_idx_range);
CREATE OPERATOR && (LEFTARG = leaf_key_type, RIGHTARG = leaf_key_type, PROCEDURE = temporal_overlap_leaf_key);

CREATE OPERATOR @> (LEFTARG = leaf_key_type, RIGHTARG = time_itv_query, PROCEDURE = temporal_contains_time_itv);
CREATE OPERATOR @> (LEFTARG = leaf_key_type, RIGHTARG = timestamp, PROCEDURE = temporal_contains_timestamp);
CREATE OPERATOR @> (LEFTARG = leaf_key_type, RIGHTARG = idx_point_query, PROCEDURE = temporal_contains_idx_point);
CREATE OPERATOR @> (LEFTARG = leaf_key_type, RIGHTARG = idx_range_query, PROCEDURE = temporal_contains_idx_range);

CREATE OPERATOR <@ (LEFTARG = leaf_key_type, RIGHTARG = time_itv_query, PROCEDURE = temporal_contained_time_itv);
CREATE OPERATOR <@ (LEFTARG = leaf_key_type, RIGHTARG = timestamp, PROCEDURE = temporal_contained_timestamp);
CREATE OPERATOR <@ (LEFTARG = leaf_key_type, RIGHTARG = idx_point_query, PROCEDURE = temporal_contained_idx_point);
CREATE OPERATOR <@ (LEFTARG = leaf_key_type, RIGHTARG = idx_range_query, PROCEDURE = temporal_contained_idx_range);

ALTER OPERATOR && (leaf_key_type, leaf_key_type)
SET (COMMUTATOR = '&&');

CREATE OPERATOR @> (LEFTARG = leaf_key_type, RIGHTARG = time_itv_query, PROCEDURE = temporal_contains_time_itv);
CREATE OPERATOR @> (LEFTARG = leaf_key_type, RIGHTARG = timestamp, PROCEDURE = temporal_contains_timestamp);
CREATE OPERATOR @> (LEFTARG = leaf_key_type, RIGHTARG = idx_point_query, PROCEDURE = temporal_contains_idx_point);
CREATE OPERATOR @> (LEFTARG = leaf_key_type, RIGHTARG = idx_range_query, PROCEDURE = temporal_contains_idx_range);

CREATE OPERATOR <@ (LEFTARG = leaf_key_type, RIGHTARG = time_itv_query, PROCEDURE = temporal_contained_time_itv);
CREATE OPERATOR <@ (LEFTARG = leaf_key_type, RIGHTARG = timestamp, PROCEDURE = temporal_contained_timestamp);
CREATE OPERATOR <@ (LEFTARG = leaf_key_type, RIGHTARG = idx_point_query, PROCEDURE = temporal_contained_idx_point);
CREATE OPERATOR <@ (LEFTARG = leaf_key_type, RIGHTARG = idx_range_query, PROCEDURE = temporal_contained_idx_range);

-- ===============================
-- OPERATOR CLASS
-- ===============================

CREATE OPERATOR CLASS temporal_ops
    DEFAULT FOR TYPE leaf_key_type USING gist AS

        OPERATOR 1  && (leaf_key_type, time_itv_query),
        OPERATOR 2  && (leaf_key_type, timestamp),
        OPERATOR 3  && (leaf_key_type, idx_point_query), 
        OPERATOR 4  && (leaf_key_type, idx_range_query), 

        OPERATOR 5  @> (leaf_key_type, time_itv_query),
        OPERATOR 6  @> (leaf_key_type, timestamp),
        OPERATOR 7  @> (leaf_key_type, idx_point_query),
        OPERATOR 8  @> (leaf_key_type, idx_range_query),

        OPERATOR 9  <@ (leaf_key_type, time_itv_query),
        OPERATOR 10 <@ (leaf_key_type, timestamp),
        OPERATOR 11 <@ (leaf_key_type, idx_point_query),
        OPERATOR 12 <@ (leaf_key_type, idx_range_query),

        OPERATOR 13 && (leaf_key_type, leaf_key_type),

        FUNCTION 1  temporal_consistent (internal, internal, smallint, oid, internal),
        FUNCTION 2  temporal_union (internal, internal),
        FUNCTION 3  temporal_compress (internal),
        FUNCTION 4  temporal_decompress (internal),
        FUNCTION 5  temporal_penalty (internal, internal, internal),
        FUNCTION 6  temporal_picksplit (internal, internal),
        FUNCTION 7  temporal_same (internal, internal, internal),

        STORAGE temporal_key_type;