DROP TABLE IF EXISTS idx_test CASCADE;

CREATE TABLE idx_test (
    id int, -- Removed PK to allow multiple segments per ID
    data varchar(20),
    start_time timestamp,
    end_time timestamp
);

CREATE INDEX idx_temporal_gist ON idx_test 
USING gist (temporal_key(id, start_time, end_time));
