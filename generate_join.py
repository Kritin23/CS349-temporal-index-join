"""
Generate two test tables A and B for the temporal_join extension and
emit a queries.sql that:
  1. times the SRF (temporal_join)
  2. times the equivalent plain-SQL merge-join-with-filter
  3. checks correctness via symmetric set difference (EXCEPT both ways)

Schema is just (id int, timerange tsrange) — no payload column.

The data generator obeys the temporal-index property: intervals for a
single id never overlap within a single table. Achieved by laying
intervals down per id along a time cursor with a forced gap.

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


NUM_ROWS_A   = 10000          # target rows in A
NUM_ROWS_B   = 10000         # target rows in B
NUM_IDS      = 1          # distinct ids shared by both
SEED         = 7

# Time domain: 2026-01-01 + offset within span; intervals 30 min .. 3 days.
SPAN_DAYS         = 1
MIN_DUR_SEC       = 10        # 30 minutes
MAX_DUR_SEC       = 2000     # 3 days
MIN_GAP_SEC       = 10          # forced gap between consecutive intervals
                                # of the same id (keeps them non-overlapping)


def fmt(ts: datetime) -> str:
    return ts.strftime('%Y-%m-%d %H:%M:%S')


def gen_table(filename, table_name, num_rows, base_date, seed_offset):
    """Generate rows obeying the temporal-index property: per-id, the
    intervals are laid down sequentially along a per-id time cursor with
    forced gaps, so they never overlap within a single table.

    Distribution is approximate — we deal `num_rows` rows out across ids
    uniformly, then for each id place its intervals back-to-back with
    randomized durations and gaps."""
    rng = random.Random(SEED + seed_offset)
    span_seconds = SPAN_DAYS * 24 * 3600

    counts = [0] * (NUM_IDS + 1)
    for _ in range(num_rows):
        counts[rng.randint(1, NUM_IDS)] += 1

    rows = []
    for obj_id in range(1, NUM_IDS + 1):
        n_for_id = counts[obj_id]
        if n_for_id == 0:
            continue
        cursor = rng.randint(0, max(1, span_seconds // 4))
        for _ in range(n_for_id):
            dur = rng.randint(MIN_DUR_SEC, MAX_DUR_SEC)
            if cursor + dur > span_seconds:
                break
            t_start = base_date + timedelta(seconds=cursor)
            t_end   = t_start   + timedelta(seconds=dur)
            rows.append((obj_id, t_start, t_end))
            cursor += dur + rng.randint(MIN_GAP_SEC, MAX_DUR_SEC)

    rng.shuffle(rows)

    with open(filename, 'a') as f:
        f.write(f"-- ---- populating {table_name} ({len(rows)} rows) ----\n")
        f.write("BEGIN;\n")
        for i, (obj_id, t_start, t_end) in enumerate(rows, 1):
            f.write(
                f"INSERT INTO {table_name} (id, timerange) VALUES "
                f"({obj_id}, "
                f"tsrange('{fmt(t_start)}','{fmt(t_end)}','[)'));\n"
            )
            if i % 2000 == 0:
                f.write("COMMIT; BEGIN;\n")
        f.write("COMMIT;\n\n")


def main():
    base_date = datetime(2026, 1, 1)

    with open('schema_join.sql', 'w') as f:
        f.write("""\
-- Tables A and B for the temporal_join extension. Schema is fixed
-- (the SRF reads columns by name): id int, timerange tsrange.
DROP TABLE IF EXISTS A CASCADE;
DROP TABLE IF EXISTS B CASCADE;

CREATE TABLE A (id int, timerange tsrange);
CREATE TABLE B (id int, timerange tsrange);

CREATE INDEX a_id ON A (id);
CREATE INDEX b_id ON B (id);
""")

    open('data_join.sql', 'w').close()
    gen_table('data_join.sql', 'A', NUM_ROWS_A, base_date, seed_offset=0)
    gen_table('data_join.sql', 'B', NUM_ROWS_B, base_date, seed_offset=1)
    with open('data_join.sql', 'a') as f:
        f.write("ANALYZE A;\nANALYZE B;\n")

    with open('queries_join.sql', 'w') as f:
        f.write("""\
