"""
Generate two test tables A and B for the temporal_join extension and
emit a queries.sql that:
  1. times the SRF (temporal_join)
  2. times the equivalent plain-SQL merge-join-with-filter
  3. checks correctness via symmetric set difference (EXCEPT both ways)

Outputs three files:
    schema_join.sql
    data_join.sql
    queries_join.sql

Run:
    python3 generate_join.py
    psql -U postgres -h localhost -v ON_ERROR_STOP=1 \\
         -f schema_join.sql -f data_join.sql -f queries_join.sql
"""

import random
from datetime import datetime, timedelta


NUM_ROWS_A   = 50_000      # rows in A
NUM_ROWS_B   = 50_000      # rows in B
NUM_IDS      = 500         # distinct ids shared by both
SEED         = 7

# Time domain: 2026-01-01 + offset within span; intervals 30 min .. 3 days.
SPAN_DAYS         = 365
MIN_DUR_SEC       = 1800        # 30 minutes
MAX_DUR_SEC       = 259_200     # 3 days


def fmt(ts: datetime) -> str:
    return ts.strftime('%Y-%m-%d %H:%M:%S')


def gen_table(filename, table_name, num_rows, base_date, seed_offset):
    rng = random.Random(SEED + seed_offset)
    span_seconds = SPAN_DAYS * 24 * 3600
    with open(filename, 'a') as f:
        f.write(f"-- ---- populating {table_name} ({num_rows} rows) ----\n")
        f.write("BEGIN;\n")
        for i in range(1, num_rows + 1):
            obj_id  = rng.randint(1, NUM_IDS)
            start_o = rng.randint(0, span_seconds - MAX_DUR_SEC)
            dur     = rng.randint(MIN_DUR_SEC, MAX_DUR_SEC)
            t_start = base_date + timedelta(seconds=start_o)
            t_end   = t_start   + timedelta(seconds=dur)
            data_v  = rng.randint(1, 1000)
            f.write(
                f"INSERT INTO {table_name} (id, timerange, data) VALUES "
                f"({obj_id}, "
                f"tsrange('{fmt(t_start)}','{fmt(t_end)}','[)'), "
                f"{data_v});\n"
            )
            if i % 2000 == 0:
                f.write("COMMIT; BEGIN;\n")
        f.write("COMMIT;\n\n")


