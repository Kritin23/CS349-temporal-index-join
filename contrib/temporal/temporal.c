/*
 * contrib/temporal/temporal.c 
 */

#include "postgres.h"
#include "fmgr.h"       /* Required for PG_MODULE_MAGIC */
#include "temporal.h"
#include "access/gist.h"
#include "access/stratnum.h"
#include "utils/float.h"
#include "utils/fmgrprotos.h"
#include "utils/timestamp.h"

/* Magic block to ensure compatibility with PostgreSQL */
PG_MODULE_MAGIC;

typedef struct temporalKey {
    int32 id_lower;
    int32 id_upper;
    Timestamp time_lower;
    Timestamp time_upper;
} temporalKey;

typedef struct leafKey {
    int32 id;
    Timestamp start;
    Timestamp end;
} leafKey;

typedef struct timeItv {
    Timestamp start; 
    Timestamp end; 
} timeItv;

typedef struct idxPointQuery {
    int32 id;
    Timestamp time;
} idxPointQuery;

typedef struct idxQuery {
    int32 id;
    Timestamp   start,
                end;
} idxQuery;


/* ===== Constructors (clean SQL interface) ===== */

PG_FUNCTION_INFO_V1(temporal_key);
Datum
temporal_key(PG_FUNCTION_ARGS)
{
    int32 id = PG_GETARG_INT32(0);
    Timestamp start = PG_GETARG_TIMESTAMP(1);
    Timestamp end = PG_GETARG_TIMESTAMP(2);

    leafKey *key = (leafKey *) palloc(sizeof(leafKey));
    key->id = id;
    key->start = start;
    key->end = end;

    PG_RETURN_POINTER(key);
}

PG_FUNCTION_INFO_V1(temporal_point);
Datum
temporal_point(PG_FUNCTION_ARGS)
{
    int32 id = PG_GETARG_INT32(0);
    Timestamp t = PG_GETARG_TIMESTAMP(1);

    idxPointQuery *q = (idxPointQuery *) palloc(sizeof(idxPointQuery));
    q->id = id;
    q->time = t;

    PG_RETURN_POINTER(q);
}

PG_FUNCTION_INFO_V1(temporal_range);
Datum
temporal_range(PG_FUNCTION_ARGS)
{
    int32 id = PG_GETARG_INT32(0);
    Timestamp start = PG_GETARG_TIMESTAMP(1);
    Timestamp end = PG_GETARG_TIMESTAMP(2);

    idxQuery *q = (idxQuery *) palloc(sizeof(idxQuery));
    q->id = id;
    q->start = start;
    q->end = end;

    PG_RETURN_POINTER(q);
}

PG_FUNCTION_INFO_V1(temporal_time_range);
Datum
temporal_time_range(PG_FUNCTION_ARGS)
{
    Timestamp start = PG_GETARG_TIMESTAMP(0);
    Timestamp end = PG_GETARG_TIMESTAMP(1);

    timeItv *q = (timeItv *) palloc(sizeof(timeItv));
    q->start = start;
    q->end = end;

    PG_RETURN_POINTER(q);
}

/* ===== Input functions (disable text input, enforce constructors) ===== */

PG_FUNCTION_INFO_V1(temporal_in);
Datum
temporal_in(PG_FUNCTION_ARGS)
{
    ereport(ERROR,
        (errmsg("temporal_key_type cannot be constructed from text"),
         errhint("Use temporal_key(...) instead")));
    PG_RETURN_NULL(); /* unreachable */
}


PG_FUNCTION_INFO_V1(leaf_in);
Datum
leaf_in(PG_FUNCTION_ARGS)
{
    ereport(ERROR,
        (errmsg("leaf_key_type cannot be constructed from text"),
         errhint("Use temporal_key(id, start_time, end_time) instead")));
    PG_RETURN_NULL(); /* unreachable */
}


PG_FUNCTION_INFO_V1(time_itv_in);
Datum
time_itv_in(PG_FUNCTION_ARGS)
{
    ereport(ERROR,
        (errmsg("time_itv_query cannot be constructed from text"),
         errhint("Use temporal_time_range(start_time, end_time) instead")));
    PG_RETURN_NULL(); /* unreachable */
}


