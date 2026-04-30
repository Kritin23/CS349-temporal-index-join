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

   1. Time period definitions use two standard table columns as the start and end of a named time period, with closed set-open set semantics. This provides compatibility with existing data models, application code, and tools

      CREATE TABLE employee_salary (
         emp_id INT,
         salary NUMERIC,
         valid_start DATE,
         valid_end DATE,
         PERIOD FOR valid_time (valid_start, valid_end),
         PRIMARY KEY (emp_id, valid_time WITHOUT OVERLAPS)
      );

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

Sample Queries:

// Create Temporal attribute

CREATE TABLE employee_salary (
    emp_id INT,
    salary NUMERIC,
    valid_start DATE,
    valid_end DATE,
    PERIOD FOR valid_time (valid_start, valid_end),
    PRIMARY KEY (emp_id, valid_time WITHOUT OVERLAPS)
);

INSERT INTO employee_salary
VALUES (1, 50000, DATE '2023-01-01', DATE '2023-06-01');

// Foreign key

CREATE TABLE project_assignment (
    emp_id INT,
    project_id INT,
    start_date DATE,
    end_date DATE,
    PERIOD FOR assignment_time (start_date, end_date),
    FOREIGN KEY (emp_id, assignment_time)
        REFERENCES employee_salary (emp_id, valid_time)
);

// Predicates

SELECT *
FROM employee_salary
WHERE valid_time CONTAINS DATE '2023-03-01';

SELECT *
FROM employee_salary
WHERE valid_time OVERLAPS PERIOD (
    DATE '2023-05-01',
    DATE '2023-08-01'
);

SELECT *
FROM employee_salary
WHERE valid_time EQUALS PERIOD (
    DATE '2023-01-01',
    DATE '2023-06-01'
);

SELECT *
FROM employee_salary
WHERE valid_time PRECEDES PERIOD (
    DATE '2023-06-01',
    DATE '2023-12-01'
);

SELECT *
FROM employee_salary
WHERE valid_time SUCCEEDS PERIOD (
    DATE '2022-01-01',
    DATE '2023-01-01'
);

SELECT e1.*
FROM employee_salary e1
JOIN employee_salary e2
ON e1.valid_time IMMEDIATELY PRECEDES e2.valid_time
AND e1.emp_id = e2.emp_id;

SELECT e1.*
FROM employee_salary e1
JOIN employee_salary e2
ON e1.valid_time IMMEDIATELY SUCCEEDS e2.valid_time
AND e1.emp_id = e2.emp_id;

// Updates

UPDATE employee_salary
SET salary = 70000
FOR PORTION OF valid_time
FROM DATE '2023-06-01' TO DATE '2023-09-01'
WHERE emp_id = 1;

DELETE FROM employee_salary
FOR PORTION OF valid_time
FROM DATE '2023-06-01' TO DATE '2023-09-01'
WHERE emp_id = 1;

// Create Temporal Index

CREATE TEMPORAL INDEX ON employee_salary(valid_time)

Good news:
   Since all I could find were extensions to postgres, and since extensions cannot mess up the grammar of postgres, possibly this has no existing implementation in postgres. 


// valid_time tstzrange GENERATED ALWAYS AS (tstzrange(start_date, end_date)) STORED
// Create another column of datatype tsrange. Use this column, as well as the other two columns. Depending on which column is used, appropriate index will be used.

Parser changes:
   1. Need to add period for, and add another column with type tsrange.
   2. Add operators like contains, overlaps, precedes, etc.
   3. An index is automatically created for each period, but an aggregate based index is not created for each period.
      Update the create index command to handle aggregate based indexing on a particular atttribute over a period.


Semantic analyser/Rewriter:
   1. Check whether contains and others contain only a period in their lhs.
   2. Check whether aggregate based indexing is being created on a period attribute only. If yes, then create it.

Planner:
   1. If a period is present in the group by clause, and aggregate column has an aggregate index on the period, we will use temporal index to get the aggregate.
   2. If a join is on a period, we need to do a temporal join.

https://chatgpt.com/share/69f09ca6-9998-83e8-8086-8281452dc008