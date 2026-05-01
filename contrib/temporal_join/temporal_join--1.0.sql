-- ============================================================
-- temporal_join --1.0.sql
--
-- Streaming sort-merge interval join for temporal data.
--
-- Both input tables MUST expose three columns of these exact names
-- and types:
--     id        int
--     timerange tsrange
--     data      int
--
-- Usage:
--     SELECT * FROM temporal_join('A'::regclass, 'B'::regclass);
--
-- Behaves equivalently to:
--     SELECT a.id, a.data, b.data,
--            lower(a.timerange), upper(a.timerange),
--            lower(b.timerange), upper(b.timerange)
--     FROM A a JOIN B b
--          ON a.id = b.id AND a.timerange && b.timerange;
--
-- but uses a single-pass sweep on inputs sorted by (id, lower(timerange))
-- instead of nested-loop-with-filter.
--
-- Both sorts and the output spill to per-backend temp files when
-- they exceed work_mem; arbitrarily large inputs and outputs work.
-- ============================================================

CREATE FUNCTION temporal_join(left_tbl regclass, right_tbl regclass)
RETURNS TABLE(
    a_id    int,
    a_data  int,
    b_data  int,
    a_start timestamp,
    a_end   timestamp,
    b_start timestamp,
    b_end   timestamp
)
AS 'MODULE_PATHNAME', 'temporal_join'
LANGUAGE C STABLE STRICT;