PG_FUNCTION_INFO_V1(idx_point_in);
Datum
idx_point_in(PG_FUNCTION_ARGS)
{
    ereport(ERROR,
        (errmsg("idx_point_query cannot be constructed from text"),
         errhint("Use temporal_point(id, timestamp) instead")));
    PG_RETURN_NULL(); /* unreachable */
}


PG_FUNCTION_INFO_V1(idx_range_in);
Datum
idx_range_in(PG_FUNCTION_ARGS)
{
    ereport(ERROR,
        (errmsg("idx_range_query cannot be constructed from text"),
         errhint("Use temporal_range(id, start_time, end_time) instead")));
    PG_RETURN_NULL(); /* unreachable */
}

/* ===== I/O functions for custom types ===== */

PG_FUNCTION_INFO_V1(temporal_out);
Datum
temporal_out(PG_FUNCTION_ARGS)
{
    temporalKey *key = (temporalKey *) PG_GETARG_POINTER(0);
    char *result = psprintf("(%d,%d,%ld,%ld)",
        key->id_lower, key->id_upper, (long) key->time_lower, (long) key->time_upper);
    PG_RETURN_CSTRING(result);
}

PG_FUNCTION_INFO_V1(leaf_out);
Datum
leaf_out(PG_FUNCTION_ARGS)
{
    leafKey *key = (leafKey *) PG_GETARG_POINTER(0);
    char *result = psprintf("(%d,%ld,%ld)",
        key->id, (long) key->start, (long) key->end);
    PG_RETURN_CSTRING(result);
}

PG_FUNCTION_INFO_V1(time_itv_out);
Datum
time_itv_out(PG_FUNCTION_ARGS)
{
    timeItv *itv = (timeItv *) PG_GETARG_POINTER(0);
    char *result = psprintf("(%ld,%ld)",
        (long) itv->start, (long) itv->end);
    PG_RETURN_CSTRING(result);
}

PG_FUNCTION_INFO_V1(idx_point_out);
Datum
idx_point_out(PG_FUNCTION_ARGS)
{
    idxPointQuery *q = (idxPointQuery *) PG_GETARG_POINTER(0);
    char *result = psprintf("(%d,%ld)",
        q->id, (long) q->time);
    PG_RETURN_CSTRING(result);
}

PG_FUNCTION_INFO_V1(idx_range_out);
Datum
idx_range_out(PG_FUNCTION_ARGS)
{
    idxQuery *q = (idxQuery *) PG_GETARG_POINTER(0);
    char *result = psprintf("(%d,%ld,%ld)",
        q->id, (long) q->start, (long) q->end);
    PG_RETURN_CSTRING(result);
}

/* ========================================================== */
/* SQL OPERATOR FUNCTIONS (Sequential Scan / Direct SQL)      */
/* ========================================================== */

/* Overlap (&&) */
PG_FUNCTION_INFO_V1(temporal_overlap_time_itv);
Datum temporal_overlap_time_itv(PG_FUNCTION_ARGS) {
    leafKey *key = (leafKey *) PG_GETARG_POINTER(0);
    timeItv *query = (timeItv *) PG_GETARG_POINTER(1);
    PG_RETURN_BOOL(key->start <= query->end && key->end >= query->start);
}

PG_FUNCTION_INFO_V1(temporal_overlap_timestamp);
Datum temporal_overlap_timestamp(PG_FUNCTION_ARGS) {
    leafKey *key = (leafKey *) PG_GETARG_POINTER(0);
    Timestamp query = PG_GETARG_TIMESTAMP(1);
    PG_RETURN_BOOL(key->start <= query && key->end >= query);
}

PG_FUNCTION_INFO_V1(temporal_overlap_idx_point);
Datum temporal_overlap_idx_point(PG_FUNCTION_ARGS) {
    leafKey *key = (leafKey *) PG_GETARG_POINTER(0);
    idxPointQuery *query = (idxPointQuery *) PG_GETARG_POINTER(1);
    PG_RETURN_BOOL(key->id == query->id && key->start <= query->time && key->end >= query->time);
}

