-- Seed test users for development. Run after reset.sql (which creates the
-- 'tn_unyielding' database by default; override with -v appdb=<name>).
-- Run: psql -U postgres -h localhost -d tn_unyielding -f db_scripts/seed_test_users.sql
-- Plaintext passwords for testing:
--   direct_test / direct-pass-123
-- Steam users authenticate via their platform ID; no password stored.

INSERT INTO users (source, source_user_id, username, email, password_hash, roles)
VALUES
  ('steam', '76561198000000001', 'steam_test', NULL, NULL, ARRAY['player']),
  ('direct', NULL, 'direct_test', 'direct@example.com',
   crypt('direct-pass-123', gen_salt('bf')), ARRAY['admin', 'player'])
ON CONFLICT DO NOTHING;