#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""IPVarch test runner.

Runs every case in cases.py against the IPVarch binary and checks
validity (exit codes, output contracts, structural table rules) and,
with --sanitize, memory safety by rebuilding ipvarch.c with
AddressSanitizer + UndefinedBehaviorSanitizer via gcc/clang.

Usage:
    python3 tests/runner.py [--bin ./ipvarch]
        [--sanitize] [--filter substring-of-group-or-case]
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cases import GROUPS  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAN_BIN = os.path.join("/tmp/ipvarchtests", "ipvarch-asan")
_usage_bin = None

# ---------------------------------------------------------------- table


def parse_table(out):
    """Parse a network-plan table into (rows dict, prefix or None)."""
    rows = {}
    prefix = None
    for line in out.splitlines():
        if not line.startswith("│"):
            continue
        if "IPVarch Network Plan" in line:      # title row spans both cells
            label = line.strip("│").strip()
            m = re.search(r"/(\d+)$", label)
            prefix = int(m.group(1)) if m else None
            rows["__title__"] = label
            continue
        cells = [c.strip() for c in line.split("│")]
        if len(cells) < 4:
            continue
        label, value = cells[1], cells[2]
        if label:
            rows[label] = value
    return rows, prefix


def v_table_layout(out):
    """Every non-title table row must have exactly 3 vertical bars."""
    rows, prefix = parse_table(out)
    if not rows.get("__title__"):
        return False, "no table title"
    if prefix is None:
        return False, "title missing prefix"
    for line in out.splitlines():
        if line.startswith("│") and "IPVarch Network Plan" not in line:
            if line.count("│") != 3:
                return False, "malformed row: %r" % line
    return True, ""


def v_table3_layout(out):
    """3-column list table: exactly one centred title row (2 bars) plus
    rows carrying exactly 4 bars.

    The title row is found by its shape (a single centred cell, so two
    bars) rather than by its wording, so renaming the header cannot
    break this check again.
    """
    titles = rows = 0
    for line in out.splitlines():
        if not line.startswith("│"):
            continue
        bars = line.count("│")
        if bars == 2:
            titles += 1
        elif bars == 4:
            rows += 1
        else:
            return False, "malformed row: %r" % line
    if titles != 1:
        return False, "expected one title row, found %d" % titles
    if rows < 1:
        return False, "no table rows"
    return True, ""


def _ip_u32(s):
    parts = [int(p) for p in s.split(".")]
    if len(parts) != 4 or any(p < 0 or p > 255 for p in parts):
        return None
    return (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]


def _u32_ip(u):
    return "%u.%u.%u.%u" % ((u >> 24) & 255, (u >> 16) & 255,
                            (u >> 8) & 255, u & 255)


EXPECTED_HOSTS = {8: "16777214", 16: "65534", 24: "254", 32: "1"}
PREFIX_MASK = {8: 0xFF000000, 16: 0xFFFF0000, 24: 0xFFFFFF00, 32: 0xFFFFFFFF}


def v_plan(out):
    ok, msg = v_table_layout(out)
    if not ok:
        return ok, msg
    rows, prefix = parse_table(out)
    labels = ["Network address", "Netmask", "Broadcast", "Usable host range",
              "Usable hosts", "Class", "Scope", "Gateway IP",
              "DNS/DHCP server", "DHCP pool"]
    for label in labels:
        if label not in rows:
            return False, "missing row %r" % label
    if rows["Usable hosts"] != EXPECTED_HOSTS.get(prefix):
        return False, ("usable hosts %r != %r" %
                       (rows["Usable hosts"], EXPECTED_HOSTS.get(prefix)))
    net = _ip_u32(rows["Network address"])
    if net is None:
        return False, "bad network address %r" % rows["Network address"]
    if prefix == 32:
        if not rows["Gateway IP"].startswith("n/a"):
            return False, "/32 gateway should be n/a: %r" % rows["Gateway IP"]
        if rows["DHCP pool"] != "disabled (single-host network)":
            return False, "/32 DHCP pool wrong: %r" % rows["DHCP pool"]
        return True, ""
    first = (net & PREFIX_MASK[prefix]) + 1
    if rows["Gateway IP"] != _u32_ip(first):
        return False, "gateway %r != first usable %r" % (rows["Gateway IP"],
                                                         _u32_ip(first))
    if rows["DNS/DHCP server"] != _u32_ip(first + 1):
        return False, "DNS/DHCP %r != second usable %r" % (
            rows["DNS/DHCP server"], _u32_ip(first + 1))
    base = rows["Network address"].rsplit(".", 1)[0]
    if rows["DHCP pool"] != base + ".100 - " + base + ".254":
        return False, "DHCP pool %r != %r" % (rows["DHCP pool"],
                                              base + ".100 - " + base + ".254")
    return True, ""


def _is_private_net(net, prefix):
    a = [(net >> 24) & 255, (net >> 16) & 255, (net >> 8) & 255, net & 255]
    if a[0] == 10 or a[0] == 192 and a[1] == 168:
        return True
    if a[0] == 172 and 16 <= a[1] <= 31:
        return True
    return False


