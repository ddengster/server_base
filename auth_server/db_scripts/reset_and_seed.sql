-- Command:
-- psql -U postgres -h localhost -d postgres -f db_scripts/reset_and_seed.sql

-- Sample output:
-- DROP TABLE
-- DROP TYPE
-- CREATE TYPE
-- psql:db_scripts/reset.sql:20: NOTICE:  extension "pgcrypto" already exists, skipping
-- CREATE EXTENSION
-- CREATE TABLE
-- INSERT 0 2

\ir reset.sql
\ir seed_test_users.sql