-- ============================================================
-- temporal_join: SRF vs plain SQL  (correctness + timing)
-- ============================================================

CREATE EXTENSION IF NOT EXISTS temporal_join;

-- 1. SANITY: row counts on both inputs
SELECT 'A' AS rel, COUNT(*) FROM A
UNION ALL
SELECT 'B', COUNT(*) FROM B;

-- 2. CORRECTNESS: SRF result == plain JOIN result (as sets).
--    Both EXCEPT counts must be zero.
WITH srf AS (
    SELECT id, common
    FROM temporal_join('A'::regclass, 'B'::regclass)
),
plain AS (
    SELECT a.id, (a.timerange * b.timerange) AS common
    FROM A a JOIN B b
      ON a.id = b.id AND a.timerange && b.timerange
)
SELECT
    (SELECT COUNT(*) FROM srf)                                            AS srf_rows,
    (SELECT COUNT(*) FROM plain)                                          AS plain_rows,
    (SELECT COUNT(*) FROM (SELECT * FROM srf   EXCEPT SELECT * FROM plain) d) AS srf_minus_plain,
    (SELECT COUNT(*) FROM (SELECT * FROM plain EXCEPT SELECT * FROM srf  ) d) AS plain_minus_srf,
    CASE
      WHEN (SELECT COUNT(*) FROM (SELECT * FROM srf   EXCEPT SELECT * FROM plain) d) = 0
       AND (SELECT COUNT(*) FROM (SELECT * FROM plain EXCEPT SELECT * FROM srf  ) d) = 0
      THEN 'PASS' ELSE 'FAIL'
    END AS status;

-- 3. TIMING: SRF (run 3x; first pays for cold caches)
\\timing on
SELECT COUNT(*) FROM temporal_join('A'::regclass, 'B'::regclass);
SELECT COUNT(*) FROM temporal_join('A'::regclass, 'B'::regclass);
SELECT COUNT(*) FROM temporal_join('A'::regclass, 'B'::regclass);
\\timing off

-- 4. TIMING: plain SQL merge-join + filter
\\timing on
SELECT COUNT(*) FROM A a JOIN B b
  ON a.id = b.id AND a.timerange && b.timerange;
SELECT COUNT(*) FROM A a JOIN B b
  ON a.id = b.id AND a.timerange && b.timerange;
SELECT COUNT(*) FROM A a JOIN B b
  ON a.id = b.id AND a.timerange && b.timerange;
\\timing off

-- 5. EXPLAIN (ANALYZE, BUFFERS) for both
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM temporal_join('A'::regclass, 'B'::regclass);

EXPLAIN (ANALYZE, BUFFERS)
SELECT a.id, (a.timerange * b.timerange) AS common
FROM A a JOIN B b
  ON a.id = b.id AND a.timerange && b.timerange;

-- 6. Spill diagnostic — if these grew, the sorts spilled to disk.
SELECT temp_files, pg_size_pretty(temp_bytes) AS temp_bytes
FROM pg_stat_database
WHERE datname = current_database();

-- 7. Sample output (first 10 rows of the SRF)
SELECT id, common
FROM temporal_join('A'::regclass, 'B'::regclass)
ORDER BY id, lower(common)
LIMIT 10;
""")

    print(f"Wrote schema_join.sql, data_join.sql, queries_join.sql")
    print(f"  A: target {NUM_ROWS_A} rows, B: target {NUM_ROWS_B} rows, "
          f"{NUM_IDS} ids, seed={SEED}")
    print(f"  Note: actual row counts may be slightly less than targets "
          f"because per-id intervals get truncated when the time cursor "
          f"approaches the end of the span.")


if __name__ == "__main__":
    main()
