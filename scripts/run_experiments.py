#!/usr/bin/env python3
import csv
import json
import os
import re
import statistics
import subprocess
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_plots as mp
import oracle_check as oc

CSV_FIELDS = [
    "experiment", "query", "plan_id", "plan_desc", "dataset_id", "seed", "scale",
    "key_dist", "zipf_theta", "corr", "order", "B", "run", "time_ms", "page_reads",
    "page_writes", "reads_base", "reads_index", "reads_temp", "writes_temp",
    "buffer_hits", "tuples_out", "checksum", "est_io", "est_out_rows",
    "ghj_overflow_partitions", "oracle_ok", "x_value",
]

DATASET_FIELDS = ["scale", "seed", "key_dist", "zipf_theta", "corr", "order", "cluster_by"]

QUERY_FLAGS = {
    "qa": ["method", "buffer_pages", "rmin", "dmin"],
    "qb": ["join_order", "method1", "method2", "pushdown", "buffer_pages", "rmin", "amax",
           "color", "d1", "d2"],
    "qc": ["join_order", "method1", "method2", "pushdown", "buffer_pages", "rmin", "amax",
           "color", "d1", "d2", "h"],
    "qd": ["join_order", "method1", "method2", "pushdown", "buffer_pages", "rmin", "amax",
           "color", "d1", "d2"],
}

def run_query_qa(run_query_bin, data_dir, method, buffer_pages, rmin, dmin):
    out = subprocess.run(
        [run_query_bin, "--data-dir", data_dir, "--method", method, "--buffer-pages",
         str(buffer_pages), "--rmin", str(rmin), "--dmin", str(dmin)],
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

def run_phase6(config):
    gen_data_bin = config.get("gen_data_bin", "build/src/gen_data")
    run_query_bin = config.get("run_query_bin", "build/src/run_query")
    data_dir = config.get("data_dir", f"data/{config['dataset_id']}")
    results_path = config.get("results_csv", f"results/{config['experiment']}.csv")
    figures_dir = config.get("figures_dir", "figures")

    os.makedirs(os.path.dirname(results_path) or ".", exist_ok=True)
    os.makedirs(figures_dir, exist_ok=True)

    subprocess.run(
        [gen_data_bin, "--out-dir", data_dir, "--seed", str(config["seed"]), "--scale",
         str(config["scale"]), "--key-dist", config.get("key_dist", "uniform"), "--zipf-theta",
         str(config.get("zipf_theta", 1.0)), "--corr", str(config.get("corr", 0.0)), "--order",
         config.get("order", "random")],
        check=True, capture_output=True, text=True,
    )

    rmin, dmin = config["rmin"], config["dmin"]
    oracle_rows, oracle_checksum = oc.oracle_qa(data_dir, rmin, dmin)

    all_rows = []

    def run_and_check(method, buffer_pages):
        engine = run_query_qa(run_query_bin, data_dir, method, buffer_pages, rmin, dmin)
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

def dataset_key(params):
    return tuple(params.get(f) for f in DATASET_FIELDS)

def gen_dataset(gen_data_bin, data_dir, params):
    args = [
        gen_data_bin, "--out-dir", data_dir,
        "--seed", str(params["seed"]), "--scale", str(params["scale"]),
        "--key-dist", params.get("key_dist", "uniform"),
        "--zipf-theta", str(params.get("zipf_theta", 1.0)),
        "--corr", str(params.get("corr", 0.0)),
        "--order", params.get("order", "random"),
    ]
    if "cluster_by" in params:
        args += ["--cluster-by", params["cluster_by"]]
    subprocess.run(args, check=True, capture_output=True, text=True)

def run_query_generic(run_query_bin, query, data_dir, params, flags):
    args = [run_query_bin, "--query", query, "--data-dir", data_dir, "--explain", "on"]
    for flag in flags:
        if flag in params:
            args += [f"--{flag.replace('_', '-')}", str(params[flag])]
    out = subprocess.run(args, capture_output=True, text=True, check=True).stdout

    def grab(pattern, cast=int, required=True):
        m = re.search(pattern, out)
        if m is None:
            if required:
                raise ValueError(f"run_query output missing '{pattern}':\n{out}")
            return None
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
        "order_ok": grab(r"OrderOk: (true|false)", lambda s: s == "true", required=False),
        "est_io": grab(r"Total estimated I/O: ([\d.]+)", float, required=False),
        "est_out_rows": grab(r"Est rows out: ([\d.]+)", float, required=False),
    }

def plan_label(params):
    if "label" in params:
        return params["label"]
    if "method1" in params:
        return f"{params['method1']}+{params['method2']}"
    return str(params.get("method", "?"))

