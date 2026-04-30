/*
 * contrib/temporal/temporal.c 
 */

#include "postgres.h"
#include "temporal.h"
#include "access/gist.h"
#include "access/stratnum.h"
#include "utils/float.h"
#include "utils/fmgrprotos.h"
#include "utils/timestamp.h"

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

static bool 
tsrange_consistent(GISTENTRY* entry, temporalKey* key, timeItv* query, StrategyNumber strategy);
static bool 
ts_consistent(GISTENTRY* entry, temporalKey* key, Timestamp* query, StrategyNumber strategy);
static bool 
idx_point_consistent(GISTENTRY* entry, temporalKey* key, idxPointQuery* query, StrategyNumber strategy);
static bool 
idx_range_consistent(GISTENTRY* entry, temporalKey* key, idxQuery* query, StrategyNumber strategy);
// static bool 
// bbox_consistent(temporalKey* key, temporalKey* query, StrategyNumber strategy);

#define CHECK_TIME_OVERLAP(key, query) ((key)->time_lower <= (query)->end && (key)->time_upper >= (query)->start)
#define CHECK_TIME_CONTAINED(key, query) ((key)->time_lower >= (query)->start && (key)->time_upper <= (query)->end)
#define CHECK_TIME_CONTAINS(key, query) ((key)->time_lower <= (query)->start && (key)->time_upper >= (query)->end)
#define CHECK_TIME_POINT_CONTAINED(key, query) ((query) >= (key)->time_lower && (query) <= (key)->time_upper)
#define CHECK_ID_OVERLAP(key, query) ((key)->id_lower <= (query)->id && (key)->id_upper >= (query)->id)

PG_FUNCTION_INFO_V1(temporal_compress);
PG_FUNCTION_INFO_V1(temporal_consistent);
PG_FUNCTION_INFO_V1(temporal_union);
PG_FUNCTION_INFO_V1(temporal_same);
PG_FUNCTION_INFO_V1(temporal_penalty);
PG_FUNCTION_INFO_V1(temporal_picksplit);


Datum
temporal_compress(PG_FUNCTION_ARGS)
{
    GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    GISTENTRY  *retval;

    if (entry->leafkey)
    {
        /* replace entry->key with a compressed version */
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
        /* typically we needn't do anything with non-leaf entries */
        retval = entry;
    }

    PG_RETURN_POINTER(retval);
}

/**
 * temporal_consistent()
 * 
 * Need to define valid Strategies for our index
 * Strategies are nothing but the operators we will use in our queries. 
 * We could go with the existing operators/strategies, or define our own as 
 * well. 
 *  See src/include/access/srtatnum.h
 * 
 *  - RTContainsStrategyNumber
 *  - RTContainedByStrategyNumber
 *  - RTOverlapStrategyNumber
 * 
 *  Need to differentiate based on data type as well, or maybe strategy number
 *  can remain same and we can figure out data type some other way.
 *  Anyways, temporal_consistent will call one of xxx_consistent(), which will
 *  ultimately call bbox_consistent(). 
 * 
 *  This seemed intuitive and neat to me, but if you want something else, feel 
 *  free. 
 */
Datum
temporal_consistent(PG_FUNCTION_ARGS)
{
    GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    Datum  query = PG_GETARG_DATUM(1);
    StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);
    /* Oid subtype = PG_GETARG_OID(3); */
    bool       *recheck = (bool *) PG_GETARG_POINTER(4);
    temporalKey  *key = (temporalKey*)DatumGetPointer(entry->key);
    bool        retval;

    /*
     * determine return value as a function of strategy, key and query.
     *
     * Use GIST_LEAF(entry) to know where you're called in the index tree,
     * which comes handy when supporting the = operator for example (you could
     * check for non empty union() in non-leaf nodes and equality in leaf
     * nodes).
     */
    switch(strategy)
    {
    case TempRangeOverlap:
    case TempRangeContained:
    case TempRangeContains:
        retval = tsrange_consistent(entry, key, (timeItv*)DatumGetPointer(query), strategy);
        break;

    case TempIdxRangeContained:
    // case TempIdxRangeContains:
    // case TempIdxRangeOverlap:
        idxQuery* query_range = (idxQuery*) DatumGetPointer(query);
        retval = idx_range_consistent(entry, key, query_range, strategy);
        break;

    case TempPointContained:
    case TempPointContains:
    case TempPointOverlap:
        Timestamp time = DatumGetTimestamp(query);
        retval = ts_consistent(entry, key, &time, strategy);
        break;
    
    case TempIdxPointContained:
    // case TempIdxPointContains:
    // case TempIdxPointOverlap:
        idxPointQuery* query_point = (idxPointQuery*) DatumGetPointer(query);
        retval = idx_point_consistent(entry, key, query_point, strategy);
        break;

        
        
        

    default:
        elog(ERROR, "unrecognized strategy number: %d", strategy);
			retval = false;		/* keep compiler quiet */
			break;
    }

    *recheck = true;        /* or false if check is exact */

    PG_RETURN_BOOL(retval);
}

 /**
  * This function will handle queries where query data is just a timestamp
  * range. e.g. get all tuples contained in [start, end]
  */
