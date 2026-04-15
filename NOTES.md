## Query Paths
### CREATE INDEX
 - `exec_simple_query`
 - __Planner__: 
      
 - __Executor__: 
    - `PortalRun`
    - `PortalRunMulti`
    - `PortalRunUtility`
    - `ProcessUtility`
    - `standard_ProcessUtility`
    - `ProcessUtilitySlow` - line `1452`
    - `DefineIndex`
    - `index_create`
    - `index_build`
    - `ambuild`



## Implementation

 - __Planner__:

   No edits needed here hopefully. GPT says we can have create index queries 
   of the form
      `CREATE INDEX ... USING am`
   which will create the index using our access method 

 - __Executor__

   GPT says the easiest way to add support for R-tree is to use Gist. We will
   need to implement a new operator for Gist, and it will handle the tree 
   creation/scan. 
   
   (https://www.postgresql.org/docs/current/gist.html - See Section 65.2.3)
   
   For example on how to extend Gist, see contrib/btree_gist
   For the Gist API, see src/include/access/gist.h

   We should aim to complete this before checkpoint.

   Join algorithm - loop over one relation, and query the other relation

   For aggregates, we need a separate index. Nothing that exists will work. 
   For this, see nbtree.c or gist.c. They define am handlers. We will do this 
   last, don't bother now


### Gist Notes
 - Create a struct which will be stored in Gist Tree internal nodes
 - Create the functions (`union`, `consistent`, `compress`, etc)
 - Write SQL code to tell Gist about our code
 - Test our code 

 work in contrib/temporal directory

 **Gist TODOS**
 - need to make a composite type (id, [start, end]) 


## References
 - https://chatgpt.com/share/69d62346-a44c-8323-96dd-7020d41ea23b
   


Next steps:

   This is the interface specified in the SQL standard. Let's go with this as our interface too.
   Reference: https://en.wikipedia.org/wiki/SQL:2011

   1. Time period definitions use two standard table columns as the start and end of a named time period, with closed      set-open set semantics. This provides compatibility with existing data models, application code, and tools
   
   2. Definition of application time period tables (elsewhere called valid time tables), using the PERIOD FOR annotation
   
   3. Update and deletion of application time rows with automatic time period splitting
   Temporal primary keys incorporating application time periods with optional non-overlapping constraints via the WITHOUT OVERLAPS clause
   
   4. Application time tables are queried using regular query syntax or using new temporal predicates for time periods including CONTAINS, OVERLAPS, EQUALS, PRECEDES, SUCCEEDS, IMMEDIATELY PRECEDES and IMMEDIATELY SUCCEEDS (which are modified versions of Allen’s interval relations)

   5. Optional : Temporal referential integrity constraints for application time tables
   

Changes to be done:
   a. Change the grammar to allow for these keywords inside the CREATE TABLE and selection queries.
   b. Integrate these changes into the query rewriter.
   c. Integrate these changes inside the planner to include temporal indexing in its plan.
   d. Need to change the (CREATE INDEX USING gist...) command to (CREATE TEMPORAL INDEX ON period) for our usecase.

Good news:
   Since all I could find were extensions to postgres, and since extensions cannot mess up the grammar of postgres, possibly this has no existing implementation in postgres. 