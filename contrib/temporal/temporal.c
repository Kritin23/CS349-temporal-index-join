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



PG_FUNCTION_INFO_V1(temporal_compress);

Datum
temporal_compress(PG_FUNCTION_ARGS)
{
    GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
    GISTENTRY  *retval;

    if (entry->leafkey)
    {
        /* replace entry->key with a compressed version */
        temporalKey *key = palloc(sizeof(temporalKey));

        // ****** TODO ******
        // Figure out what do we get in entry, and how to fill 
        // it in temporalKey.

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