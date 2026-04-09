/*
 * contrib/temporal/temporal.c 
 */

#include "postgres.h"
#include "temporal.h"
#include "access/gist.h"
#include "access/stratnum.h"
#include "utils/float.h"
#include "utils/fmgrprotos.h"

typedef struct temporalKey {
    int64 id_lower;
    int64 id_upper;
    Timestamp time_lower;
    Timestamp time_upper;
} temporalKey;

typedef struct leafKey {
    int64 id;
    Timestamp start;
    Timestamp end;
} leafKey;

typedef struct tsRange {
    Timestamp start; 
    Timestamp end; 
} tsRange;

typedef struct idxPointQuery {
    int64 id;
    Timestamp time;
} idxPointQuery;

typedef struct idxQuery {
    int64 id;
    Timestamp   start,
                end;
} idxQuery;

static bool 
tsrange_consistent(temporalKey* key, tsRange* query, StrategyNumber strategy);
static bool 
ts_consistent(temporalKey* key, Timestamp* query, StrategyNumber strategy);
static bool 
idx_point_consistent(temporalKey* key, idxPointQuery* query, StrategyNumber strategy);
static bool 
idx_range_consistent(temporalKey* key, idxQuery* query, StrategyNumber strategy);
static bool 
bbox_consistent(temporalKey* key, temporalKey* query, StrategyNumber strategy);

#define CHECK_TIME_OVERLAP(key, query) ((key)->time_lower <= (query)->time_upper && (key)->time_upper >= (query)->time_lower)
#define CHECK_TIME_CONTAINED(key, query) ((key)->time_lower >= (query)->time_lower && (key)->time_upper <= (query)->time_upper)
#define CHECK_TIME_CONTAINS(key, query) ((key)->time_lower <= (query)->time_lower && (key)->time_upper >= (query)->time_upper)
#define CHECK_TIME_POINT_CONTAINED(key, query) ((query) >= (key)->time_lower && (query) <= (key)->time_upper)
#define CHECK_ID_OVERLAP(key, query) ((key)->id_lower <= (query)->id && (key)->id_upper >= (query)->id)

PG_FUNCTION_INFO_V1(temporal_compress);
PG_FUNCTION_INFO_V1(temporal_consistent);
PG_FUNCTION_INFO_V1(temporal_same);


Datum
temporal_compress(PG_FUNCTION_ARGS)
{
    GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    GISTENTRY  *retval;

    if (entry->leafkey)
    {
        /* replace entry->key with a compressed version */
        temporalKey *key = palloc(sizeof(temporalKey));
        leafKey *entry_data = (leafKey *)(entry->key);
        
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
    Datum  query = PG_GETARG_DATA_TYPE_P(1);
    StrategyNumber strategy = (StrategyNumber) PG_GETARG_UINT16(2);
    /* Oid subtype = PG_GETARG_OID(3); */
    bool       *recheck = (bool *) PG_GETARG_POINTER(4);
    temporalKey  *key = DatumGetDataType(entry->key);
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
tsrange_consistent(temporalKey* key, tsRange* query, StrategyNumber strategy)
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
ts_consistent(temporalKey* key, Timestamp* query, StrategyNumber strategy)
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
idx_point_consistent(temporalKey* key, idxPointQuery* query, StrategyNumber strategy)
{
    if (GIST_LEAF(entry))
    {
        switch(strategy)
        {
            case TempIdxPointContained:
                return CHECK_TIME_POINT_CONTAINED(key, query) && CHECK_ID_OVERLAP(key, query);
            default:
                elog(ERROR, "unrecognized strategy number: %d for idx point queries", strategy);
                return false;
        }
    }
    return CHECK_TIME_POINT_CONTAINED(key, query) && CHECK_ID_OVERLAP(key, query);
}

/**
 * This function will handle queries where query data an idx and a time range
 * e.g. get all tuples with primary key K overlapping [start, end].
 */
static bool 
idx_range_consistent(temporalKey* key, idxQuery* query, StrategyNumber strategy)
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

static bool 
bbox_consistent(temporalKey* key, temporalKey* query, StrategyNumber strategy)
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
        case TempPointContained:
            return CHECK_TIME_POINT_CONTAINED(key, query);
        case TempIdxRangeContained:
            return CHECK_TIME_CONTAINED(key, query) && CHECK_ID_OVERLAP(key, query);
        case TempIdxRangeOverlap:
            return CHECK_TIME_OVERLAP(key, query) && CHECK_ID_OVERLAP(key, query);
        case TempIdxRangeContains:
            return CHECK_TIME_CONTAINS(key, query) && CHECK_ID_OVERLAP(key, query);
        case TempIdxPointContained:
            return CHECK_TIME_POINT_CONTAINED(key, query) && CHECK_ID_OVERLAP(key, query);
        default:
            elog(ERROR, "unrecognized strategy number: %d for bbox queries", strategy);
            return false;
        }
    }
    return CHECK_TIME_OVERLAP(key, query) && CHECK_ID_OVERLAP(key, query);
}


Datum
my_same(PG_FUNCTION_ARGS)
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