PG_FUNCTION_INFO_V1(temporal_overlap_idx_range);
Datum temporal_overlap_idx_range(PG_FUNCTION_ARGS) {
    leafKey *key = (leafKey *) PG_GETARG_POINTER(0);
    idxQuery *query = (idxQuery *) PG_GETARG_POINTER(1);
    PG_RETURN_BOOL(key->id == query->id && key->start <= query->end && key->end >= query->start);
}

PG_FUNCTION_INFO_V1(temporal_overlap_leaf_key);
Datum temporal_overlap_leaf_key(PG_FUNCTION_ARGS) {
    leafKey *key = (leafKey *) PG_GETARG_POINTER(0);
    leafKey *query = (leafKey *) PG_GETARG_POINTER(1);
    PG_RETURN_BOOL(key->id == query->id && key->start < query->end && key->end > query->start);
}

/* Contains (@>) */
PG_FUNCTION_INFO_V1(temporal_contains_time_itv);
Datum temporal_contains_time_itv(PG_FUNCTION_ARGS) {
    leafKey *key = (leafKey *) PG_GETARG_POINTER(0);
    timeItv *query = (timeItv *) PG_GETARG_POINTER(1);
    PG_RETURN_BOOL(key->start <= query->start && key->end >= query->end);
}

PG_FUNCTION_INFO_V1(temporal_contains_timestamp);
Datum temporal_contains_timestamp(PG_FUNCTION_ARGS) {
    leafKey *key = (leafKey *) PG_GETARG_POINTER(0);
    Timestamp query = PG_GETARG_TIMESTAMP(1);
    PG_RETURN_BOOL(key->start <= query && key->end >= query);
}

PG_FUNCTION_INFO_V1(temporal_contains_idx_point);
Datum temporal_contains_idx_point(PG_FUNCTION_ARGS) {
    leafKey *key = (leafKey *) PG_GETARG_POINTER(0);
    idxPointQuery *query = (idxPointQuery *) PG_GETARG_POINTER(1);
    PG_RETURN_BOOL(key->id == query->id && key->start <= query->time && key->end >= query->time);
}

PG_FUNCTION_INFO_V1(temporal_contains_idx_range);
Datum temporal_contains_idx_range(PG_FUNCTION_ARGS) {
    leafKey *key = (leafKey *) PG_GETARG_POINTER(0);
    idxQuery *query = (idxQuery *) PG_GETARG_POINTER(1);
    PG_RETURN_BOOL(key->id == query->id && key->start <= query->start && key->end >= query->end);
}

/* Contained By (<@) */
PG_FUNCTION_INFO_V1(temporal_contained_time_itv);
Datum temporal_contained_time_itv(PG_FUNCTION_ARGS) {
    leafKey *key = (leafKey *) PG_GETARG_POINTER(0);
    timeItv *query = (timeItv *) PG_GETARG_POINTER(1);
    PG_RETURN_BOOL(key->start >= query->start && key->end <= query->end);
}

PG_FUNCTION_INFO_V1(temporal_contained_timestamp);
Datum temporal_contained_timestamp(PG_FUNCTION_ARGS) {
    leafKey *key = (leafKey *) PG_GETARG_POINTER(0);
    Timestamp query = PG_GETARG_TIMESTAMP(1);
    PG_RETURN_BOOL(key->start >= query && key->end <= query);
}

PG_FUNCTION_INFO_V1(temporal_contained_idx_point);
Datum temporal_contained_idx_point(PG_FUNCTION_ARGS) {
    leafKey *key = (leafKey *) PG_GETARG_POINTER(0);
    idxPointQuery *query = (idxPointQuery *) PG_GETARG_POINTER(1);
    PG_RETURN_BOOL(key->id == query->id && key->start >= query->time && key->end <= query->time);
}

PG_FUNCTION_INFO_V1(temporal_contained_idx_range);
Datum temporal_contained_idx_range(PG_FUNCTION_ARGS) {
    leafKey *key = (leafKey *) PG_GETARG_POINTER(0);
    idxQuery *query = (idxQuery *) PG_GETARG_POINTER(1);
    PG_RETURN_BOOL(key->id == query->id && key->start >= query->start && key->end <= query->end);
}

/* ========================================================== */
/* GiST SUPPORT FUNCTIONS                                     */
/* ========================================================== */

