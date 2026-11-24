#!/usr/bin/env python3
"""Turns one experiment's results CSV into a graph, a console table, and
the estimated-vs-measured comparisons SPEC.md section 11 asks every
experiment to emit: the measured/estimated I/O ratio, and whether the
estimator's top-ranked plan is the measured-fastest plan (only
meaningful for rows that share a "run" / sweep point, e.g. a
methods-comparison experiment).

Callable as a library (run_experiments.py imports plot_experiment
directly, with already-typed in-memory rows) or standalone:
    python3 scripts/make_plots.py results/e1.csv --x-label "Scale factor"
"""
import argparse
import csv
import os
import statistics
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

NUMERIC_FLOAT_FIELDS = ["time_ms", "est_io", "est_out_rows", "x_value", "scale", "zipf_theta",
                        "corr"]
NUMERIC_INT_FIELDS = ["page_reads", "page_writes", "run", "ghj_overflow_partitions", "tuples_out"]


def load_csv_rows(path):
    with open(path, newline="") as f:
        rows = [dict(r) for r in csv.DictReader(f)]
    for r in rows:
        for field in NUMERIC_FLOAT_FIELDS:
            if r.get(field) not in (None, ""):
                try:
                    r[field] = float(r[field])
                except ValueError:
                    pass
        for field in NUMERIC_INT_FIELDS:
            if r.get(field) not in (None, ""):
                try:
                    r[field] = int(float(r[field]))
                except ValueError:
                    pass
        r["oracle_ok"] = str(r.get("oracle_ok")).lower() == "true"
    return rows


def plot_experiment(experiment, rows, x_label, figures_dir):
    os.makedirs(figures_dir, exist_ok=True)
    by_plan = {}
    for r in rows:
        by_plan.setdefault(r["plan_id"], []).append(r)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.5))
    for plan, plan_rows in by_plan.items():
        plan_rows = sorted(plan_rows, key=lambda r: r["x_value"])
        xs = [r["x_value"] for r in plan_rows]
        times = [r["time_ms"] for r in plan_rows]
        ios = [r["page_reads"] + r["page_writes"] for r in plan_rows]
        ax1.plot(xs, times, marker="o", label=plan)
        ax2.plot(xs, ios, marker="o", label=plan)
    ax1.set_xlabel(x_label)
    ax1.set_ylabel("Time (ms, median of repeats)")
    ax1.set_title(f"{experiment}: time vs. {x_label}")
    ax1.legend(fontsize="small")
    ax2.set_xlabel(x_label)
    ax2.set_ylabel("Page I/O (reads + writes)")
    ax2.set_title(f"{experiment}: I/O vs. {x_label}")
    ax2.legend(fontsize="small")
    fig.tight_layout()
    fig_path = os.path.join(figures_dir, f"{experiment}.png")
    fig.savefig(fig_path)
    plt.close(fig)
    print(f"Graph written to {fig_path}")

    print(f"\n{'Plan':<20}{'x':>10}{'Time (ms)':>12}{'I/O':>10}{'Est I/O':>10}{'Oracle':>8}")
    for r in sorted(rows, key=lambda r: (r["plan_id"], r["x_value"])):
        io = r["page_reads"] + r["page_writes"]
        est = f"{r['est_io']:.0f}" if r.get("est_io") not in (None, "") else "-"
        print(f"{r['plan_id']:<20}{r['x_value']:>10}{r['time_ms']:>12.2f}{io:>10}{est:>10}"
              f"{str(r['oracle_ok']):>8}")

    # Estimated-vs-measured comparison (SPEC.md section 11's closing line).
    pairs = [(r["page_reads"] + r["page_writes"], r["est_io"]) for r in rows
             if r.get("est_io") not in (None, "", 0)]
    if pairs:
        ratio_values = [m / e for m, e in pairs]
        print(f"\nMeasured/estimated I/O ratio: min={min(ratio_values):.2f} "
              f"max={max(ratio_values):.2f} mean={statistics.mean(ratio_values):.2f}")

    # Is the estimator's top-ranked plan the measured-fastest plan?
    by_run = {}
    for r in rows:
        by_run.setdefault(r["run"], []).append(r)
    comparable = {run: rs for run, rs in by_run.items()
                  if len(rs) > 1 and all(r.get("est_io") not in (None, "") for r in rs)}
    if comparable:
        agree = 0
        for run, rs in comparable.items():
            est_best = min(rs, key=lambda r: r["est_io"])["plan_id"]
            measured_best = min(rs, key=lambda r: r["time_ms"])["plan_id"]
            if est_best == measured_best:
                agree += 1
        print(f"Estimator's top plan == measured fastest: {agree}/{len(comparable)} points")

    return fig_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", nargs="+", help="results CSV file(s), e.g. results/e1.csv")
    parser.add_argument("--x-label", default=None)
    parser.add_argument("--figures-dir", default="figures")
    args = parser.parse_args()

    ok = True
    for path in args.csv:
        rows = load_csv_rows(path)
        if not rows:
            print(f"{path}: no rows", file=sys.stderr)
            continue
        experiment = rows[0].get("experiment") or os.path.splitext(os.path.basename(path))[0]
        x_label = args.x_label or "x"
        print(f"=== {experiment} ({path}) ===")
        plot_experiment(experiment, rows, x_label, args.figures_dir)
        if not all(r["oracle_ok"] for r in rows):
            ok = False
        print()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
