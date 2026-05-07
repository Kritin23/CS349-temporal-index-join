-- ============================================================
-- temporal_join --1.0.sql
--
-- Both input tables MUST have two columns of these exact names
-- and types:
--     id        int
--     timerange tsrange
--
-- Usage:
--     SELECT * FROM temporal_join('A'::regclass, 'B'::regclass);
--
--  Semantically same as:
--     SELECT a.id, (a.timerange * b.timerange) AS common
--     FROM A a JOIN B b
--          ON a.id = b.id AND a.timerange && b.timerange;
--
-- ============================================================

CREATE FUNCTION temporal_join(left_tbl regclass, right_tbl regclass)
RETURNS TABLE(
    id     int,
    common tsrange
)
AS 'MODULE_PATHNAME', 'temporal_join'
LANGUAGE C STABLE STRICT;
