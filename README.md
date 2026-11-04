# IPVarch — Internet Protocol Version Architect

A tiny ANSI C (C89, tcc-friendly) command-line tool for inspecting and
planning IP addresses and IPv4 networks. Rules follow `ipaddrinfo.html`
(IPv4 classes A–E, RFC 1918 private ranges, IPv6 unicast/multicast/
link-local/unique-local).

## Usage

```text
ipvarch <address>                              inspect an IPv4 or IPv6 address
ipvarch --plan-network <ipv4>/<prefix>         plan a network (/8 /16 /24 /32)
ipvarch --random-ipv4network <scope> <prefix>  random valid network plan
ipvarch --use-network <cidr> <label>           register a used network
ipvarch --list-networks                        list registered networks
ipvarch --forget-network <cidr>                unregister a network
```

### Examples

```
$ ipvarch 192.168.1.1
Address: 192.168.1.1
Type:    IPv4
Class:   C
Scope:   Private (RFC 1918)
Octets:  192, 168, 1, 1

$ ipvarch 2001:db8::1
Address: 2001:db8::1
Type:    IPv6
Class:   n/a (IPv6 has no address classes)
Scope:   Public (global unicast)
Groups:  2001 0db8 0000 0000 0000 0000 0000 0001
Octets:  32, 1, 13, 184, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1

$ ipvarch --plan-network 192.168.1.0/24
┌───────────────────┬───────────────────────────────┐
│       IPVarch Network Plan - 192.168.1.0 /24      │
├───────────────────┼───────────────────────────────┤
│ Network address   │ 192.168.1.0                   │
│ Netmask           │ 255.255.255.0 /24             │
│ Broadcast         │ 192.168.1.255                 │
│ Usable host range │ 192.168.1.1 - 192.168.1.254   │
│ Usable hosts      │ 254                           │
│ Class             │ C                             │
│ Scope             │ Private (RFC 1918)            │
│ Gateway IP        │ 192.168.1.1                   │
│ DNS/DHCP server   │ 192.168.1.2                   │
│ DHCP pool         │ 192.168.1.100 - 192.168.1.254 │
└───────────────────┴───────────────────────────────┘
```

`--plan-network` takes literal CIDR masks (`/8`=255.0.0.0, `/16`, `/24`,
`/32`=single host). Host bits must be zero (it tells you the canonical
network address otherwise). The gateway is the first usable host, the
DNS/DHCP server the second, and the DHCP pool is the fixed last-octet
`.100`–`.254` range.

`--random-ipv4network private|public /8|/16|/24|/32` generates a random
valid network constrained to real private space (10/8, 172.16/12,
192.168/16) or public/global-unicast space (never multicast, loopback,
reserved, or documentation ranges) and prints the same plan table.
Networks registered in the used-networks database (below) are skipped.

## Used-networks database (IPAM)

IPVarch keeps a small SQLite-backed registry of networks that are
already in use, so plans and random picks avoid them:

```sh
$ ipvarch --use-network 10.164.0.0/16 "Main Homelab Bridge"
Registered 10.164.0.0/16 as "Main Homelab Bridge" in .../networks.db

$ ipvarch --plan-network 10.164.9.0/24     # gains a row when overlapping
│ Used by           │ 10.164.0.0/16 ("Main Homelab Bridge") │

$ ipvarch --list-networks                  # 3-column overview table
$ ipvarch --forget-network 10.164.0.0/16
Removed 10.164.0.0/16 (was "Main Homelab Bridge")
```

Details:

- `--use-network` accepts any prefix from `/0` to `/32`; the address is
  canonicalized (host bits must be zero) and stored with the label (max
  64 characters).
- Any `--plan-network` that intersects a registered network shows a
  `Used by:` row naming it (plus `+N more` for additional overlaps).
- `--random-ipv4network` rejects (and retries around) registered
  ranges; if no free network exists it errors out after 10000 attempts.
- Registration is idempotent-checked: registering the exact same cidr
  again is a warning and exit code `2`. Forgetting a cidr that is not
  registered (or registered under a different prefix length) is also a
  warning + exit code `2`.

### Database location

The database is a standard SQLite file, resolved as:

1. `$IPVARCH_DB` — explicit path (used verbatim, created as needed);
2. `$XDG_CONFIG_HOME/ipvarch/networks.db`;
3. `~/.config/ipvarch/networks.db` (the `ipvarch` directory is created
   automatically when registering a network).

Read-only modes (`--plan-network`, `--list-networks`, random) never
create the file; if no database exists they simply behave as if nothing
is registered. A database that cannot be opened is ignored with a
stderr warning. Access is read-only for registration-free commands and
read/write (creating the schema `UsedNetworks`) for register/forget.

### Schema

