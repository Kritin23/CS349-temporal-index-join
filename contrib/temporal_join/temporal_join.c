/*
 * contrib/temporal_join/temporal_join.c
 *
 *   SELECT * FROM temporal_join('A'::regclass, 'B'::regclass);
 *
 * Both input tables are required to have three columns
 *   id        int
 *   timerange tsrange
 *   data      int
 * (the column names matter; types must be exactly these).
 *
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
#include "utils/rangetypes.h"
#include "utils/rel.h"
#include "utils/timestamp.h"
#include "utils/tuplesort.h"
#include "utils/typcache.h"

PG_MODULE_MAGIC;

#define INT4_LT_OP        97
#define TIMESTAMP_LT_OP   2062


typedef struct InRow {
    int32     id;
    Timestamp lo;
    Timestamp hi;
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

/* An interval [lo, hi) is OPEN,at time t iff lo <= t < hi. So at the sweep point `cutoff`, only
 * entries with hi > cutoff  are still active. */
static void
as_prune(ActiveSet *s, Timestamp cutoff)
{
    int w = 0;
    for (int i = 0; i < s->n; i++)
        if (s->items[i].hi > cutoff)
            s->items[w++] = s->items[i];
    s->n = w;
}


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

    tuplestore_putvalues(out, desc, vals, nulls);
}


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


static bool
read_row(Tuplesortstate *sort, TupleTableSlot *slot, InRow *out)
{
    if (!tuplesort_gettupleslot(sort, true /* forward */, false /* copy */,
                                slot, NULL))
        return false;

    slot_getallattrs(slot);
    out->id = DatumGetInt32(slot->tts_values[0]);
    out->lo = DatumGetTimestamp(slot->tts_values[1]);
    out->hi = DatumGetTimestamp(slot->tts_values[2]);

    return true;
}


static char *
build_projection_sql(Oid relOid)
{
    char *nspname = get_namespace_name(get_rel_namespace(relOid));
    char *relname = get_rel_name(relOid);
    const char *qual;

    if (!nspname || !relname)
        elog(ERROR, "temporal_join: cannot resolve relation %u", relOid);

    qual = quote_qualified_identifier(nspname, relname);

    /* Required column names: id, timerange. */
    return psprintf(
        "SELECT id::int4, "
        "lower(timerange)::timestamp, "
        "upper(timerange)::timestamp "
        "FROM %s",
        qual);
}



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

    InitMaterializedSRF(fcinfo, MAT_SRF_USE_EXPECTED_DESC);
    outDesc  = rsinfo->setDesc;
    outStore = rsinfo->setResult;

    inDesc = CreateTemplateTupleDesc(3);
    TupleDescInitEntry(inDesc, 1, "id", INT4OID,      -1, 0);
    TupleDescInitEntry(inDesc, 2, "lo", TIMESTAMPOID, -1, 0);
    TupleDescInitEntry(inDesc, 3, "hi", TIMESTAMPOID, -1, 0);
    inDesc = BlessTupleDesc(inDesc);

    tsrangeTypcache = lookup_type_cache(TSRANGEOID, TYPECACHE_RANGE_INFO);

    putslot = MakeSingleTupleTableSlot(inDesc, &TTSOpsHeapTuple);
    getslot = MakeSingleTupleTableSlot(inDesc, &TTSOpsMinimalTuple);

    aSort = tuplesort_begin_heap(inDesc, 2, sortKeys,
                                 sortOps, sortColls, nullsFirst,
                                 work_mem, NULL, 0);
    bSort = tuplesort_begin_heap(inDesc, 2, sortKeys,
                                 sortOps, sortColls, nullsFirst,
                                 work_mem, NULL, 0);

    if (SPI_connect() != SPI_OK_CONNECT)
        elog(ERROR, "temporal_join: SPI_connect failed");

    aSql = build_projection_sql(leftOid);
    bSql = build_projection_sql(rightOid);

    stream_into_sort(aSql, aSort, putslot);
    stream_into_sort(bSql, bSort, putslot);

    SPI_finish();

    tuplesort_performsort(aSort);
    tuplesort_performsort(bSort);

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

        as_prune(&activeA, cur->lo);
        as_prune(&activeB, cur->lo);

        if (aFirst)
        {
            for (int i = 0; i < activeB.n; i++)
                emit_pair(outStore, outDesc, tsrangeTypcache,
                          cur, &activeB.items[i]);
            as_add(&activeA, cur);
            aValid = read_row(aSort, getslot, &aCur);
        }
        else
        {
            for (int i = 0; i < activeA.n; i++)
                emit_pair(outStore, outDesc, tsrangeTypcache,
                          &activeA.items[i], cur);
            as_add(&activeB, cur);
            bValid = read_row(bSort, getslot, &bCur);
        }
    }

    as_free(&activeA);
    as_free(&activeB);

    tuplesort_end(aSort);
    tuplesort_end(bSort);

    ExecDropSingleTupleTableSlot(putslot);
    ExecDropSingleTupleTableSlot(getslot);

    return (Datum) 0;
}

