#!/usr/bin/env python3
import argparse
import itertools
import re
import subprocess
import sys

sys.path.append("scripts")
import oracle_check as oc

METHODS = ["bnlj", "smj", "ghj", "inlj"]
ORDERS = ["order1", "order2"]
PUSHDOWNS = ["on", "off"]

def run_qb(run_query_bin, data_dir, join_order, method1, method2, pushdown, buffer_pages,
           rmin, amax, color, d1, d2):
    out = subprocess.run(
        [
            run_query_bin,
            "--query", "qb",
            "--data-dir", data_dir,
            "--join-order", join_order,
            "--method1", method1,
            "--method2", method2,
            "--pushdown", pushdown,
            "--buffer-pages", str(buffer_pages),
            "--rmin", str(rmin),
            "--amax", str(amax),
            "--color", str(color),
            "--d1", str(d1),
            "--d2", str(d2),
        ],
        capture_output=True, text=True, check=True,
    ).stdout
    rows = int(re.search(r"Rows: (\d+)", out).group(1))
    checksum_hex = re.search(r"Checksum: 0x([0-9a-f]+)", out).group(1)
    return rows, int(checksum_hex, 16)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", default="data")
    parser.add_argument("--run-query", default="build/src/run_query")
    parser.add_argument("--buffer-pages", type=int, default=15)
    parser.add_argument("--rmin", type=int, default=4)
    parser.add_argument("--amax", type=int, default=60)
    parser.add_argument("--color", type=int, default=1)
    parser.add_argument("--d1", type=int, default=50)
    parser.add_argument("--d2", type=int, default=300)
    args = parser.parse_args()

    oracle_rows, oracle_checksum = oc.oracle_qb(args.data_dir, args.rmin, args.amax, args.color,
                                                 args.d1, args.d2)
    print(f"Oracle: rows={oracle_rows} checksum=0x{oracle_checksum:x}\n")

    methods_seen = set()
    plan_count = 0
    failures = []
    for order, pushdown, method1, method2 in itertools.product(ORDERS, PUSHDOWNS, METHODS,
                                                                 METHODS):
        plan_count += 1
        methods_seen.add(method1)
        methods_seen.add(method2)
        rows, checksum = run_qb(args.run_query, args.data_dir, order, method1, method2,
                                 pushdown, args.buffer_pages, args.rmin, args.amax, args.color,
                                 args.d1, args.d2)
        ok = rows == oracle_rows and checksum == oracle_checksum
        status = "OK" if ok else "MISMATCH"
        print(f"[{status}] order={order} pushdown={pushdown:3} method1={method1:4} "
              f"method2={method2:4} rows={rows}")
        if not ok:
            failures.append((order, pushdown, method1, method2, rows, checksum))

    print(f"\n{plan_count} plans checked, methods seen: {sorted(methods_seen)}")
    if failures:
        print(f"{len(failures)} plan(s) mismatched the oracle", file=sys.stderr)
        return 1
    if plan_count < 12 or methods_seen != set(METHODS):
        print("did not meet the >=12 distinct plans / all four methods requirement",
              file=sys.stderr)
        return 1
    print("All QB plans match the SQLite oracle.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
