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


## References
 - https://chatgpt.com/share/69d62346-a44c-8323-96dd-7020d41ea23b
   


