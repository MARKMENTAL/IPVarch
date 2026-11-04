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

## Testing (Make + python3, stdlib only)

```sh
make              # build (tcc, falls back to gcc)
make test         # full validity matrix: info, canonicalization, plan,
                  # random (statistical), invalid inputs, edge cases, usage
make test-sanitize  # rebuild with gcc/clang ASan+UBSan and run full matrix
make check        # strict C89 build: -std=c89 -pedantic -Wall -Wextra
make clean
```

Notes:

- The runner prints `PASS`/`FAIL` per case and exits nonzero on failure.
- Run a subset with `python3 tests/runner.py --bin ./ipvarch --filter plan`.
- Add cases as simple dict entries in `tests/cases.py`.
- Memory safety runs through compiler instrumentation (tcc has no
  sanitizer support, so `test-sanitize` uses gcc/clang); the runner
  detects sanitizer aborts explicitly and reports them per case.

## Exit codes

- `0` — success
- `1` — any error (bad address, bad prefix, bad usage); the message goes
  to stderr prefixed with `ipvarch: error:`.