def run_phase9(config):
    gen_data_bin = config.get("gen_data_bin", "build/src/gen_data")
    run_query_bin = config.get("run_query_bin", "build/src/run_query")
    data_root = config.get("data_root", f"data/{config['experiment']}")
    results_path = config.get("results_csv", f"results/{config['experiment']}.csv")
    figures_dir = config.get("figures_dir", "figures")
    query = config["query"]
    flags = QUERY_FLAGS[query]
    repeats = config.get("repeats", 5)

    os.makedirs(os.path.dirname(results_path) or ".", exist_ok=True)
    os.makedirs(figures_dir, exist_ok=True)

    dataset_defaults = config.get("dataset_defaults", {})
    query_defaults = config.get("query_defaults", {})
    default_methods = config.get("methods")

    dataset_cache = {}
    oracle_cache = {}
    all_rows = []
    x_field = config.get("x_field", "B")

    for point_idx, point in enumerate(config["points"]):
        base = {**dataset_defaults, **query_defaults, **point}
        key = dataset_key(base)
        if key not in dataset_cache:
            dataset_id = f"ds{len(dataset_cache) + 1}"
            data_dir = os.path.join(data_root, dataset_id)
            gen_dataset(gen_data_bin, data_dir, base)
            dataset_cache[key] = (data_dir, dataset_id)
        data_dir, dataset_id = dataset_cache[key]

        if data_dir not in oracle_cache:
            if query == "qa":
                oracle_cache[data_dir] = oc.oracle_qa(data_dir, base["rmin"], base["dmin"])
            elif query == "qc":
                oracle_cache[data_dir] = oc.oracle_qc(data_dir, base["rmin"], base["amax"],
                                                       base["color"], base["d1"], base["d2"],
                                                       base.get("h", 0))
            elif query == "qd":
                _, rows, checksum = oc.oracle_qd(data_dir, base["rmin"], base["amax"],
                                                  base["color"], base["d1"], base["d2"])
                oracle_cache[data_dir] = (rows, checksum)
            else:
                oracle_cache[data_dir] = oc.oracle_qb(data_dir, base["rmin"], base["amax"],
                                                       base["color"], base["d1"], base["d2"])
        oracle_rows, oracle_checksum = oracle_cache[data_dir]

        methods = point.get("methods", default_methods)
        variants = [{}] if methods is None else [
            ({"method": m} if query == "qa" else {"method1": m, "method2": m}) for m in methods
        ]

        for variant in variants:
            params = {**base, **variant}
            label = plan_label(params)

            timings = []
            last = None
            for run_idx in range(repeats + 1):
                last = run_query_generic(run_query_bin, query, data_dir, params, flags)
                if run_idx > 0:
                    timings.append(last["time_ms"])
            median_time = statistics.median(timings)

            oracle_ok = last["tuples_out"] == oracle_rows and last["checksum"] == oracle_checksum
            if query == "qd" and last["order_ok"] is False:
                oracle_ok = False

            row = {
                "experiment": config["experiment"], "query": query.upper(), "plan_id": label,
                "plan_desc": label, "dataset_id": dataset_id,
                "seed": base.get("seed"), "scale": base.get("scale"),
                "key_dist": base.get("key_dist", "uniform"),
                "zipf_theta": base.get("zipf_theta", 1.0), "corr": base.get("corr", 0.0),
                "order": base.get("order", "random"), "B": base.get("buffer_pages"),
                "run": point_idx,
                "time_ms": median_time, "page_reads": last["page_reads"],
                "page_writes": last["page_writes"], "reads_base": last["reads_base"],
                "reads_index": last["reads_index"], "reads_temp": last["reads_temp"],
                "writes_temp": last["writes_temp"], "buffer_hits": last["buffer_hits"],
                "tuples_out": last["tuples_out"], "checksum": f"0x{last['checksum']:x}",
                "est_io": last["est_io"], "est_out_rows": last["est_out_rows"],
                "ghj_overflow_partitions": last["ghj_overflow_partitions"],
                "oracle_ok": oracle_ok,
                "x_value": params.get(x_field, base.get(x_field)),
            }
            all_rows.append(row)
            print(f"[{'OK' if oracle_ok else 'MISMATCH'}] point={point_idx} plan={label} "
                  f"time={median_time:.2f}ms io={row['page_reads'] + row['page_writes']} "
                  f"est_io={row['est_io']}")

    with open(results_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(all_rows)
    print(f"\nResults written to {results_path}")

    mp.plot_experiment(config["experiment"], all_rows, config.get("x_label", x_field), figures_dir)

    all_ok = all(r["oracle_ok"] for r in all_rows)
    print(f"All oracle checks passed: {all_ok}")
    return 0 if all_ok else 1

def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <config.json>", file=sys.stderr)
        return 2
    with open(sys.argv[1]) as f:
        config = json.load(f)

    if "points" in config:
        return run_phase9(config)
    return run_phase6(config)

if __name__ == "__main__":
    sys.exit(main())