def _is_public_net(net):
    a = [(net >> 24) & 255, (net >> 16) & 255, (net >> 8) & 255, net & 255]
    if a[0] in (0, 10, 127):
        return False
    if a[0] == 172 and 16 <= a[1] <= 31:
        return False
    if a[0] == 192 and a[1] == 168:
        return False
    if a[0] == 169 and a[1] == 254:
        return False
    if a[0] == 100 and 64 <= a[1] <= 127:
        return False
    if a[0] == 198 and a[1] in (18, 19):
        return False
    if a[0] == 192 and a[1] == 0 and a[2] == 2:
        return False
    if a[0] == 198 and a[1] == 51 and a[2] == 100:
        return False
    if a[0] == 203 and a[1] == 0 and a[2] == 113:
        return False
    if a[0] >= 224:
        return False
    return True


def _random_check(out, want_private):
    ok, msg = v_plan(out)
    if not ok:
        return ok, msg
    rows, prefix = parse_table(out)
    net = _ip_u32(rows["Network address"])
    shape = "zeroed" if prefix != 32 else "any"
    if want_private:
        if not rows["Scope"].startswith("Private"):
            return False, "scope should be Private: %r" % rows["Scope"]
        if not _is_private_net(net, prefix):
            return False, "network %r not private" % rows["Network address"]
    else:
        if not rows["Scope"].startswith("Public"):
            return False, "scope should be Public: %r" % rows["Scope"]
        if not _is_public_net(net):
            return False, ("network %r is in a reserved/special range" %
                           rows["Network address"])
    if prefix != 32:
        expect_net = net & PREFIX_MASK[prefix]
        if net != expect_net:
            return False, "host bits set in random network %r" % _u32_ip(net)
        expect_bc = net | (~PREFIX_MASK[prefix] & 0xFFFFFFFF)
        if rows["Broadcast"] != _u32_ip(expect_bc):
            return False, "broadcast %r wrong" % rows["Broadcast"]
    return True, ""


def v_usage_examples(out):
    """Every <addr>/<prefix> example in usage output must be planned OK
    when fed back to the binary."""
    bin_path = _usage_bin
    rows = []
    for m in re.finditer(r"(\d{1,3}(?:\.\d{1,3}){3})/(\d+)", out):
        rows.append((m.group(1), int(m.group(2))))
    if not rows:
        return False, "usage shows no <addr>/<prefix> examples"
    for net, prefix in rows:
        rc, out2, err2 = run_bin(bin_path, ["--plan-network",
                                            "%s/%d" % (net, prefix)])
        if rc != 0 or "IPVarch Network Plan" not in out2:
            return False, ("usage example %s/%d is not a plannable network "
                           "(exit=%d, stderr=%r)" %
                           (net, prefix, rc, err2.strip()))
    return True, ""


VALIDATORS = {
    "plan": v_plan,
    "plan-gateway": v_plan,
    "table": v_table_layout,
    "table3": v_table3_layout,
    "random-private": lambda out: _random_check(out, True),
    "random-public": lambda out: _random_check(out, False),
    "usage-examples": v_usage_examples,
}

# ---------------------------------------------------------------- run


