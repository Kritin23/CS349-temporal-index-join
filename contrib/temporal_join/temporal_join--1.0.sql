-- ============================================================
-- temporal_join --1.0.sql
--
-- Streaming sort-merge interval join for temporal data.
--
-- Both input tables MUST expose two columns of these exact names
-- and types:
--     id        int
--     timerange tsrange
--
-- Assumed temporal-index property: for any single id, intervals within
-- the same table do not overlap (at most one entry per id at any moment).
-- Algorithm is correct without this property; the property merely keeps
-- the sweep's active sets bounded by 1 per side.
--
-- Usage:
--     SELECT * FROM temporal_join('A'::regclass, 'B'::regclass);
--
-- Behaves equivalently to:
--     SELECT a.id, (a.timerange * b.timerange) AS common
--     FROM A a JOIN B b
--          ON a.id = b.id AND a.timerange && b.timerange;
--
-- Both sorts and the output spill to per-backend temp files when
-- they exceed work_mem; arbitrarily large inputs and outputs work.
-- ============================================================

CREATE FUNCTION temporal_join(left_tbl regclass, right_tbl regclass)
RETURNS TABLE(
    id     int,
    common tsrange
)
AS 'MODULE_PATHNAME', 'temporal_join'
LANGUAGE C STABLE STRICT;
