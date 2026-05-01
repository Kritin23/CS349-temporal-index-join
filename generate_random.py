import random
from datetime import datetime, timedelta


NUM_ROWS    = 100_000     # total rows in idx_test
NUM_IDS     = 1_000       # distinct ids — gives ~NUM_ROWS / NUM_IDS rows per id
NUM_QUERIES = 8           # number of paired benchmark queries to emit
SEED        = 42          # reproducibility


def generate(num_rows=NUM_ROWS, num_ids=NUM_IDS,
             num_queries=NUM_QUERIES, seed=SEED):
    random.seed(seed)
    base_date = datetime(2026, 1, 1)
    span_seconds = 365 * 24 * 3600          # one year of data
    min_event_sec, max_event_sec = 1800, 259200   # 30 min .. 3 days

    # ------------------------------------------------------------ schema
    with open('schema.sql', 'w') as f:
        f.write("DROP TABLE IF EXISTS idx_test CASCADE;\n\n")
        # f.write("DROP EXTENSION temporal_agg CASCADE;\n\n")
        f.write("CREATE TABLE idx_test (\n")
        f.write("    id          int,\n")
        f.write("    data        bigint,\n")
        f.write("    start_time  timestamp,\n")
        f.write("    end_time    timestamp\n")
        f.write(");\n\n")

        # The aggregate-augmented GiST index used by seg_minmax_v2.
        f.write("CREATE INDEX idx_temporal_agg ON idx_test\n")
        f.write("USING gist (agg_leaf(id, start_time, end_time, data));\n\n")

        # A plain btree to give the 'normal SQL' path a fair shot.
        f.write("CREATE INDEX idx_btree ON idx_test (id, start_time)\n")
        f.write("INCLUDE (end_time, data);\n")

    # ------------------------------------------------------------ data
    with open('data.sql', 'w') as f:
        f.write("BEGIN;\n")
        for i in range(1, num_rows + 1):
            obj_id = random.randint(1, num_ids)
            start_offset = random.randint(0, span_seconds)
            duration     = random.randint(min_event_sec, max_event_sec)
            start_ts = base_date + timedelta(seconds=start_offset)
            end_ts   = start_ts  + timedelta(seconds=duration)
            data_val = random.randint(1, 100)
            f.write(
                f"INSERT INTO idx_test (id, data, start_time, end_time) "
                f"VALUES ({obj_id}, {data_val}, "
                f"'{start_ts}', '{end_ts}');\n"
            )
            if i % 2000 == 0:
                f.write("COMMIT; BEGIN;\n")
        f.write("COMMIT;\n")
        f.write("ANALYZE idx_test;\n")

    # ------------------------------------------------------------ queries
    with open('queries.sql', 'w') as f:
        f.write("-- " + "=" * 56 + "\n")
        f.write("-- temporal_agg benchmark: extension vs plain SQL\n")
        f.write("-- Each query block runs both paths so timings can be\n")
        f.write("-- compared on the same id and time window.\n")
        f.write("-- " + "=" * 56 + "\n\n")

        # A spread of selectivities: narrow .. wide time windows.
        windows_days = [1, 7, 30, 90, 180]

        for q in range(num_queries):
            test_id = random.randint(1, num_ids)
            window  = random.choice(windows_days)
            start_offset = random.randint(0, span_seconds - window * 86400)
            t_start = base_date + timedelta(seconds=start_offset)
            t_end   = t_start   + timedelta(days=window)

            f.write(f"-- ---- Query {q + 1}: id={test_id}, "
                    f"window={window}d "
                    f"[{t_start} .. {t_end}] ----\n")

            # 1. extension path
            f.write("-- extension: seg_minmax_v2 via aggregate-GiST walk\n")
            f.write("EXPLAIN (ANALYZE, BUFFERS)\n")
            f.write(
                f"SELECT seg_minmax_v2('idx_temporal_agg'::regclass, "
                f"{test_id}, '{t_start}'::timestamp, "
                f"'{t_end}'::timestamp);\n\n"
            )

            # 2. plain SQL path (uses idx_btree)
            f.write("-- plain SQL: MIN/MAX with btree on (id, start_time)\n")
            f.write("EXPLAIN (ANALYZE, BUFFERS)\n")
            f.write("SELECT MIN(data), MAX(data), SUM(data), COUNT(data) FROM idx_test\n")
            f.write(f" WHERE id = {test_id}\n")
            f.write(f"   AND start_time <= '{t_end}'::timestamp\n")
            f.write(f"   AND end_time   >= '{t_start}'::timestamp;\n\n")

            # 3. correctness check — both paths should agree
            # NOTE: SUM(bigint) returns numeric in PostgreSQL, so cast it to
            # bigint to keep the ARRAY homogeneous (otherwise it resolves to
            # numeric[] and won't compare cleanly with the extension's
            # bigint[] result).
            f.write("-- Correctness Check\n")
            f.write("SELECT\n")
            f.write("  ext,\n")
            f.write("  plain,\n")
            f.write("  CASE WHEN ext IS NOT DISTINCT FROM plain "
                    "THEN 'PASS' ELSE 'FAIL' END AS status\n")
            f.write("FROM (\n")
            f.write(f"  SELECT seg_minmax_v2('idx_temporal_agg'::regclass, "
                    f"{test_id}, '{t_start}', '{t_end}') AS ext,\n")
            f.write(f"  ARRAY[\n")
            f.write(f"    (SELECT MIN(data)         FROM idx_test "
                    f"WHERE id = {test_id} "
                    f"AND start_time <= '{t_end}' "
                    f"AND end_time   >= '{t_start}'),\n")
            f.write(f"    (SELECT MAX(data)         FROM idx_test "
                    f"WHERE id = {test_id} "
                    f"AND start_time <= '{t_end}' "
                    f"AND end_time   >= '{t_start}'),\n")
            f.write(f"    (SELECT SUM(data)::bigint FROM idx_test "
                    f"WHERE id = {test_id} "
                    f"AND start_time <= '{t_end}' "
                    f"AND end_time   >= '{t_start}'),\n")
            f.write(f"    (SELECT COUNT(data)       FROM idx_test "
                    f"WHERE id = {test_id} "
                    f"AND start_time <= '{t_end}' "
                    f"AND end_time   >= '{t_start}')\n")
            f.write(f"  ] AS plain\n")
            f.write(") validation;\n\n")

    print(f"Wrote schema.sql, data.sql, queries.sql "
          f"({num_rows} rows, {num_ids} ids, {num_queries} queries).")


if __name__ == "__main__":
    generate()
