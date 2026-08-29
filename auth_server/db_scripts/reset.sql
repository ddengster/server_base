-- Reset the users table and its registration-source enum.
-- Run: psql -U postgres -h localhost -d postgres -f db_scripts/reset.sql
--   (connect to the 'postgres' maintenance database; this script creates
--    the app database if it does not exist, then switches to it)
-- Idempotent: safe to re-run.
-- Override the database name with: psql -d postgres -v appdb=mydb -f db_scripts/reset.sql

\set appdb 'tn_unyielding'

SELECT 'CREATE DATABASE ' || :'appdb'
WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = :'appdb')\gexec

\connect :appdb

DROP TABLE IF EXISTS users CASCADE;
DROP TYPE IF EXISTS user_registration_source CASCADE;

CREATE TYPE user_registration_source AS ENUM ('direct', 'steam');

CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TABLE users
(
  user_id       BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
  source        user_registration_source NOT NULL,
  source_user_id TEXT,
  username      TEXT,
  email         TEXT,
  password_hash TEXT,
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
  last_login    TIMESTAMPTZ,
  last_verify   TIMESTAMPTZ,
  roles         TEXT[] NOT NULL DEFAULT '{}',
  CONSTRAINT users_source_user_id_unique UNIQUE (source, source_user_id),
  CONSTRAINT user_id_criteria CHECK ((source = 'direct' AND username IS NOT NULL) OR (source != 'direct' AND source_user_id IS NOT NULL))
);

-- direct users are identified by username (steam users by source_user_id)
CREATE UNIQUE INDEX IF NOT EXISTS users_direct_username_unique
  ON users (username) WHERE source = 'direct';

-- non-direct users are looked up by their source + source_user_id
CREATE UNIQUE INDEX IF NOT EXISTS users_source_user_id_idx
  ON users (source, source_user_id) WHERE source != 'direct';