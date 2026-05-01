/*
 * contrib/temporal_join/temporal_join.c
 *
 * Streaming sort-merge interval join for temporal data.
 *
 *   SELECT * FROM temporal_join('A'::regclass, 'B'::regclass);
 *
 * Both input tables are required to expose three columns
 *   id        int
 *   timerange tsrange
 *   data      int
 * (the column names matter; types must be exactly these).
 *
 * Algorithm:
 *   1. Pull each table through SPI cursors (row-at-a-time, no full
 *      materialization in memory).
 *   2. Push every row into a Tuplesortstate keyed (id, lower(timerange)).
 *      Tuplesort spills to per-backend temp files when work_mem is exceeded.
 *   3. Stream both sorts in lockstep ("opening" event = next row from
 *      whichever side has the smaller (id, lower)).
 *   4. Maintain two active sets — intervals of each side that have
 *      opened but not yet closed (i.e. upper >= current sweep point and
 *      same id). On each opening, emit pairs against the opposite-side
 *      active set; that's the join output.
 *
 * Memory ceiling: 2 * work_mem (one per tuplesort) + work_mem (output
 * tuplestore) + active-set sizes. Active sets are bounded by the maximum
 * number of intervals overlapping a single point within one id, never by
 * |A| or |B|. Inputs of any size work — sorts spill, output spills.
 *
 * Cost: O((|A|+|B|) log (|A|+|B|) + output) plus disk I/O if spilled.
 */

#include "postgres.h"
#include "fmgr.h"

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "executor/executor.h"
#include "executor/spi.h"
#include "executor/tuptable.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
<<<<<<< HEAD
#include "utils/rel.h"
#include "utils/timestamp.h"
#include "utils/tuplesort.h"
=======
#include "utils/rangetypes.h"
#include "utils/rel.h"
#include "utils/timestamp.h"
#include "utils/tuplesort.h"
#include "utils/typcache.h"
>>>>>>> main

PG_MODULE_MAGIC;

/* Operator OIDs for the sort keys.
 * int4 < int4         : pg_operator.oid = 97
 * timestamp < timestamp : pg_operator.oid = 2062
 * (Hardcoded — these are stable since the dawn of time.) */
#define INT4_LT_OP        97
#define TIMESTAMP_LT_OP   2062


/* ====================================================================== */
/* Row buffer + active-set helpers                                        */
/* ====================================================================== */

typedef struct InRow {
    int32     id;
    Timestamp lo;
    Timestamp hi;
<<<<<<< HEAD
    int32     data;
=======
>>>>>>> main
} InRow;

typedef struct ActiveSet {
    InRow *items;
    int    n;
    int    cap;
} ActiveSet;

static void
as_init(ActiveSet *s)
{
    s->items = NULL;
    s->n = 0;
    s->cap = 0;
}

static void
as_clear(ActiveSet *s)
{
    s->n = 0;
}

static void
as_free(ActiveSet *s)
{
    if (s->items) pfree(s->items);
    s->items = NULL;
    s->n = s->cap = 0;
}

static void
as_add(ActiveSet *s, const InRow *r)
{
    if (s->n == s->cap)
    {
        int newcap = s->cap ? s->cap * 2 : 16;
        if (s->items)
            s->items = (InRow *) repalloc(s->items, newcap * sizeof(InRow));
        else
            s->items = (InRow *) palloc(newcap * sizeof(InRow));
        s->cap = newcap;
    }
    s->items[s->n++] = *r;
}

<<<<<<< HEAD
/* Drop entries whose interval has already closed before `cutoff`. */
=======
/* Drop entries whose interval has already closed before (or at) `cutoff`.
 * Half-open semantics ('[)' canonical form): an interval [lo, hi) is OPEN
 * at time t iff lo <= t < hi. So at the sweep point `cutoff`, only
 * entries with hi > cutoff (strict) are still active. Using >= here
 * would treat endpoints as inclusive and emit spurious pairs whenever
 * one interval ends exactly where another starts. */
>>>>>>> main
static void
as_prune(ActiveSet *s, Timestamp cutoff)
{
    int w = 0;
    for (int i = 0; i < s->n; i++)
<<<<<<< HEAD
        if (s->items[i].hi >= cutoff)
=======
        if (s->items[i].hi > cutoff)
>>>>>>> main
            s->items[w++] = s->items[i];
    s->n = w;
}


/* ====================================================================== */
/* Output emission                                                        */
/* ====================================================================== */