static bool tsrange_consistent(GISTENTRY* entry, temporalKey* key, timeItv* query, StrategyNumber strategy);
static bool ts_consistent(GISTENTRY* entry, temporalKey* key, Timestamp* query, StrategyNumber strategy);
static bool idx_point_consistent(GISTENTRY* entry, temporalKey* key, idxPointQuery* query, StrategyNumber strategy);
static bool idx_range_consistent(GISTENTRY* entry, temporalKey* key, idxQuery* query, StrategyNumber strategy);

#define CHECK_TIME_OVERLAP(key, query) ((key)->time_lower <= (query)->end && (key)->time_upper >= (query)->start)
#define CHECK_TIME_CONTAINED(key, query) ((key)->time_lower >= (query)->start && (key)->time_upper <= (query)->end)
#define CHECK_TIME_CONTAINS(key, query) ((key)->time_lower <= (query)->start && (key)->time_upper >= (query)->end)
#define CHECK_TIME_POINT_CONTAINED(key, query) ((query) >= (key)->time_lower && (query) <= (key)->time_upper)
#define CHECK_ID_OVERLAP(key, query) ((key)->id_lower <= (query)->id && (key)->id_upper >= (query)->id)

PG_FUNCTION_INFO_V1(temporal_decompress);
PG_FUNCTION_INFO_V1(temporal_compress);
PG_FUNCTION_INFO_V1(temporal_consistent);
PG_FUNCTION_INFO_V1(temporal_union);
PG_FUNCTION_INFO_V1(temporal_same);
PG_FUNCTION_INFO_V1(temporal_penalty);
PG_FUNCTION_INFO_V1(temporal_picksplit);

Datum
temporal_decompress(PG_FUNCTION_ARGS)
{
    PG_RETURN_POINTER(PG_GETARG_POINTER(0));
}

Datum
temporal_compress(PG_FUNCTION_ARGS)
{
    GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    GISTENTRY  *retval;

    if (entry->leafkey)
    {
        temporalKey *key = palloc(sizeof(temporalKey));
        leafKey *entry_data = (leafKey *)DatumGetPointer(entry->key);
        
        key->id_lower   = entry_data->id;
        key->id_upper   = entry_data->id;
        key->time_lower = entry_data->start;
        key->time_upper = entry_data->end;

        retval = palloc(sizeof(GISTENTRY));
        gistentryinit(*retval, PointerGetDatum(key),
                      entry->rel, entry->page, entry->offset, false);
    }
    else
    {
        retval = entry;
    }

    PG_RETURN_POINTER(retval);
}

Datum
temporal_consistent(PG_FUNCTION_ARGS)
{
    GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    Datum  query = PG_GETARG_DATUM(1);
    StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);
    bool       *recheck = (bool *) PG_GETARG_POINTER(4);
    temporalKey  *key = (temporalKey*)DatumGetPointer(entry->key);
    bool        retval;

    switch(strategy)
    {
    case TempRangeOverlap:       /* 1 */
    case TempRangeContains:      /* 5 */
    case TempRangeContained:     /* 9 */
        retval = tsrange_consistent(entry, key, (timeItv*)DatumGetPointer(query), strategy);
        break;

    case TempPointOverlap:       /* 2 */
    case TempPointContains:      /* 6 */
    case TempPointContained:     /* 10 */
    {
        Timestamp time = DatumGetTimestamp(query);
        retval = ts_consistent(entry, key, &time, strategy);
        break;
    }
    
    case TempIdxPointOverlap:    /* 3 */
    case TempIdxPointContains:   /* 7 */
    case TempIdxPointContained:  /* 11 */
        retval = idx_point_consistent(entry, key, (idxPointQuery*) DatumGetPointer(query), strategy);
        break;

    case TempIdxRangeOverlap:    /* 4 */
    case TempIdxRangeContains:   /* 8 */
    case TempIdxRangeContained:  /* 12 */
    case TempLeafOverlap:        /* 13 */
        retval = idx_range_consistent(entry, key, (idxQuery*) DatumGetPointer(query), strategy);
        break;

    default:
        elog(ERROR, "unrecognized strategy number: %d", strategy);
        retval = false;
        break;
    }

    *recheck = true;
    PG_RETURN_BOOL(retval);
}

