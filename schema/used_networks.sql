-- IPVarch used-networks schema (IPAM registry).
--
-- This file is the single source of truth for the UsedNetworks table.
-- It is compiled into the ipvarch binary at build time by the Makefile
-- (it becomes the generated schema_sql.c), which keeps the shipped
-- binary self-contained.  To change the schema: edit this file and
-- rebuild.
--
-- The table is created with "IF NOT EXISTS", so opening an already
-- initialised database is a no-op.

CREATE TABLE IF NOT EXISTS UsedNetworks (
 id INTEGER PRIMARY KEY AUTOINCREMENT,
 cidr TEXT NOT NULL UNIQUE,
 prefix INTEGER NOT NULL,
 start_hi INTEGER NOT NULL,
 start_lo INTEGER NOT NULL,
 end_hi INTEGER NOT NULL,
 end_lo INTEGER NOT NULL,
 label TEXT NOT NULL,
 created_at DATETIME DEFAULT CURRENT_TIMESTAMP)
