/*
 * contrib/temporal_agg/temporal_agg.c
 *
 * Aggregate-augmented GiST index for temporal data. Stores per-subtree
 * min/max of an attribute v2 in internal nodes, and answers min/max(v2)
 * over (id = K AND time IN [L, R]) via a custom page-walking traversal
 */

#include "postgres.h"
#include "fmgr.h"

#include "access/genam.h"
#include "access/gist.h"
#include "access/gist_private.h"
#include "access/relation.h"
#include "access/stratnum.h"
#include "storage/bufmgr.h"
#include "storage/lockdefs.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/fmgrprotos.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/timestamp.h"

PG_MODULE_MAGIC;

#define AggOverlap   1


typedef struct AggLeaf {
    int32     id;
    Timestamp start;
    Timestamp end;
    int64     v2;
} AggLeaf;                     /* 32 bytes with double alignment */

typedef struct AggKey {
    int32     id_lower;
    int32     id_upper;
    Timestamp time_lower;
    Timestamp time_upper;
    int64     v2_min;
    int64     v2_max;
    int64     v2_sum;
    int32     v2_count;
} AggKey;                      /* 56 bytes (double-aligned, 4-byte tail pad) */

typedef struct AggQuery {
    int32     id;
    Timestamp start;
    Timestamp end;
} AggQuery;                    /* 24 bytes with padding */


