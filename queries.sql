-- =========================================================================
-- TEMPORAL INDEX PERFORMANCE BENCHMARK SUITE
-- =========================================================================

-- 1. SETUP COMPETING INDEXES
-- Ensure both the GiST and standard B-Tree indexes exist.
CREATE INDEX IF NOT EXISTS idx_btree_test ON idx_test (id, start_time, end_time);
ANALYZE idx_test;

-- Turn on psql timing to get raw millisecond outputs
\timing on

-- =========================================================================
-- 2. PLANNER TOGGLES (Change these for each of your 4 test runs)
-- =========================================================================

-- RUN 1: Force Sequential Scan (The Baseline)
-- SET enable_seqscan = on;
-- SET enable_indexscan = off;
-- SET enable_bitmapscan = off;

-- RUN 2: Force Composite B-Tree 
-- SET enable_seqscan = off;
-- SET enable_indexscan = on;
-- SET enable_bitmapscan = on;

-- RUN 3: Force GiST (Bitmap Heap Scan)
-- SET enable_seqscan = off;
-- SET enable_indexscan = off;
-- SET enable_bitmapscan = on;

-- RUN 4: Force GiST (Pure Index Scan)
-- SET enable_seqscan = off;
-- SET enable_indexscan = on;
-- SET enable_bitmapscan = off;

-- =========================================================================
-- 3. THE TEST SUITE
-- =========================================================================

\echo '-------------------------------------------------------'
\echo 'TEST 1: High Selectivity (Point-in-Time Lookup)'
\echo 'Goal: Find one specific ID at an exact microsecond.'
\echo 'Hypothesis: B-Tree and GiST will both be extremely fast.'
\echo '-------------------------------------------------------'

\echo '>>> 1A. GiST Query'
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM idx_test 
WHERE temporal_key(id, start_time, end_time) && temporal_point(8, '2026-06-10 12:00:00');

\echo '>>> 1B. Equivalent B-Tree Query'
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM idx_test 
WHERE id = 8 
  AND start_time <= '2026-06-10 12:00:00' 
  AND end_time >= '2026-06-10 12:00:00';


\echo '-------------------------------------------------------'
\echo 'TEST 2: 2D Bounding Box (Range Overlap on Specific ID)'
\echo 'Goal: Find an entity overlapping a specific month.'
\echo 'Hypothesis: GiST begins to outperform B-Tree due to 2D spatial indexing.'
\echo '-------------------------------------------------------'

\echo '>>> 2A. GiST Query'
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM idx_test 
WHERE temporal_key(id, start_time, end_time) && temporal_range(42, '2026-01-01 00:00:00', '2026-01-31 23:59:59');

\echo '>>> 2B. Equivalent B-Tree Query'
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM idx_test 
WHERE id = 42 
  AND start_time <= '2026-01-31 23:59:59' 
  AND end_time >= '2026-01-01 00:00:00';


\echo '-------------------------------------------------------'
\echo 'TEST 3: The Fatal B-Tree Flaw (Pure Time Slice)'
\echo 'Goal: Find ALL entities active during a specific week.'
\echo 'Hypothesis: B-Tree will completely fail and force a Seq Scan because the leading column (id) is missing.'
\echo '-------------------------------------------------------'

\echo '>>> 3A. GiST Query'
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM idx_test 
WHERE temporal_key(id, start_time, end_time) && temporal_time_range('2026-05-01 00:00:00', '2026-05-07 23:59:59');

\echo '>>> 3B. Equivalent B-Tree Query'
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM idx_test 
WHERE start_time <= '2026-05-07 23:59:59' 
  AND end_time >= '2026-05-01 00:00:00';


\echo '-------------------------------------------------------'
\echo 'TEST 4: Strict Containment (@>)'
\echo 'Goal: Find records that completely envelop a target window.'
\echo '-------------------------------------------------------'

\echo '>>> 4A. GiST Query'
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM idx_test 
WHERE temporal_key(id, start_time, end_time) @> temporal_range(99, '2026-06-01 00:00:00', '2026-06-07 23:59:59');

\echo '>>> 4B. Equivalent B-Tree Query'
EXPLAIN (ANALYZE, BUFFERS)
SELECT * FROM idx_test 
WHERE id = 99 
  AND start_time <= '2026-06-01 00:00:00' 
  AND end_time >= '2026-06-07 23:59:59';