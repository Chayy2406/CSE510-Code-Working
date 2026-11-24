#!/usr/bin/env python3
"""Checks QC and QD against the SQLite oracle under many distinct plans.

PLAN.md Phase 8 acceptance: "QC and QD match SQLite." Reuses the same
(join_order, method1, method2, pushdown) plan sweep as
check_qb_plans.py, but wraps the join with GroupBy+HAVING (QC) or an
ORDER BY S.sid delivery check (QD, verifying both the oracle checksum
and that run_query's own OrderOk flag is true). Exits non-zero on any
mismatch.
"""
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


def run_query(run_query_bin, query, data_dir, join_order, method1, method2, pushdown,
              buffer_pages, rmin, amax, color, d1, d2, extra=()):
    out = subprocess.run(
        [
            run_query_bin,
            "--query", query,
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
            *extra,
        ],
        capture_output=True, text=True, check=True,
    ).stdout
    rows = int(re.search(r"Rows: (\d+)", out).group(1))
    checksum_hex = re.search(r"Checksum: 0x([0-9a-f]+)", out).group(1)
    order_ok_match = re.search(r"OrderOk: (true|false)", out)
    order_ok = order_ok_match.group(1) == "true" if order_ok_match else None
    return rows, int(checksum_hex, 16), order_ok


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
    parser.add_argument("--h", type=int, default=5)
    args = parser.parse_args()

    failures = []

    print("=== QC ===")
    oracle_rows, oracle_checksum = oc.oracle_qc(args.data_dir, args.rmin, args.amax, args.color,
                                                 args.d1, args.d2, args.h)
    print(f"Oracle: rows={oracle_rows} checksum=0x{oracle_checksum:x}\n")
    qc_count = 0
    for order, pushdown, method1, method2 in itertools.product(ORDERS, PUSHDOWNS, METHODS,
                                                                 METHODS):
        qc_count += 1
        rows, checksum, _ = run_query(args.run_query, "qc", args.data_dir, order, method1,
                                       method2, pushdown, args.buffer_pages, args.rmin,
                                       args.amax, args.color, args.d1, args.d2,
                                       extra=["--h", str(args.h)])
        ok = rows == oracle_rows and checksum == oracle_checksum
        if not ok:
            failures.append(("QC", order, pushdown, method1, method2))
        print(f"[{'OK' if ok else 'MISMATCH'}] order={order} pushdown={pushdown:3} "
              f"method1={method1:4} method2={method2:4} rows={rows}")

    print("\n=== QD ===")
    _, oracle_rows, oracle_checksum = oc.oracle_qd(args.data_dir, args.rmin, args.amax,
                                                    args.color, args.d1, args.d2)
    print(f"Oracle: rows={oracle_rows} checksum=0x{oracle_checksum:x}\n")
    qd_count = 0
    for order, pushdown, method1, method2 in itertools.product(ORDERS, PUSHDOWNS, METHODS,
                                                                 METHODS):
        qd_count += 1
        rows, checksum, order_ok = run_query(args.run_query, "qd", args.data_dir, order, method1,
                                              method2, pushdown, args.buffer_pages, args.rmin,
                                              args.amax, args.color, args.d1, args.d2)
        ok = rows == oracle_rows and checksum == oracle_checksum and order_ok
        if not ok:
            failures.append(("QD", order, pushdown, method1, method2))
        print(f"[{'OK' if ok else 'MISMATCH'}] order={order} pushdown={pushdown:3} "
              f"method1={method1:4} method2={method2:4} rows={rows} order_ok={order_ok}")

    print(f"\n{qc_count} QC plans + {qd_count} QD plans checked")
    if failures:
        print(f"{len(failures)} plan(s) mismatched the oracle: {failures}", file=sys.stderr)
        return 1
    print("All QC and QD plans match the SQLite oracle.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