<<<<<<< HEAD
static void
emit_pair(Tuplestorestate *out, TupleDesc desc,
          const InRow *a, const InRow *b)
{
    Datum vals[7];
    bool  nulls[7] = { false, false, false, false, false, false, false };

    vals[0] = Int32GetDatum(a->id);
    vals[1] = Int32GetDatum(a->data);
    vals[2] = Int32GetDatum(b->data);
    vals[3] = TimestampGetDatum(a->lo);
    vals[4] = TimestampGetDatum(a->hi);
    vals[5] = TimestampGetDatum(b->lo);
    vals[6] = TimestampGetDatum(b->hi);
=======
/* Build a tsrange '[common_lo, common_hi)' for the intersection of two
 * intervals known to overlap. */
static RangeType *
build_common_range(TypeCacheEntry *typcache, Timestamp lo, Timestamp hi)
{
    RangeBound lower, upper;
    lower.val       = TimestampGetDatum(lo);
    lower.infinite  = false;
    lower.inclusive = true;
    lower.lower     = true;
    upper.val       = TimestampGetDatum(hi);
    upper.infinite  = false;
    upper.inclusive = false;          /* match '[)' canonical form */
    upper.lower     = false;
    return make_range(typcache, &lower, &upper, false, NULL);
}

static void
emit_pair(Tuplestorestate *out, TupleDesc desc,
          TypeCacheEntry *typcache,
          const InRow *a, const InRow *b)
{
    Datum vals[2];
    bool  nulls[2] = { false, false };

    Timestamp common_lo = (a->lo > b->lo) ? a->lo : b->lo;
    Timestamp common_hi = (a->hi < b->hi) ? a->hi : b->hi;

    vals[0] = Int32GetDatum(a->id);
    vals[1] = PointerGetDatum(build_common_range(typcache, common_lo, common_hi));
>>>>>>> main

    tuplestore_putvalues(out, desc, vals, nulls);
}


/* ====================================================================== */
/* SPI cursor → Tuplesortstate streaming                                  */
/* ====================================================================== */

static void
stream_into_sort(const char *sql,
                 Tuplesortstate *sort,
                 TupleTableSlot *inslot)
{
    Portal p;

    p = SPI_cursor_open_with_args(NULL, sql,
                                  0, NULL, NULL, NULL,
                                  true /* read_only */, 0);

    for (;;)
    {
        SPI_cursor_fetch(p, true /* forward */, 1000);
        if (SPI_processed == 0)
            break;

        for (uint64 i = 0; i < SPI_processed; i++)
        {
            HeapTuple ht = SPI_tuptable->vals[i];
            ExecClearTuple(inslot);
            ExecStoreHeapTuple(ht, inslot, false /* shouldFree */);
            tuplesort_puttupleslot(sort, inslot);
        }
        SPI_freetuptable(SPI_tuptable);
    }
    SPI_cursor_close(p);
}


/* ====================================================================== */
/* Tuplesort → InRow                                                      */
/* ====================================================================== */

static bool
read_row(Tuplesortstate *sort, TupleTableSlot *slot, InRow *out)
{
    if (!tuplesort_gettupleslot(sort, true /* forward */, false /* copy */,
                                slot, NULL))
        return false;

    slot_getallattrs(slot);
<<<<<<< HEAD
    /* All four attrs are NOT NULL by SQL contract; we don't bother
     * checking tts_isnull here. If the source has NULLs, sort order is
     * still well-defined and we just propagate possibly-zero values. */
    out->id   = DatumGetInt32(slot->tts_values[0]);
    out->lo   = DatumGetTimestamp(slot->tts_values[1]);
    out->hi   = DatumGetTimestamp(slot->tts_values[2]);
    out->data = DatumGetInt32(slot->tts_values[3]);
=======
    /* All three attrs are NOT NULL by SQL contract. If the source has
     * NULLs, sort order is still well-defined; we don't filter here. */
    out->id = DatumGetInt32(slot->tts_values[0]);
    out->lo = DatumGetTimestamp(slot->tts_values[1]);
    out->hi = DatumGetTimestamp(slot->tts_values[2]);
>>>>>>> main

    return true;
}


/* ====================================================================== */
/* Build the SPI projection SQL for a given relation                      */
/* ====================================================================== */

static char *
build_projection_sql(Oid relOid)
{
    char *nspname = get_namespace_name(get_rel_namespace(relOid));
    char *relname = get_rel_name(relOid);
    const char *qual;

    if (!nspname || !relname)
        elog(ERROR, "temporal_join: cannot resolve relation %u", relOid);

    qual = quote_qualified_identifier(nspname, relname);

<<<<<<< HEAD
    /* Required column names: id, timerange, data.
=======
    /* Required column names: id, timerange.
>>>>>>> main
     * lower()/upper() return timestamp; ::int4 cast is defensive. */
    return psprintf(
        "SELECT id::int4, "
        "lower(timerange)::timestamp, "
<<<<<<< HEAD
        "upper(timerange)::timestamp, "
        "data::int4 "
=======
        "upper(timerange)::timestamp "
>>>>>>> main
        "FROM %s",
        qual);
}


/* ====================================================================== */
/* Main SRF                                                                */
/* ====================================================================== */

PG_FUNCTION_INFO_V1(temporal_join);

Datum
temporal_join(PG_FUNCTION_ARGS)
{
    Oid leftOid  = PG_GETARG_OID(0);
    Oid rightOid = PG_GETARG_OID(1);

    ReturnSetInfo   *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;
    TupleDesc        outDesc;
    Tuplestorestate *outStore;

    TupleDesc        inDesc;
    TupleTableSlot  *putslot;       /* heap-tuple ops, for SPI -> sort */
    TupleTableSlot  *getslot;       /* minimal-tuple ops, for sort -> sweep */

    Tuplesortstate  *aSort;
    Tuplesortstate  *bSort;

    AttrNumber       sortKeys[2]   = { 1, 2 };          /* id, lo */
    Oid              sortOps[2]    = { INT4_LT_OP, TIMESTAMP_LT_OP };
    Oid              sortColls[2]  = { InvalidOid, InvalidOid };
    bool             nullsFirst[2] = { false, false };

    InRow            aCur, bCur;
    bool             aValid, bValid;
    int32            curId;
    ActiveSet        activeA, activeB;
    TypeCacheEntry  *tsrangeTypcache;

    char *aSql;
    char *bSql;

    /* --- 1. Set up the SRF return tuplestore (uses work_mem; spills) --- */
    InitMaterializedSRF(fcinfo, MAT_SRF_USE_EXPECTED_DESC);
    outDesc  = rsinfo->setDesc;
    outStore = rsinfo->setResult;

    /* --- 2. Input tuple descriptor used by both tuplesorts --- */
    inDesc = CreateTemplateTupleDesc(3);
    TupleDescInitEntry(inDesc, 1, "id", INT4OID,      -1, 0);
    TupleDescInitEntry(inDesc, 2, "lo", TIMESTAMPOID, -1, 0);
    TupleDescInitEntry(inDesc, 3, "hi", TIMESTAMPOID, -1, 0);
    inDesc = BlessTupleDesc(inDesc);

    /* Resolve tsrange's range info once (used per emitted pair). */
    tsrangeTypcache = lookup_type_cache(TSRANGEOID, TYPECACHE_RANGE_INFO);

    putslot = MakeSingleTupleTableSlot(inDesc, &TTSOpsHeapTuple);
    getslot = MakeSingleTupleTableSlot(inDesc, &TTSOpsMinimalTuple);

    /* --- 3. Two tuplesorts. Each gets its own work_mem budget; both
     *        spill to disk if exceeded. --- */
    aSort = tuplesort_begin_heap(inDesc, 2, sortKeys,
                                 sortOps, sortColls, nullsFirst,
                                 work_mem, NULL, 0);
    bSort = tuplesort_begin_heap(inDesc, 2, sortKeys,
                                 sortOps, sortColls, nullsFirst,
                                 work_mem, NULL, 0);

    /* --- 4. Stream both source relations through SPI cursors --- */
    if (SPI_connect() != SPI_OK_CONNECT)
        elog(ERROR, "temporal_join: SPI_connect failed");

    aSql = build_projection_sql(leftOid);
    bSql = build_projection_sql(rightOid);

    stream_into_sort(aSql, aSort, putslot);
    stream_into_sort(bSql, bSort, putslot);

    SPI_finish();

    /* --- 5. Sort. This is where any disk spill occurs. --- */
    tuplesort_performsort(aSort);
    tuplesort_performsort(bSort);

    /* --- 6. Streaming sweep --- */
    aValid = read_row(aSort, getslot, &aCur);
    bValid = read_row(bSort, getslot, &bCur);

    curId = INT_MIN;
    as_init(&activeA);
    as_init(&activeB);

    while (aValid || bValid)
    {
        bool   aFirst;
        InRow *cur;

        CHECK_FOR_INTERRUPTS();

        /* Determine which side opens next. Order: smaller id first;
         * within the same id, smaller lower first. */
        if (!bValid)            aFirst = true;
        else if (!aValid)       aFirst = false;
        else if (aCur.id < bCur.id) aFirst = true;
        else if (aCur.id > bCur.id) aFirst = false;
        else                    aFirst = (aCur.lo <= bCur.lo);

        cur = aFirst ? &aCur : &bCur;

        /* New id → reset both active sets. */
        if (cur->id != curId)
        {
            curId = cur->id;
            as_clear(&activeA);
            as_clear(&activeB);
        }

        /* Drop expired (closed) intervals before emitting. */
        as_prune(&activeA, cur->lo);
        as_prune(&activeB, cur->lo);

        if (aFirst)
        {
            /* The opening A-row overlaps everything still active on B. */
            for (int i = 0; i < activeB.n; i++)
<<<<<<< HEAD
                emit_pair(outStore, outDesc, cur, &activeB.items[i]);
=======
                emit_pair(outStore, outDesc, tsrangeTypcache,
                          cur, &activeB.items[i]);
>>>>>>> main
            as_add(&activeA, cur);
            aValid = read_row(aSort, getslot, &aCur);
        }
        else
        {
            for (int i = 0; i < activeA.n; i++)
<<<<<<< HEAD
                emit_pair(outStore, outDesc, &activeA.items[i], cur);
=======
                emit_pair(outStore, outDesc, tsrangeTypcache,
                          &activeA.items[i], cur);
>>>>>>> main
            as_add(&activeB, cur);
            bValid = read_row(bSort, getslot, &bCur);
        }
    }

    /* --- 7. Cleanup --- */
    as_free(&activeA);
    as_free(&activeB);

    tuplesort_end(aSort);
    tuplesort_end(bSort);

    ExecDropSingleTupleTableSlot(putslot);
    ExecDropSingleTupleTableSlot(getslot);

    return (Datum) 0;
}
