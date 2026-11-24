#!/usr/bin/env python3
"""Minimal experiment runner (PLAN.md Phase 6).

Given one config JSON, this: generates data deterministically (gen_data),
runs QA under each configured join method at a fixed buffer size (the
"showcase" row set for Plan/Time/Reads/Writes), runs one method across a
buffer-size sweep, oracle-checks every single run against SQLite, writes
one CSV row per run (SPEC.md section 10's columns — est_io/est_out_rows
are left blank until the cost estimator exists in a later phase), prints
the four-row showcase table, and saves one sweep graph to figures/.

Usage: python3 scripts/run_experiments.py configs/qa_methods.json
Exits non-zero if any oracle check fails.
"""
import csv
import json
import os
import re
import subprocess
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_check as oc

CSV_FIELDS = [
    "experiment", "query", "plan_id", "plan_desc", "dataset_id", "seed", "scale",
    "key_dist", "zipf_theta", "corr", "order", "B", "run", "time_ms", "page_reads",
    "page_writes", "reads_base", "reads_index", "reads_temp", "writes_temp",
    "buffer_hits", "tuples_out", "checksum", "est_io", "est_out_rows",
    "ghj_overflow_partitions", "oracle_ok",
]


def run_query(run_query_bin, data_dir, method, buffer_pages, rmin, dmin):
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

    def grab(pattern, cast=int):
        m = re.search(pattern, out)
        if m is None:
            raise ValueError(f"run_query output missing '{pattern}':\n{out}")
        return cast(m.group(1))

    return {
        "time_ms": grab(r"Time: ([\d.]+)", float),
        "page_reads": grab(r"Reads: (\d+)"),
        "page_writes": grab(r"Writes: (\d+)"),
        "reads_base": grab(r"ReadsBase: (\d+)"),
        "reads_index": grab(r"ReadsIndex: (\d+)"),
        "reads_temp": grab(r"ReadsTemp: (\d+)"),
        "writes_temp": grab(r"WritesTemp: (\d+)"),
        "buffer_hits": grab(r"BufferHits: (\d+)"),
        "ghj_overflow_partitions": grab(r"GhjOverflowPartitions: (\d+)"),
        "tuples_out": grab(r"Rows: (\d+)"),
        "checksum": grab(r"Checksum: 0x([0-9a-f]+)", lambda s: int(s, 16)),
    }


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <config.json>", file=sys.stderr)
        return 2
    with open(sys.argv[1]) as f:
        config = json.load(f)

    gen_data_bin = config.get("gen_data_bin", "build/src/gen_data")
    run_query_bin = config.get("run_query_bin", "build/src/run_query")
    data_dir = config.get("data_dir", f"data/{config['dataset_id']}")
    results_path = config.get("results_csv", f"results/{config['experiment']}.csv")
    figures_dir = config.get("figures_dir", "figures")

    os.makedirs(os.path.dirname(results_path) or ".", exist_ok=True)
    os.makedirs(figures_dir, exist_ok=True)

    subprocess.run(
        [
            gen_data_bin,
            "--out-dir", data_dir,
            "--seed", str(config["seed"]),
            "--scale", str(config["scale"]),
            "--key-dist", config.get("key_dist", "uniform"),
            "--zipf-theta", str(config.get("zipf_theta", 1.0)),
            "--corr", str(config.get("corr", 0.0)),
            "--order", config.get("order", "random"),
        ],
        check=True, capture_output=True, text=True,
    )

    rmin, dmin = config["rmin"], config["dmin"]
    oracle_rows, oracle_checksum = oc.oracle_qa(data_dir, rmin, dmin)

    all_rows = []

    def run_and_check(method, buffer_pages):
        engine = run_query(run_query_bin, data_dir, method, buffer_pages, rmin, dmin)
        oracle_ok = (engine["tuples_out"] == oracle_rows and
                     engine["checksum"] == oracle_checksum)
        row = {
            "experiment": config["experiment"], "query": "QA", "plan_id": method,
            "plan_desc": method.upper(), "dataset_id": config["dataset_id"],
            "seed": config["seed"], "scale": config["scale"],
            "key_dist": config.get("key_dist", "uniform"),
            "zipf_theta": config.get("zipf_theta", 1.0), "corr": config.get("corr", 0.0),
            "order": config.get("order", "random"), "B": buffer_pages, "run": 1,
            "time_ms": engine["time_ms"], "page_reads": engine["page_reads"],
            "page_writes": engine["page_writes"], "reads_base": engine["reads_base"],
            "reads_index": engine["reads_index"], "reads_temp": engine["reads_temp"],
            "writes_temp": engine["writes_temp"], "buffer_hits": engine["buffer_hits"],
            "tuples_out": engine["tuples_out"], "checksum": f"0x{engine['checksum']:x}",
            "est_io": "", "est_out_rows": "",
            "ghj_overflow_partitions": engine["ghj_overflow_partitions"],
            "oracle_ok": oracle_ok,
        }
        all_rows.append(row)
        return row

    showcase_rows = [run_and_check(m, config["buffer_pages"]) for m in config["methods"]]
    sweep_method = config.get("sweep_method", config["methods"][0])
    sweep_buffers = config.get("buffer_sweep", [])
    sweep_rows = [run_and_check(sweep_method, b) for b in sweep_buffers]

    with open(results_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(all_rows)

    print(f"{'Plan':<8}{'Time (ms)':>12}{'Reads':>10}{'Writes':>10}{'Oracle OK':>12}")
    for r in showcase_rows:
        print(f"{r['plan_id']:<8}{r['time_ms']:>12.2f}{r['page_reads']:>10}"
              f"{r['page_writes']:>10}{str(r['oracle_ok']):>12}")

    if len(sweep_rows) >= 2:
        xs = [r["B"] for r in sweep_rows]
        times = [r["time_ms"] for r in sweep_rows]
        ios = [r["page_reads"] + r["page_writes"] for r in sweep_rows]
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 4))
        ax1.plot(xs, times, marker="o")
        ax1.set_xlabel("Buffer pages (B)")
        ax1.set_ylabel("Time (ms)")
        ax1.set_title(f"{sweep_method.upper()} time vs. B")
        ax2.plot(xs, ios, marker="o")
        ax2.set_xlabel("Buffer pages (B)")
        ax2.set_ylabel("Page I/O (reads + writes)")
        ax2.set_title(f"{sweep_method.upper()} I/O vs. B")
        fig.tight_layout()
        fig_path = os.path.join(figures_dir, f"{config['experiment']}_sweep.png")
        fig.savefig(fig_path)
        print(f"\nSweep graph written to {fig_path}")

    print(f"Results written to {results_path}")
    all_ok = all(r["oracle_ok"] for r in all_rows)
    print(f"All oracle checks passed: {all_ok}")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
