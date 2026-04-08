/*
 * contrib/temporal/temporal.c 
 */

#include "postgres.h"
#include "temporal.h"
#include "access/gist.h"

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


PG_FUNCTION_INFO_V1(my_compress);

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

        /* fill *compressed_data from entry->key ... */

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
