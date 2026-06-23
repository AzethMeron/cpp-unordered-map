#!/usr/bin/env python3
"""Plot the CSV produced by benchmarks/sweep.cpp.

Usage:
    python3 benchmarks/plot_sweep.py results.csv [output_dir]

Produces:
    sweep_size.png      ns/op vs element count, per operation (fum vs std)
    sweep_speedup.png   speedup (std/fum) vs element count, all operations
    sweep_density.png   ns/op vs achieved load factor (fum vs std)
"""
import csv
import sys
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

FUM_COLOR = "#1f77b4"
STD_COLOR = "#d62728"
OP_TITLES = {
    "insert": "insert (random keys)",
    "find_hit": "find — hit",
    "find_miss": "find — miss",
    "erase": "erase",
    "iterate": "iterate",
}


def load(path):
    # data[sweep][operation][container] = list of (x, ns_per_op)
    data = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    with open(path, newline="") as handle:
        for row in csv.DictReader(handle):
            data[row["sweep"]][row["operation"]][row["container"]].append(
                (float(row["x"]), float(row["ns_per_op"]))
            )
    for sweep in data.values():
        for op in sweep.values():
            for container in op:
                op[container].sort()
    return data


def plot_size(data, out_dir):
    ops = ["insert", "find_hit", "find_miss", "erase", "iterate"]
    fig, axes = plt.subplots(2, 3, figsize=(15, 9))
    axes = axes.flatten()
    for ax, op in zip(axes, ops):
        for container, color in (("fum", FUM_COLOR), ("std", STD_COLOR)):
            points = data["size"][op][container]
            xs = [p[0] for p in points]
            ys = [p[1] for p in points]
            ax.plot(xs, ys, marker="o", markersize=4, color=color,
                    label=container)
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_title(OP_TITLES[op])
        ax.set_xlabel("element count N")
        ax.set_ylabel("ns per operation")
        ax.grid(True, which="both", ls=":", alpha=0.4)
        ax.legend()
    # Use the spare 6th cell for a short explanatory note.
    axes[5].axis("off")
    axes[5].text(0.0, 0.5,
                 "Lower is faster.\n\n"
                 "fum::unordered_map vs std::unordered_map\n"
                 "uint64 -> uint64, random keys.\n\n"
                 "Both axes log-scaled.\n"
                 "The fum advantage widens with N as cache\n"
                 "misses begin to dominate.",
                 fontsize=12, va="center")
    fig.suptitle("Throughput sweep over element count", fontsize=15)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    path = f"{out_dir}/sweep_size.png"
    fig.savefig(path, dpi=110)
    plt.close(fig)
    return path


def plot_speedup(data, out_dir):
    ops = ["insert", "find_hit", "find_miss", "erase", "iterate"]
    fig, ax = plt.subplots(figsize=(10, 6.5))
    for op in ops:
        fum = dict(data["size"][op]["fum"])
        std = dict(data["size"][op]["std"])
        xs = sorted(set(fum) & set(std))
        speedup = [std[x] / fum[x] for x in xs]
        ax.plot(xs, speedup, marker="o", markersize=4, label=OP_TITLES[op])
    ax.axhline(1.0, color="black", ls="--", lw=1, alpha=0.7)
    ax.text(xs[0], 1.03, "above 1.0 = fum faster", fontsize=10, alpha=0.7)
    ax.set_xscale("log")
    ax.set_title("Speedup of fum::unordered_map over std::unordered_map")
    ax.set_xlabel("element count N")
    ax.set_ylabel("speedup  (std ns/op  /  fum ns/op)")
    ax.grid(True, which="both", ls=":", alpha=0.4)
    ax.legend()
    fig.tight_layout()
    path = f"{out_dir}/sweep_speedup.png"
    fig.savefig(path, dpi=110)
    plt.close(fig)
    return path


def plot_density(data, out_dir):
    fig, axes = plt.subplots(1, 2, figsize=(14, 5.5))
    for ax, op in zip(axes, ("find_hit", "find_miss")):
        for container, color in (("fum", FUM_COLOR), ("std", STD_COLOR)):
            points = data["density"][op][container]
            xs = [p[0] for p in points]
            ys = [p[1] for p in points]
            ax.plot(xs, ys, marker="o", markersize=5, color=color,
                    label=container)
        ax.set_title(f"{OP_TITLES[op]} vs table density")
        ax.set_xlabel("achieved load factor (elements / buckets)")
        ax.set_ylabel("ns per lookup")
        ax.grid(True, ls=":", alpha=0.4)
        ax.legend()
    fig.suptitle("Lookup cost as the table fills up (fixed table size, ~1M slots)",
                 fontsize=14)
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    path = f"{out_dir}/sweep_density.png"
    fig.savefig(path, dpi=110)
    plt.close(fig)
    return path


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    csv_path = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "."
    data = load(csv_path)
    for path in (plot_size(data, out_dir), plot_speedup(data, out_dir),
                 plot_density(data, out_dir)):
        print(f"wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
