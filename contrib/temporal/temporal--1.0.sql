CREATE TYPE temporal_key_type (
    internallength = 24,
    input = temporal_in,
    output = temporal_out,
    alignment = double, 
    storage = plain
);

CREATE TYPE leaf_key_type (
    internallength = 24,
    input = leaf_in,
    output = leaf_out,
    alignment = double,
    storage = plain
);

CREATE TYPE idx_range_query (
    internallength = 24,
    input = idx_range_in,  
    output = idx_range_out,
    alignment = double
);

CREATE TYPE time_itv_query (
    internallength = 16,
    input = time_itv_in,
    output = time_itv_out,
    alignment = double
);

CREATE TYPE idx_point_query (
    internallength = 16,
    input = idx_point_in,
    output = idx_point_out,
    alignment = double
);

-- CREAT 

CREATE OPERATOR && (
    LEFTARG = leaf_key_type, RIGHTARG = time_itv_query,
    PROCEDURE = itv_consistent_overlap_range
);

CREATE OPERATOR && (
    LEFTARG = leaf_key_type, RIGHTARG = timestamp,
    PROCEDURE = itv_consistent_overlap_point
);

CREATE OPERATOR && (
    LEFTARG = leaf_key_type, RIGHTARG = idx_range_query,
    PROCEDURE = itv_range_overlap_bool
);

CREATE OPERATOR && (
    LEFTARG = leaf_key_type, RIGHTARG = idx_point_query,
    PROCEDURE = itv_point_overlap_bool
);



CREATE OPERATOR <@ (
    LEFTARG = leaf_key_type, RIGHTARG = time_itv_query,
    PROCEDURE = itv_consistent_overlap_range
);

CREATE OPERATOR <@ (
    LEFTARG = leaf_key_type, RIGHTARG = timestamp,
    PROCEDURE = itv_consistent_overlap_point
);

CREATE OPERATOR <@ (
    LEFTARG = leaf_key_type, RIGHTARG = idx_range_query,
    PROCEDURE = itv_range_overlap_bool
);

CREATE OPERATOR <@ (
    LEFTARG = leaf_key_type, RIGHTARG = idx_point_query,
    PROCEDURE = itv_point_overlap_bool
);



CREATE OPERATOR @> (
    LEFTARG = leaf_key_type, RIGHTARG = time_itv_query,
    PROCEDURE = itv_consistent_overlap_range
);

CREATE OPERATOR @> (
    LEFTARG = leaf_key_type, RIGHTARG = timestamp,
    PROCEDURE = itv_consistent_overlap_point
);

CREATE OPERATOR @> (
    LEFTARG = leaf_key_type, RIGHTARG = idx_range_query,
    PROCEDURE = itv_range_overlap_bool
);

CREATE OPERATOR @> (
    LEFTARG = leaf_key_type, RIGHTARG = idx_point_query,
    PROCEDURE = itv_point_overlap_bool
);




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

        FUNCTION 1  temporal_consistent (internal, internal, smallint, oid, internal),
        FUNCTION 2  temporal_union (internal, internal),
        FUNCTION 3  temporal_compress (internal),
        FUNCTION 4  temporal_decompress (internal),
        FUNCTION 5  temporal_penalty (internal, internal, internal),
        FUNCTION 6  temporal_picksplit (internal, internal),
        FUNCTION 7  temporal_same (internal, internal, internal),

        STORAGE temporal_key_type;