def main():
    base_date = datetime(2026, 1, 1)

    # ---------- schema_join.sql ----------
    with open('schema_join.sql', 'w') as f:
        f.write("""\
-- Tables A and B for the temporal_join extension. Schema is fixed
-- (the SRF reads columns by name): id int, timerange tsrange, data int.
DROP TABLE IF EXISTS A CASCADE;
DROP TABLE IF EXISTS B CASCADE;

CREATE TABLE A (id int, timerange tsrange, data int);
CREATE TABLE B (id int, timerange tsrange, data int);

-- btree on id helps the baseline merge-join.
CREATE INDEX a_id ON A (id);
CREATE INDEX b_id ON B (id);

-- (Optional) GiST on (id, timerange) — requires btree_gist.
-- CREATE EXTENSION IF NOT EXISTS btree_gist;
-- CREATE INDEX a_gist ON A USING gist (id, timerange);
-- CREATE INDEX b_gist ON B USING gist (id, timerange);
""")

    # ---------- data_join.sql ----------
    open('data_join.sql', 'w').close()        # truncate
    gen_table('data_join.sql', 'A', NUM_ROWS_A, base_date, seed_offset=0)
    gen_table('data_join.sql', 'B', NUM_ROWS_B, base_date, seed_offset=1)
    with open('data_join.sql', 'a') as f:
        f.write("ANALYZE A;\nANALYZE B;\n")

    # ---------- queries_join.sql ----------
    with open('queries_join.sql', 'w') as f:
        f.write("""\
-- ============================================================
-- temporal_join: SRF vs plain SQL  (correctness + timing)
-- ============================================================

-- Make sure the extension is installed.
CREATE EXTENSION IF NOT EXISTS temporal_join;

-- Spill ceiling per sort. Bump if you load very large datasets.
-- SET work_mem = '128MB';

-- ------------------------------------------------------------
-- 1. SANITY: row counts on both inputs
-- ------------------------------------------------------------
SELECT 'A' AS rel, COUNT(*) FROM A
UNION ALL
SELECT 'B', COUNT(*) FROM B;

-- ------------------------------------------------------------
-- 2. CORRECTNESS: SRF result == plain JOIN result (as sets)
--    Both EXCEPT counts must be zero.
-- ------------------------------------------------------------
WITH srf AS (
    SELECT a_id, a_data, b_data, a_start, a_end, b_start, b_end
    FROM temporal_join('A'::regclass, 'B'::regclass)
),
plain AS (
    SELECT a.id        AS a_id,
           a.data      AS a_data,
           b.data      AS b_data,
           lower(a.timerange) AS a_start,
           upper(a.timerange) AS a_end,
           lower(b.timerange) AS b_start,
           upper(b.timerange) AS b_end
    FROM A a JOIN B b
      ON a.id = b.id AND a.timerange && b.timerange
)
SELECT
    (SELECT COUNT(*) FROM srf)                                          AS srf_rows,
    (SELECT COUNT(*) FROM plain)                                        AS plain_rows,
    (SELECT COUNT(*) FROM (SELECT * FROM srf   EXCEPT SELECT * FROM plain) d) AS srf_minus_plain,
    (SELECT COUNT(*) FROM (SELECT * FROM plain EXCEPT SELECT * FROM srf  ) d) AS plain_minus_srf,
    CASE
      WHEN (SELECT COUNT(*) FROM (SELECT * FROM srf   EXCEPT SELECT * FROM plain) d) = 0
       AND (SELECT COUNT(*) FROM (SELECT * FROM plain EXCEPT SELECT * FROM srf  ) d) = 0
      THEN 'PASS' ELSE 'FAIL'
    END AS status;

-- ------------------------------------------------------------
-- 3. TIMING: SRF (run 3x, look at the last; first pays for cold caches)
-- ------------------------------------------------------------
\\timing on
SELECT COUNT(*) FROM temporal_join('A'::regclass, 'B'::regclass);
SELECT COUNT(*) FROM temporal_join('A'::regclass, 'B'::regclass);
SELECT COUNT(*) FROM temporal_join('A'::regclass, 'B'::regclass);
\\timing off

-- ------------------------------------------------------------
-- 4. TIMING: plain SQL merge-join + filter
-- ------------------------------------------------------------
\\timing on
SELECT COUNT(*) FROM A a JOIN B b
  ON a.id = b.id AND a.timerange && b.timerange;
SELECT COUNT(*) FROM A a JOIN B b
  ON a.id = b.id AND a.timerange && b.timerange;
SELECT COUNT(*) FROM A a JOIN B b
  ON a.id = b.id AND a.timerange && b.timerange;
\\timing off

-- ------------------------------------------------------------
-- 5. EXPLAIN (ANALYZE, BUFFERS) for both
-- ------------------------------------------------------------
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM temporal_join('A'::regclass, 'B'::regclass);

EXPLAIN (ANALYZE, BUFFERS)
SELECT a.id, a.data, b.data,
       lower(a.timerange), upper(a.timerange),
       lower(b.timerange), upper(b.timerange)
FROM A a JOIN B b
  ON a.id = b.id AND a.timerange && b.timerange;

-- ------------------------------------------------------------
-- 6. Spill diagnostic — if these grew, the sorts spilled to disk.
-- ------------------------------------------------------------
SELECT temp_files, pg_size_pretty(temp_bytes) AS temp_bytes
FROM pg_stat_database
WHERE datname = current_database();
""")

    print(f"Wrote schema_join.sql, data_join.sql, queries_join.sql")
    print(f"  A: {NUM_ROWS_A} rows, B: {NUM_ROWS_B} rows, "
          f"{NUM_IDS} ids, seed={SEED}")


if __name__ == "__main__":
    main()
