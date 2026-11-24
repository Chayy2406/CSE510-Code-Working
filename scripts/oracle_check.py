#!/usr/bin/env python3
"""Checks run_query's QA output against a SQLite oracle over the same CSVs.

Loads S.csv and R.csv from a data directory into an in-memory SQLite
database, runs the SQL equivalent of QA with the given constants, and
computes the same order-insensitive checksum the engine computes
(src/exec/checksum.h) over the oracle's rows. Then runs run_query with
matching constants, parses its printed row count and checksum, and
compares. Exits non-zero (and prints what differed) on any mismatch.
"""
import argparse
import csv
import re
import sqlite3
import subprocess
import sys

MASK64 = (1 << 64) - 1
FNV_OFFSET = 0xcbf29ce484222325
FNV_PRIME = 0x100000001b3


def tuple_hash(values):
    """Must match qe::TupleHash in src/exec/checksum.h exactly."""
    h = FNV_OFFSET
    for v in values:
        h ^= v & 0xFFFFFFFF
        h = (h * FNV_PRIME) & MASK64
    return h


def checksum(rows):
    total = 0
    for row in rows:
        total = (total + tuple_hash(row)) & MASK64
    return total


def load_csv(conn, table, path, columns):
    conn.execute(f"CREATE TABLE {table} ({', '.join(c + ' INTEGER' for c in columns)})")
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        rows = [tuple(int(row[c]) for c in columns) for row in reader]
    placeholders = ", ".join("?" for _ in columns)
    conn.executemany(f"INSERT INTO {table} VALUES ({placeholders})", rows)


def oracle_qa(data_dir, rmin, dmin):
    conn = sqlite3.connect(":memory:")
    load_csv(conn, "S", f"{data_dir}/S.csv", ["sid", "rating", "age"])
    load_csv(conn, "R", f"{data_dir}/R.csv", ["sid", "bid", "day"])
    cur = conn.execute(
        "SELECT S.sid, S.rating, R.bid, R.day FROM S JOIN R ON S.sid = R.sid "
        "WHERE S.rating > ? AND R.day >= ?",
        (rmin, dmin),
    )
    rows = cur.fetchall()
    conn.close()
    return len(rows), checksum(rows)


def run_engine(run_query_bin, data_dir, buffer_pages, rmin, dmin, method):
    out = subprocess.run(
        [
            run_query_bin,
            "--data-dir", data_dir,
            "--method", method,
            "--buffer-pages", str(buffer_pages),
            "--rmin", str(rmin),
            "--dmin", str(dmin),
        ],
        capture_output=True, text=True, check=True,
    ).stdout
    rows = int(re.search(r"Rows: (\d+)", out).group(1))
    checksum_hex = re.search(r"Checksum: 0x([0-9a-f]+)", out).group(1)
    return rows, int(checksum_hex, 16), out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", default="data")
    parser.add_argument("--run-query", default="build/src/run_query")
    parser.add_argument("--buffer-pages", type=int, default=10)
    parser.add_argument("--rmin", type=int, default=5)
    parser.add_argument("--dmin", type=int, default=1)
    parser.add_argument("--method", default="bnlj", choices=["bnlj", "smj", "ghj", "inlj"])
    args = parser.parse_args()

    oracle_rows, oracle_checksum = oracle_qa(args.data_dir, args.rmin, args.dmin)
    engine_rows, engine_checksum, engine_out = run_engine(
        args.run_query, args.data_dir, args.buffer_pages, args.rmin, args.dmin, args.method
    )

    ok = oracle_rows == engine_rows and oracle_checksum == engine_checksum
    print(engine_out.strip())
    print(f"Oracle rows={oracle_rows} checksum=0x{oracle_checksum:x}")
    print(f"Engine rows={engine_rows} checksum=0x{engine_checksum:x}")
    if ok:
        print("oracle_check: OK")
        return 0
    print("oracle_check: MISMATCH", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