PG_FUNCTION_INFO_V1(agg_leaf_in);
Datum agg_leaf_in(PG_FUNCTION_ARGS) {
    ereport(ERROR, (errmsg("agg_leaf_type cannot be parsed from text"),
        errhint("use agg_leaf(id, start, end, v2)")));
    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(agg_leaf_out);
Datum agg_leaf_out(PG_FUNCTION_ARGS) {
    AggLeaf *l = (AggLeaf *) PG_GETARG_POINTER(0);
    PG_RETURN_CSTRING(psprintf("(%d,%ld,%ld,%lld)",
        l->id, (long) l->start, (long) l->end, (long long) l->v2));
}

PG_FUNCTION_INFO_V1(agg_key_in);
Datum agg_key_in(PG_FUNCTION_ARGS) {
    ereport(ERROR, (errmsg("agg_key_type cannot be parsed from text")));
    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(agg_key_out);
Datum agg_key_out(PG_FUNCTION_ARGS) {
    AggKey *k = (AggKey *) PG_GETARG_POINTER(0);
    PG_RETURN_CSTRING(psprintf("(id[%d..%d],t[%ld..%ld],v2[min=%lld,max=%lld,sum=%lld],count=%d)",
        k->id_lower, k->id_upper,
        (long) k->time_lower, (long) k->time_upper,
        (long long) k->v2_min, (long long) k->v2_max, (long long) k->v2_sum,
        k->v2_count));
}

PG_FUNCTION_INFO_V1(agg_query_in);
Datum agg_query_in(PG_FUNCTION_ARGS) {
    ereport(ERROR, (errmsg("agg_query_type cannot be parsed from text"),
        errhint("use agg_query(id, start, end)")));
    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(agg_query_out);
Datum agg_query_out(PG_FUNCTION_ARGS) {
    AggQuery *q = (AggQuery *) PG_GETARG_POINTER(0);
    PG_RETURN_CSTRING(psprintf("(%d,%ld,%ld)",
        q->id, (long) q->start, (long) q->end));
}


/* ====================================================================== */
/* Constructors                                                           */
/* ====================================================================== */

PG_FUNCTION_INFO_V1(agg_leaf);
Datum agg_leaf(PG_FUNCTION_ARGS) {
    AggLeaf *l = (AggLeaf *) palloc(sizeof(*l));
    l->id    = PG_GETARG_INT32(0);
    l->start = PG_GETARG_TIMESTAMP(1);
    l->end   = PG_GETARG_TIMESTAMP(2);
    l->v2    = PG_GETARG_INT64(3);
    PG_RETURN_POINTER(l);
}

PG_FUNCTION_INFO_V1(agg_query);
Datum agg_query(PG_FUNCTION_ARGS) {
    AggQuery *q = (AggQuery *) palloc(sizeof(*q));
    q->id    = PG_GETARG_INT32(0);
    q->start = PG_GETARG_TIMESTAMP(1);
    q->end   = PG_GETARG_TIMESTAMP(2);
    PG_RETURN_POINTER(q);
}


/* ====================================================================== */
/* AggKey helpers                                                         */
/* ====================================================================== */

static inline void
key_init_from_leaf(AggKey *k, const AggLeaf *l)
{
    k->id_lower   = k->id_upper = l->id;
    k->time_lower = l->start;
    k->time_upper = l->end;
    k->v2_min     = k->v2_max =   k->v2_sum =  l->v2;
    k->v2_count   = 1;
}

static inline void
key_union(AggKey *dst, const AggKey *a, const AggKey *b)
{
    dst->id_lower   = Min(a->id_lower,   b->id_lower);
    dst->id_upper   = Max(a->id_upper,   b->id_upper);
    dst->time_lower = Min(a->time_lower, b->time_lower);
    dst->time_upper = Max(a->time_upper, b->time_upper);
    dst->v2_min     = Min(a->v2_min,     b->v2_min);
    dst->v2_max     = Max(a->v2_max,     b->v2_max);
    dst->v2_sum     = a->v2_sum + b->v2_sum;
    dst->v2_count   = a->v2_count + b->v2_count;
}

static inline int64
key_area_idtime(const AggKey *k)
{
    int64 dx = (int64) k->id_upper   - (int64) k->id_lower;
    int64 dt = (int64) k->time_upper - (int64) k->time_lower;
    if (dx < 0) dx = -dx;
    if (dt < 0) dt = -dt;
    return dx * dt;
}


PG_FUNCTION_INFO_V1(agg_overlap_query);
Datum agg_overlap_query(PG_FUNCTION_ARGS) {
    AggLeaf  *l = (AggLeaf *)  PG_GETARG_POINTER(0);
    AggQuery *q = (AggQuery *) PG_GETARG_POINTER(1);
    PG_RETURN_BOOL(l->id == q->id &&
                   l->start <= q->end && l->end >= q->start);
}


/* ====================================================================== */
/* GiST support functions                                                 */
/* ====================================================================== */

PG_FUNCTION_INFO_V1(agg_compress);
Datum agg_compress(PG_FUNCTION_ARGS) {
    GISTENTRY *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    GISTENTRY *retval;

    if (entry->leafkey) {
        AggLeaf *l = (AggLeaf *) DatumGetPointer(entry->key);
        AggKey  *k = (AggKey *)  palloc(sizeof(*k));
        key_init_from_leaf(k, l);
        retval = palloc(sizeof(*retval));
        gistentryinit(*retval, PointerGetDatum(k),
                      entry->rel, entry->page, entry->offset, false);
    } else {
        retval = entry;
    }
    PG_RETURN_POINTER(retval);
}

PG_FUNCTION_INFO_V1(agg_decompress);
Datum agg_decompress(PG_FUNCTION_ARGS) {
    PG_RETURN_POINTER(PG_GETARG_POINTER(0));
}

PG_FUNCTION_INFO_V1(agg_union);
Datum agg_union(PG_FUNCTION_ARGS) {
    GistEntryVector *ev = (GistEntryVector *) PG_GETARG_POINTER(0);
    int             *size = (int *)             PG_GETARG_POINTER(1);
    AggKey          *out  = (AggKey *) palloc(sizeof(*out));
    AggKey          *first = (AggKey *) DatumGetPointer(ev->vector[0].key);

    memcpy(out, first, sizeof(*out));
    for (int i = 1; i < ev->n; i++) {
        AggKey *cur = (AggKey *) DatumGetPointer(ev->vector[i].key);
        key_union(out, out, cur);
    }
    *size = sizeof(*out);
    PG_RETURN_POINTER(out);
}

PG_FUNCTION_INFO_V1(agg_same);
Datum agg_same(PG_FUNCTION_ARGS) {
    AggKey *a = (AggKey *) PG_GETARG_POINTER(0);
    AggKey *b = (AggKey *) PG_GETARG_POINTER(1);
    bool   *r = (bool *)   PG_GETARG_POINTER(2);
    *r = (a->id_lower == b->id_lower && a->id_upper == b->id_upper &&
          a->time_lower == b->time_lower && a->time_upper == b->time_upper &&
          a->v2_min == b->v2_min && a->v2_max == b->v2_max &&
          a->v2_sum == b->v2_sum && a->v2_count == b->v2_count);
    PG_RETURN_POINTER(r);
}

PG_FUNCTION_INFO_V1(agg_penalty);
Datum agg_penalty(PG_FUNCTION_ARGS) {
    GISTENTRY *orig  = (GISTENTRY *) PG_GETARG_POINTER(0);
    GISTENTRY *new_  = (GISTENTRY *) PG_GETARG_POINTER(1);
    float     *out   = (float *)     PG_GETARG_POINTER(2);

    AggKey *o = (AggKey *) DatumGetPointer(orig->key);
    AggKey *n = (AggKey *) DatumGetPointer(new_->key);
    AggKey  merged;

    key_union(&merged, o, n);
    /* penalty by (id, time) area  */
    *out = (float) (key_area_idtime(&merged) - key_area_idtime(o));
    PG_RETURN_POINTER(out);
}

/* picksplit: median split sorted by (id_lower, time_lower) — produces
 * near-disjoint sibling MBRs along the query dimensions. */
static int
cmp_entries(const void *a, const void *b)
{
    const GISTENTRY *ea = *(const GISTENTRY *const *) a;
    const GISTENTRY *eb = *(const GISTENTRY *const *) b;
    AggKey *ka = (AggKey *) DatumGetPointer(ea->key);
    AggKey *kb = (AggKey *) DatumGetPointer(eb->key);
    if (ka->id_lower != kb->id_lower)
        return (ka->id_lower < kb->id_lower) ? -1 : 1;
    if (ka->time_lower < kb->time_lower) return -1;
    if (ka->time_lower > kb->time_lower) return  1;
    return 0;
}

PG_FUNCTION_INFO_V1(agg_picksplit);
Datum agg_picksplit(PG_FUNCTION_ARGS) {
    GistEntryVector *ev = (GistEntryVector *) PG_GETARG_POINTER(0);
    GIST_SPLITVEC   *v  = (GIST_SPLITVEC *)   PG_GETARG_POINTER(1);
    int n = ev->n - 1;          
    GISTENTRY **sorted;
    int half;
    AggKey *uL = NULL, *uR = NULL;

    sorted = (GISTENTRY **) palloc(n * sizeof(GISTENTRY *));
    for (int i = 0; i < n; i++)
        sorted[i] = &ev->vector[i + FirstOffsetNumber];
    qsort(sorted, n, sizeof(GISTENTRY *), cmp_entries);

    half = n / 2;
    v->spl_left   = (OffsetNumber *) palloc(n * sizeof(OffsetNumber));
    v->spl_right  = (OffsetNumber *) palloc(n * sizeof(OffsetNumber));
    v->spl_nleft  = 0;
    v->spl_nright = 0;

    for (int i = 0; i < n; i++) {
        OffsetNumber off = sorted[i] - ev->vector;
        AggKey *k = (AggKey *) DatumGetPointer(sorted[i]->key);
        if (i < half) {
            if (!uL) { uL = palloc(sizeof(*uL)); memcpy(uL, k, sizeof(*uL)); }
            else     key_union(uL, uL, k);
            v->spl_left[v->spl_nleft++] = off;
        } else {
            if (!uR) { uR = palloc(sizeof(*uR)); memcpy(uR, k, sizeof(*uR)); }
            else     key_union(uR, uR, k);
            v->spl_right[v->spl_nright++] = off;
        }
    }
    v->spl_ldatum = PointerGetDatum(uL);
    v->spl_rdatum = PointerGetDatum(uR);
    PG_RETURN_POINTER(v);
}

static inline bool
key_overlaps_query(const AggKey *k, const AggQuery *q)
{
    if (k->id_upper < q->id || k->id_lower > q->id) return false;
    if (k->time_upper < q->start || k->time_lower > q->end) return false;
    return true;
}

PG_FUNCTION_INFO_V1(agg_consistent);
Datum agg_consistent(PG_FUNCTION_ARGS) {
    GISTENTRY      *entry    = (GISTENTRY *) PG_GETARG_POINTER(0);
    AggQuery       *q        = (AggQuery *)  PG_GETARG_POINTER(1);
    StrategyNumber  strategy = (StrategyNumber) PG_GETARG_UINT16(2);
    bool           *recheck  = (bool *) PG_GETARG_POINTER(4);
    AggKey         *k        = (AggKey *) DatumGetPointer(entry->key);

    *recheck = true;
    if (strategy != AggOverlap) {
        elog(ERROR, "unrecognized strategy: %d", strategy);
        PG_RETURN_BOOL(false);
    }
    PG_RETURN_BOOL(key_overlaps_query(k, q));
}


typedef enum { CLS_DISJOINT, CLS_CONTAINED, CLS_PARTIAL } Classify;

static Classify
classify(const AggKey *k, const AggQuery *q)
{
    if (k->id_upper < q->id || k->id_lower > q->id) return CLS_DISJOINT;
    if (k->time_upper < q->start || k->time_lower > q->end) return CLS_DISJOINT;
    if (k->id_lower == q->id && k->id_upper == q->id &&
        k->time_lower >= q->start && k->time_upper <= q->end)
        return CLS_CONTAINED;
    return CLS_PARTIAL;
}

static void
walk(Relation rel, BlockNumber blkno, const AggQuery *q,
     int64 *best_min, int64 *best_max, int64 *sum, int32 *count)
{
    Buffer       buf;
    Page         page;
    bool         isLeaf;
    OffsetNumber maxoff;
    BlockNumber *visit = NULL;
    int          n_visit = 0;

    buf = ReadBuffer(rel, blkno);
    LockBuffer(buf, BUFFER_LOCK_SHARE);
    page = BufferGetPage(buf);
    isLeaf = GistPageIsLeaf(page);
    maxoff = PageGetMaxOffsetNumber(page);

    if (maxoff > 0)
        visit = (BlockNumber *) palloc(maxoff * sizeof(BlockNumber));

    for (OffsetNumber off = FirstOffsetNumber;
         off <= maxoff;
         off = OffsetNumberNext(off))
    {
        IndexTuple it;
        Datum      d;
        bool       isnull;
        AggKey    *k;
        Classify   c;

        it = (IndexTuple) PageGetItem(page, PageGetItemId(page, off));
        d  = index_getattr(it, 1, RelationGetDescr(rel), &isnull);
        if (isnull) continue;
        k = (AggKey *) DatumGetPointer(d);

        c = classify(k, q);
        if (c == CLS_DISJOINT) continue;


        if (c == CLS_CONTAINED) {
            if (k->v2_min < *best_min) *best_min = k->v2_min;
            if (k->v2_max > *best_max) *best_max = k->v2_max;
            *sum   += k->v2_sum;
            *count += k->v2_count;
            continue;
        }

        if (isLeaf) {
            if (k->v2_min < *best_min) *best_min = k->v2_min;
            if (k->v2_max > *best_max) *best_max = k->v2_max;
            *sum   += k->v2_sum;     
            *count += k->v2_count;   
        } else {
            visit[n_visit++] = ItemPointerGetBlockNumber(&it->t_tid);
        }
    }

    UnlockReleaseBuffer(buf);

    for (int i = 0; i < n_visit; i++)
        walk(rel, visit[i], q, best_min, best_max, sum, count);

    if (visit) pfree(visit);
}

PG_FUNCTION_INFO_V1(seg_aggregate);
Datum
seg_aggregate(PG_FUNCTION_ARGS)
{
    Oid        index_oid = PG_GETARG_OID(0);
    AggQuery  *q         = (AggQuery *) PG_GETARG_POINTER(1);
    Relation   rel;
    int64      best_min = PG_INT64_MAX;
    int64      best_max = PG_INT64_MIN;
    int64      sum = 0;
    int32      count = 0;
    Datum      vals[4];
    bool       nulls[4] = { false, false, false, false };
    int        dims[1]  = { 4 };
    int        lbs[1]   = { 1 };
    ArrayType *arr;

    rel = index_open(index_oid, AccessShareLock);
    walk(rel, GIST_ROOT_BLKNO, q, &best_min, &best_max, &sum, &count);
    index_close(rel, AccessShareLock);

    if (count == 0) {
        nulls[0] = nulls[1] = nulls[2] = true;
        vals[0] = vals[1] = vals[2] = (Datum) 0;
        vals[3] = Int64GetDatum(0);
    } else {
        vals[0] = Int64GetDatum(best_min);
        vals[1] = Int64GetDatum(best_max);
        vals[2] = Int64GetDatum(sum);
        vals[3] = Int64GetDatum((int64) count);  
    }

    arr = construct_md_array(vals, nulls, 1, dims, lbs,
                             INT8OID, 8, FLOAT8PASSBYVAL, 'd');
    PG_RETURN_ARRAYTYPE_P(arr);
}