def run_bin(bin_path, args, timeout=15):
    try:
        proc = subprocess.run([bin_path] + args, capture_output=True,
                              text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "TIMEOUT (%ss) on args=%r" % (timeout, args), ""
    except OSError as exc:
        return None, "cannot run binary: %s" % exc, ""
    return proc.returncode, proc.stdout, proc.stderr


def check_once(bin_path, case):
    rc, out, err = run_bin(bin_path, case["args"])
    if rc is None:
        return False, err
    if rc != case["exit"]:
        head = err.strip().splitlines()[0] if err.strip() else ""
        if any(s in err for s in ("AddressSanitizer", "LeakSanitizer",
                                  "runtime error:")):
            return False, "sanitizer abort (rc=%d): %s" % (rc, head)
        return False, "exit %d, expected %d; stderr=%r" % (rc, case["exit"],
                                                           head)
    for sub in case["must"]:
        hay = err if sub.startswith("stderr:") else out
        clean = sub[len("stderr:"):] if sub.startswith("stderr:") else sub
        if clean not in hay:
            return False, "output missing %r" % clean
    for sub in case["must_not"]:
        if sub in out:
            return False, "output unexpectedly contains %r" % sub
    ban = case.get("ban_network")
    if ban:
        rows, prefix = parse_table(out)
        if rows.get("Network address") == ban:
            return False, "generated registered network %s (must skip)" % ban
    if case["validator"]:
        ok, msg = VALIDATORS[case["validator"]](out)
        if not ok:
            return False, "validator %s: %s" % (case["validator"], msg)
    return True, ""


def run_case(bin_path, case):
    reps = case.get("repeat", 1)
    for i in range(reps):
        ok, msg = check_once(bin_path, case)
        if not ok:
            suffix = " (iteration %d/%d)" % (i + 1, reps) if reps > 1 else ""
            return False, msg + suffix
    return True, ""


def build_sanitize(repo):
    cc = shutil.which("gcc") or shutil.which("clang")
    if cc is None:
        return None, "no ASan-capable compiler (gcc/clang) found"
    san_dir = os.path.dirname(SAN_BIN)
    vendor_obj = os.path.join(san_dir, "sqlite3-asan.o")

    # schema_sql.c is generated from schema/used_networks.sql by the
    # Makefile; reuse that rule so there is one generator only.
    schema_gen = os.path.join(repo, "schema_sql.c")
    proc = subprocess.run(["make", "-s", "schema_sql.c"], cwd=repo,
                          capture_output=True, text=True)
    if proc.returncode != 0 or not os.path.exists(schema_gen):
        return None, "schema generation failed:\n%s" % proc.stderr.strip()

    # The Makefile owns the SQLite embedder defines (SQL_DEFS); ask it for
    # them instead of duplicating the list, so the sanitized build can never
    # end up configured differently from the real one.  With those defines
    # the amalgamation references no dlopen/pthread/fabs symbol, so no -l
    # flags are needed here -- and minimal systems may not even have libdl,
    # libpthread or libm available to link.
    proc = subprocess.run(["make", "-s", "print-flags"], cwd=repo,
                          capture_output=True, text=True)
    sql_defs = [f for f in proc.stdout.split() if f.startswith("-D")]
    if proc.returncode != 0 or not sql_defs:
        return None, "make print-flags failed:\n%s" % proc.stderr.strip()

    cmd = [cc, "-O1", "-g", "-fsanitize=address,undefined",
           "-fno-omit-frame-pointer"] + sql_defs + [
           "-c", os.path.join(repo, "vendor", "sqlite3.c"), "-o", vendor_obj]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        return None, "sanitized sqlite build failed:\n%s" % proc.stderr.strip()

    cmd = [cc, "-std=c89", "-O1", "-g",
           "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] + sql_defs + [
           os.path.join(repo, "ipvarch.c"),
           os.path.join(repo, "ipvarch_db.c"),
           os.path.join(repo, "ipvtbl.c"),
           schema_gen,
           vendor_obj, "-o", SAN_BIN]
    proc = subprocess.run(cmd, capture_output=True, text=True, cwd=repo)
    if proc.returncode != 0:
        return None, "sanitized build failed:\n%s" % proc.stderr.strip()
    return SAN_BIN, None


def main():
    global _usage_bin
    ap = argparse.ArgumentParser(description="IPVarch test runner")
    ap.add_argument("--bin", default=os.path.join(REPO, "ipvarch"))
    ap.add_argument("--sanitize", action="store_true",
                    help="rebuild with ASan+UBSan (gcc/clang) and rerun matrix")
    ap.add_argument("--filter", default=None,
                    help="only run groups/cases whose name contains this")
    ap.add_argument("--keep-db", action="store_true",
                    help="do not delete the temporary used-networks db")
    args = ap.parse_args()

    os.makedirs(os.path.dirname(SAN_BIN), exist_ok=True)
    memory = "not instrumented (run 'make test-sanitize' to enable)"

    tmp_db_dir = tempfile.mkdtemp(prefix="ipvarch-tests-")
    db_file = os.path.join(tmp_db_dir, "tests.db")
    os.environ["IPVARCH_DB"] = db_file

    if args.sanitize:
        bin_path, err = build_sanitize(REPO)
        if bin_path is None:
            print("sanitize: SKIPPED (%s)" % err)
            return 0
        memory = "ASan+UBSan instrumentation active"
    else:
        bin_path = args.bin

    _usage_bin = bin_path

    if not os.path.exists(bin_path):
        print("error: binary not found: %s (run 'make' first)" % bin_path,
              file=sys.stderr)
        return 1

    passed = failed = 0
    failures = []
    ran_any = False

    for gname, cases in GROUPS:
        if args.filter and args.filter not in gname and \
           not any(args.filter in case["name"] for case in cases):
            continue
        print("== %s ==" % gname)
        for case in cases:
            if args.filter and args.filter not in case["name"]:
                continue
            ran_any = True
            ok, msg = run_case(bin_path, case)
            if ok:
                passed += 1
                print("  PASS  %s" % case["name"])
            else:
                failed += 1
                failures.append((gname, case["name"], msg))
                print("  FAIL  %s\n        %s" % (case["name"], msg))
        print()

    if not ran_any:
        print("error: filter %r matched nothing" % args.filter, file=sys.stderr)
        return 1

    print("Summary: %d passed, %d failed  [binary: %s]" %
          (passed, failed, bin_path))
    print("         memory safety: %s" % memory)
    print("         used networks db: %s" % db_file)
    if args.keep_db:
        print("         (kept for inspection: %s)" % tmp_db_dir)
    else:
        shutil.rmtree(tmp_db_dir, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