static bool 
tsrange_consistent(GISTENTRY* entry, temporalKey* key, timeItv* query, StrategyNumber strategy)
{
    if (GIST_LEAF(entry))
    {
        switch (strategy)
        {
            case TempRangeOverlap:
                return CHECK_TIME_OVERLAP(key, query);
            case TempRangeContains:
                return CHECK_TIME_CONTAINS(key, query);
            case TempRangeContained:
                return CHECK_TIME_CONTAINED(key, query);
            default:
                elog(ERROR, "unrecognized strategy number: %d for tsrange queries", strategy);
                return false;
        }
    }
    // For non-leaf nodes, we just check if the bounding box overlaps with the query range.
    return CHECK_TIME_OVERLAP(key, query);
}

/**
 * This function will handle queries where query data is a single time point.
 * e.g. get all tuples containing T.
 */
static bool 
ts_consistent(GISTENTRY* entry, temporalKey* key, Timestamp* query, StrategyNumber strategy)
{
    switch (strategy)
    {
        case TempPointContained:
            return CHECK_TIME_POINT_CONTAINED(key, *query);
        
        default:
            elog(ERROR, "unrecognized strategy number: %d for single time point", strategy);
            break;
    }
    return false;
}

/**
 * This function will handle queries where query data an idx and a time point
 * e.g. get all tuples with primary key K containing T.
 */
static bool 
idx_point_consistent(GISTENTRY* entry, temporalKey* key, idxPointQuery* query, StrategyNumber strategy)
{
    if (GIST_LEAF(entry))
    {
        switch(strategy)
        {
            case TempIdxPointContained:
                return CHECK_TIME_POINT_CONTAINED(key, query->time) && CHECK_ID_OVERLAP(key, query);
            default:
                elog(ERROR, "unrecognized strategy number: %d for idx point queries", strategy);
                return false;
        }
    }
    return CHECK_TIME_POINT_CONTAINED(key, query->time) && CHECK_ID_OVERLAP(key, query);
}

/**
 * This function will handle queries where query data an idx and a time range
 * e.g. get all tuples with primary key K overlapping [start, end].
 */
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
            return CHECK_TIME_OVERLAP(key, query) && CHECK_ID_OVERLAP(key, query);
        case TempIdxRangeContains:
            return CHECK_TIME_CONTAINS(key, query) && CHECK_ID_OVERLAP(key, query);
        default:
            break;
        }
    }
    return CHECK_TIME_OVERLAP(key, query) && CHECK_ID_OVERLAP(key, query);
}

/**
 * This functions check consistent based on query types. Each recieves a 
 * bounding box to check from its caller.
 * Strategy specifies whether it is OVERLAP, CONTAINED, or CONTAINS
 */

// static bool 
// bbox_consistent(temporalKey* key, temporalKey* query, StrategyNumber strategy)
// {
//     if (GIST_LEAF(entry))
//     {
//         switch (strategy)
//         {
//         case TempRangeOverlap:
//             return CHECK_TIME_OVERLAP(key, query);
//         case TempRangeContains:
//             return CHECK_TIME_CONTAINS(key, query);
//         case TempRangeContained:
//             return CHECK_TIME_CONTAINED(key, query);
//         case TempPointContained:
//             return CHECK_TIME_POINT_CONTAINED(key, query);
//         case TempIdxRangeContained:
//             return CHECK_TIME_CONTAINED(key, query) && CHECK_ID_OVERLAP(key, query);
//         case TempIdxRangeOverlap:
//             return CHECK_TIME_OVERLAP(key, query) && CHECK_ID_OVERLAP(key, query);
//         case TempIdxRangeContains:
//             return CHECK_TIME_CONTAINS(key, query) && CHECK_ID_OVERLAP(key, query);
//         case TempIdxPointContained:
//             return CHECK_TIME_POINT_CONTAINED(key, query) && CHECK_ID_OVERLAP(key, query);
//         default:
//             elog(ERROR, "unrecognized strategy number: %d for bbox queries", strategy);
//             return false;
//         }
//     }
//     return CHECK_TIME_OVERLAP(key, query) && CHECK_ID_OVERLAP(key, query);
// }

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
    temporalKey     *out,
                    *tmp;
    int         numranges,
                i = 0;

    int *size = (int*) PG_GETARG_POINTER(1);

    numranges = entryvec->n;
    tmp = (temporalKey*) DatumGetPointer(ent[0].key);
    out = (temporalKey*) palloc(sizeof(temporalKey));
    memcpy(out, tmp, sizeof(temporalKey));
    // out = tmp;

    if (numranges == 1)
    {
        PG_RETURN_POINTER(out);
    }

    for (i = 1; i < numranges; i++)
    {
        tmp = (temporalKey*) DatumGetPointer(ent[i].key);

    
        entry_union(out, tmp, out);

        // out = my_union_implementation(out, tmp);
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
        *result = (v1->id_lower == v2->id_lower &&
                   v1->id_upper == v2->id_upper && 
                   v1->time_lower == v2->time_lower && 
                   v1->time_upper == v2->time_upper);
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