static bool 
tsrange_consistent(GISTENTRY* entry, temporalKey* key, timeItv* query, StrategyNumber strategy)
{
    if (GIST_LEAF(entry))
    {
        switch (strategy)
        {
            case TempRangeOverlap: return CHECK_TIME_OVERLAP(key, query);
            case TempRangeContains: return CHECK_TIME_CONTAINS(key, query);
            case TempRangeContained: return CHECK_TIME_CONTAINED(key, query);
            default: return false;
        }
    }
    return CHECK_TIME_OVERLAP(key, query);
}

static bool 
ts_consistent(GISTENTRY* entry, temporalKey* key, Timestamp* query, StrategyNumber strategy)
{
    switch (strategy)
    {
        case TempPointOverlap:
        case TempPointContains:
        case TempPointContained: 
            return CHECK_TIME_POINT_CONTAINED(key, *query);
        default: return false;
    }
}

static bool 
idx_point_consistent(GISTENTRY* entry, temporalKey* key, idxPointQuery* query, StrategyNumber strategy)
{
    if (GIST_LEAF(entry))
    {
        switch(strategy)
        {
            case TempIdxPointOverlap:
            case TempIdxPointContains:
            case TempIdxPointContained:
                return CHECK_TIME_POINT_CONTAINED(key, query->time) && CHECK_ID_OVERLAP(key, query);
            default: return false;
        }
    }
    return CHECK_TIME_POINT_CONTAINED(key, query->time) && CHECK_ID_OVERLAP(key, query);
}

static bool 
idx_range_consistent(GISTENTRY* entry, temporalKey* key, idxQuery* query, StrategyNumber strategy)
{
    if (GIST_LEAF(entry))
    {
        switch (strategy)
        {
        case TempIdxRangeContained:
            return CHECK_TIME_CONTAINED(key, query) && CHECK_ID_OVERLAP(key, query);
        case TempIdxRangeOverlap:
        case TempLeafOverlap:
            return CHECK_TIME_OVERLAP(key, query) && CHECK_ID_OVERLAP(key, query);
        case TempIdxRangeContains:
            return CHECK_TIME_CONTAINS(key, query) && CHECK_ID_OVERLAP(key, query);
        default: break;
        }
    }
    return CHECK_TIME_OVERLAP(key, query) && CHECK_ID_OVERLAP(key, query);
}

static void 
entry_union(temporalKey* a, temporalKey* b, temporalKey* dest)
{
    dest->id_lower = Min(a->id_lower, b->id_lower);
    dest->id_upper = Max(a->id_upper, b->id_upper);
    dest->time_lower = Min(a->time_lower, b->time_lower);
    dest->time_upper = Max(a->time_upper, b->time_upper);   
}

static int64
temporal_area(temporalKey* a)
{
    return abs(a->id_upper - a->id_lower) * abs(a->time_upper - a->time_lower);
}

Datum
temporal_penalty(PG_FUNCTION_ARGS)
{
    GISTENTRY    *origentry = (GISTENTRY *) PG_GETARG_POINTER(0);
    GISTENTRY    *newentry = (GISTENTRY *) PG_GETARG_POINTER(1);
    float        *penalty = (float *) PG_GETARG_POINTER(2);

    temporalKey  *orig = (temporalKey*) DatumGetPointer(origentry->key);
    temporalKey  *new  = (temporalKey*) DatumGetPointer(newentry->key);

    temporalKey  merged;
    entry_union(orig, new, &merged);
    *penalty = temporal_area(&merged) - temporal_area(orig);
    PG_RETURN_POINTER(penalty);
}

Datum
temporal_union(PG_FUNCTION_ARGS)
{
    GistEntryVector *entryvec = (GistEntryVector *) PG_GETARG_POINTER(0);
    GISTENTRY  *ent = entryvec->vector;
    temporalKey     *out, *tmp;
    int         numranges, i;
    int *size = (int*) PG_GETARG_POINTER(1);

    numranges = entryvec->n;
    tmp = (temporalKey*) DatumGetPointer(ent[0].key);
    out = (temporalKey*) palloc(sizeof(temporalKey));
    memcpy(out, tmp, sizeof(temporalKey));

    if (numranges == 1) PG_RETURN_POINTER(out);

    for (i = 1; i < numranges; i++)
    {
        tmp = (temporalKey*) DatumGetPointer(ent[i].key);
        entry_union(out, tmp, out);
    }

    *size = sizeof(temporalKey);
    PG_RETURN_POINTER(out);
}