The table layout lives in `schema/used_networks.sql`, the single source
of truth. The Makefile compiles it into the binary at build time (as
the generated `schema_sql.c`) — a purely development convenience: the
shipped binary stays self-contained, and changing the schema means
editing the SQL file and rebuilding. It is created with `IF NOT
EXISTS`, so re-opening an initialised database is a no-op:

```sql
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
```

Column reference:

| Column | Meaning |
|---|---|
| `id` | autoincrement row key, present but unused by the tool logic |
| `cidr` | canonical network text (`a.b.c.d/p`); `UNIQUE` — re-registering the same network fails here with a constraint violation (warning + exit 2) |
| `prefix` | prefix length as registered (the registry accepts 0–32) |
| `start_hi`, `start_lo` | first address of the covered range, split into two 16-bit halves |
| `end_hi`, `end_lo` | last address of the covered range (network or-ed with the host bits) |
| `label` | free-text label, max 64 characters, shown by `--list-networks` and the plan `Used by:` row |
| `created_at` | registration timestamp (`CURRENT_TIMESTAMP` default) |

Design notes:

- The 32-bit network range is stored as two 16-bit `INTEGER` halves
  instead of one 64-bit value: the C code computes addresses in
  unsigned 32-bit math, and this split keeps the SQLite marshaling
  explicit (`start_hi << 16 | start_lo` on the way out).
- Overlap detection is plain interval intersection over `[start, end]`
  performed in C (`scan_used`); the table needs no indexes or triggers
  for correctness.

## Testing (Make + python3, stdlib only)

```sh
make              # build (tcc, falls back to gcc)
make test         # full validity matrix: info, canonicalization, plan,
                  # random (statistical), used networks, invalid inputs,
                  # edge cases, usage
make test-sanitize  # rebuild with gcc/clang ASan+UBSan and run full matrix
make check        # strict C89 build: -std=c89 -pedantic -Wall -Wextra
make clean
```

Notes:

- The runner prints `PASS`/`FAIL` per case and exits nonzero on failure.
- Run a subset with `python3 tests/runner.py --bin ./ipvarch --filter plan`.
- Add cases as simple dict entries in `tests/cases.py`.
- Each run hands the test binary a fresh temporary `IPVARCH_DB`
  (use `--keep-db` to inspect it afterwards), so runs are isolated and
  your personal databases are never touched.
- Memory safety runs through compiler instrumentation (tcc has no
  sanitizer support, so `test-sanitize` uses gcc/clang); the runner
  detects sanitizer aborts explicitly and reports them per case.

## Exit codes

- `0` — success
- `1` — any error (bad address, bad prefix, bad usage); the message goes
  to stderr prefixed with `ipvarch: error:`.
- `2` — warning paths of the database commands: duplicate registration
  (`--use-network` of an already-registered cidr) and forgetting a
  network that is not registered.

## Layout and dependencies

- `ipvarch.c` — modes, argument parsing, address math.
- `ipvtbl.c/.h` — shared UTF-8 box-table renderer.
- `ipvarch_db.c/.h` — used-networks SQLite backend.
- `vendor/sqlite3.c`, `vendor/sqlite3.h` — vendored SQLite
  amalgamation 3.54.1 from https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip
  (public domain — free for any use, no attribution required); compiled separately with gcc and linked in, so the
  only third-party dependency is vendored and there is nothing to
  install.

### Code style

I write IPVarch in what I'll call a Stroustrup hybrid: an opening brace
always stays inline with the statement it belongs to (function
definitions included), while `else` always drops to its own line after
the closing brace of the block it follows.

```c
static int parse_ipv4(const char *s, byte out[4]) {
    if (ok) {
        return 1;
    }
    else {
        return 0;
    }
}
```

A floating `{` on its own line wastes vertical space, and a jammed
`} else {` buries the branch in the middle of a line — this way I keep
the density and still see every branch entry running down the left
margin. It is formatting only, so no behaviour rests on it, but please
keep the convention when patching.

## License

IPVarch — the program and its test suite (`ipvarch.c`, `ipvtbl.c`,
`ipvtbl.h`, `ipvarch_db.c`, `ipvarch_db.h`, `tests/`) — is licensed
under the GNU General Public License version 3 or (at your option) any
later version; see the `LICENSE` file for the full text.

Copyright © 2026 Mark Robillard Jr (MARKMENTAL)

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

Notes on other material in this repository:

- `vendor/sqlite3.c` and `vendor/sqlite3.h` are the public-domain
  SQLite amalgamation and remain unmodified and public domain; linking
  them imposes no license conditions and they are not covered by the
  GPL notice above.
- `ipaddrinfo.html` is retained reference material describing the rules
  IPVarch implements; it is not part of the IPVarch source and not
  covered by this license.
