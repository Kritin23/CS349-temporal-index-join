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

typedef struct tsRange {
    Timestamp start; 
    Timestamp end; 
} tsRange;

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
tsrange_consistent(temporalKey* key, tsRange* query, StrategyNumber strategy);
static bool 
ts_consistent(temporalKey* key, Timestamp* query, StrategyNumber strategy);
static bool 
idx_point_consistent(temporalKey* key, idxPointQuery* query, StrategyNumber strategy);
static bool 
idx_range_consistent(temporalKey* key, idxQuery* query, StrategyNumber strategy);
static bool 
bbox_consistent(temporalKey* key, temporalKey* query, StrategyNumber strategy);



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

}

/**
 * This function will handle queries where query data is a single time point.
 * e.g. get all tuples containing T.
 */
static bool 
ts_consistent(temporalKey* key, Timestamp* query, StrategyNumber strategy)
{

}

/**
 * This function will handle queries where query data an idx and a time point
 * e.g. get all tuples with primary key K containing T.
 */
static bool 
idx_point_consistent(temporalKey* key, idxPointQuery* query, StrategyNumber strategy)
{

}

/**
 * This function will handle queries where query data an idx and a time range
 * e.g. get all tuples with primary key K overlapping [start, end].
 */
static bool 
idx_range_consistent(temporalKey* key, idxQuery* query, StrategyNumber strategy)
{

}

/**
 * This functions check consistent based on query types. Each recieves a 
 * bounding box to check from its caller.
 * Strategy specifies whether it is OVERLAP, CONTAINED, or CONTAINS
 */

static bool 
bbox_consistent(temporalKey* key, temporalKey* query, StrategyNumber strategy)
{

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
    temporalKey     *out,
                    *tmp,
                    *old;
    int         numranges,
                i = 0;

    numranges = entryvec->n;
    tmp = (temporalKey*) DatumGetPointer(ent[0].key);
    out = tmp;

    if (numranges == 1)
    {
        out = (temporalKey*) palloc(sizeof(temporalKey));
        memcpy(out, tmp, sizeof(temporalKey));
        PG_RETURN_POINTER(out);
    }

    for (i = 1; i < numranges; i++)
    {
        old = out;
        tmp = (temporalKey*) DatumGetPointer(ent[i].key);

        out = (temporalKey*) palloc(sizeof(temporalKey));
    
        entry_union(old, tmp, out);

        // out = my_union_implementation(out, tmp);
    }

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
    OffsetNumber maxoff = entryvec->n - 1;
    GISTENTRY  *ent = entryvec->vector;
    int         i,
                nbytes;
    OffsetNumber *left,
               *right;
    temporalKey *tmp_union;
    temporalKey *unionL;
    temporalKey *unionR;
    GISTENTRY **raw_entryvec;

    maxoff = entryvec->n - 1;
    nbytes = (maxoff + 1) * sizeof(OffsetNumber);

    v->spl_left = (OffsetNumber *) palloc(nbytes);
    left = v->spl_left;
    v->spl_nleft = 0;

    v->spl_right = (OffsetNumber *) palloc(nbytes);
    right = v->spl_right;
    v->spl_nright = 0;

    unionL = NULL;
    unionR = NULL;

    /* Initialize the raw entry vector. */
    raw_entryvec = (GISTENTRY **) malloc(entryvec->n * sizeof(void *));
    for (i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
        raw_entryvec[i] = &(entryvec->vector[i]);

    for (i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
    {
        int         real_index = raw_entryvec[i] - entryvec->vector;

        tmp_union = (temporalKey*) DatumGetPointer(entryvec->vector[real_index].key);
        Assert(tmp_union != NULL);

        /*
         * Choose where to put the index entries and update unionL and unionR
         * accordingly. Append the entries to either v->spl_left or
         * v->spl_right, and care about the counters.
         */

        if (i < maxoff/2)
        {
            if (unionL == NULL)
                unionL = tmp_union;
            else
                entry_union(unionL, tmp_union, unionL);

            *left = real_index;
            ++left;
            ++(v->spl_nleft);
        }
        else
        {
            /*
             * Same on the right
             */
            if (unionR == NULL)
                unionR = tmp_union;
            else
                entry_union(unionR, tmp_union, unionR);

            *right = real_index;
            ++right;
            ++(v->spl_nright);
        }
    }

    v->spl_ldatum = (temporalKey*)DatumGetPointer(unionL);
    v->spl_rdatum = (temporalKey*)DatumGetPointer(unionR);
    PG_RETURN_POINTER(v);
}