Datum
temporal_same(PG_FUNCTION_ARGS)
{
    temporalKey *v1 = (temporalKey*) PG_GETARG_POINTER(0);
    temporalKey *v2 = (temporalKey*) PG_GETARG_POINTER(1);
    bool       *result = (bool *) PG_GETARG_POINTER(2);

    if(v1 && v2)
        *result = (v1->id_lower == v2->id_lower && v1->id_upper == v2->id_upper && 
                   v1->time_lower == v2->time_lower && v1->time_upper == v2->time_upper);
    else 
        *result = (v1 == NULL && v2 == NULL);
    PG_RETURN_POINTER(result);
}

Datum
temporal_picksplit(PG_FUNCTION_ARGS)
{
    GistEntryVector *entryvec = (GistEntryVector *) PG_GETARG_POINTER(0);
    GIST_SPLITVEC *v = (GIST_SPLITVEC *) PG_GETARG_POINTER(1);
    OffsetNumber i, j, maxoff = entryvec->n - 1;
    temporalKey *unionL, *unionR, *cur;
    OffsetNumber seed_1 = FirstOffsetNumber, seed_2 = OffsetNumberNext(FirstOffsetNumber);
    int64 max_dist = -1;

    /* 1. Find the two most distant seeds (Simplified Quadratic Seed Selection) */
    for (i = FirstOffsetNumber; i < maxoff; i = OffsetNumberNext(i))
    {
        temporalKey *ki = (temporalKey *) DatumGetPointer(entryvec->vector[i].key);
        for (j = OffsetNumberNext(i); j <= maxoff; j = OffsetNumberNext(j))
        {
            temporalKey *kj = (temporalKey *) DatumGetPointer(entryvec->vector[j].key);
            temporalKey merged;
            entry_union(ki, kj, &merged);
            int64 dist = temporal_area(&merged) - temporal_area(ki) - temporal_area(kj);
            if (dist > max_dist)
            {
                max_dist = dist;
                seed_1 = i;
                seed_2 = j;
            }
        }
    }

    /* Initialize split vectors */
    v->spl_left = (OffsetNumber *) palloc(entryvec->n * sizeof(OffsetNumber));
    v->spl_right = (OffsetNumber *) palloc(entryvec->n * sizeof(OffsetNumber));
    v->spl_nleft = 0;
    v->spl_nright = 0;

    unionL = (temporalKey *) palloc(sizeof(temporalKey));
    unionR = (temporalKey *) palloc(sizeof(temporalKey));
    memcpy(unionL, DatumGetPointer(entryvec->vector[seed_1].key), sizeof(temporalKey));
    memcpy(unionR, DatumGetPointer(entryvec->vector[seed_2].key), sizeof(temporalKey));

    /* 2. Distribute remaining entries */
    for (i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
    {
        if (i == seed_1) {
            v->spl_left[v->spl_nleft++] = i;
            continue;
        }
        if (i == seed_2) {
            v->spl_right[v->spl_nright++] = i;
            continue;
        }

        cur = (temporalKey *) DatumGetPointer(entryvec->vector[i].key);
        
        temporalKey tmpL, tmpR;
        entry_union(unionL, cur, &tmpL);
        entry_union(unionR, cur, &tmpR);

        int64 growthL = temporal_area(&tmpL) - temporal_area(unionL);
        int64 growthR = temporal_area(&tmpR) - temporal_area(unionR);

        /* Assign to the group that grows the least */
        if (growthL < growthR)
        {
            entry_union(unionL, cur, unionL);
            v->spl_left[v->spl_nleft++] = i;
        }
        else
        {
            entry_union(unionR, cur, unionR);
            v->spl_right[v->spl_nright++] = i;
        }
    }

    v->spl_ldatum = PointerGetDatum(unionL);
    v->spl_rdatum = PointerGetDatum(unionR);
    PG_RETURN_POINTER(v);
}