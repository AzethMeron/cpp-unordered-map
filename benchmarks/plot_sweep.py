#!/usr/bin/env python3
"""Plot the CSV produced by benchmarks/sweep.cpp or benchmarks/compare_maps.cpp.

Usage:
    python3 benchmarks/plot_sweep.py results.csv [output_dir] [prefix]

Handles any number of containers present in the CSV (std, fum, boost, absl, ...).
Produces, in output_dir:
    <prefix>_size.png      ns/op vs element count, per operation
    <prefix>_speedup.png   speedup (std/other) vs element count, per container
    <prefix>_density.png   ns/op vs achieved load factor   (only if present)
"""
import csv
import itertools
import sys
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# Known containers get a stable colour; fum is drawn thicker to stand out.
STYLES = {
    "fum": dict(color="#1f77b4", lw=2.6, zorder=5),
    "std": dict(color="#d62728", lw=1.7, zorder=4),
    "boost": dict(color="#2ca02c", lw=1.7, zorder=4),
    "absl": dict(color="#9467bd", lw=1.7, zorder=4),
}
LABELS = {
    "fum": "fum::unordered_map",
    "std": "std::unordered_map",
    "boost": "boost::unordered_flat_map",
    "absl": "absl::flat_hash_map",
}
_PALETTE = itertools.cycle(["#ff7f0e", "#8c564b", "#e377c2", "#17becf"])
OP_TITLES = {
    "insert": "insert (random keys)",
    "find_hit": "find — hit",
    "find_miss": "find — miss",
    "erase": "erase",
    "iterate": "iterate",
}


def style_for(container):
    if container not in STYLES:
        STYLES[container] = dict(color=next(_PALETTE), lw=1.7, zorder=4)
    return STYLES[container]


def label_for(container):
    return LABELS.get(container, container)


def load(path):
    # data[sweep][operation][container] = sorted list of (x, ns_per_op)
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


def containers_in(section):
    seen = []
    for op in section.values():
        for container in op:
            if container not in seen:
                seen.append(container)
    # Stable, readable order.
    order = ["std", "fum", "boost", "absl"]
    return sorted(seen, key=lambda c: (order.index(c) if c in order else 99, c))


def plot_size(data, out_dir, prefix):
    ops = ["insert", "find_hit", "find_miss", "erase", "iterate"]
    containers = containers_in(data["size"])
    fig, axes = plt.subplots(2, 3, figsize=(15, 9))
    axes = axes.flatten()
    for ax, op in zip(axes, ops):
        for container in containers:
            points = data["size"][op][container]
            if not points:
                continue
            xs = [p[0] for p in points]
            ys = [p[1] for p in points]
            ax.plot(xs, ys, marker="o", markersize=3.5, label=label_for(container),
                    **style_for(container))
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_title(OP_TITLES.get(op, op))
        ax.set_xlabel("element count N")
        ax.set_ylabel("ns per operation")
        ax.grid(True, which="both", ls=":", alpha=0.4)
        ax.legend(fontsize=8)
    axes[5].axis("off")
    axes[5].text(0.0, 0.5,
                 "Lower is faster.  uint64 -> uint64, random keys.\n"
                 "Both axes log-scaled.\n\n"
                 "boost/absl are flat maps: faster but they MOVE\n"
                 "elements on rehash, so they are not drop-in\n"
                 "replacements. fum keeps pointer/reference\n"
                 "stability and stays competitive.",
                 fontsize=11, va="center")
    fig.suptitle("Throughput vs element count", fontsize=15)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    path = f"{out_dir}/{prefix}_size.png"
    fig.savefig(path, dpi=110)
    plt.close(fig)
    return path


def plot_speedup(data, out_dir, prefix):
    ops = ["insert", "find_hit", "find_miss", "erase", "iterate"]
    others = [c for c in containers_in(data["size"]) if c != "std"]
    fig, axes = plt.subplots(1, len(others), figsize=(6.5 * len(others), 5.5),
                             squeeze=False)
    for ax, container in zip(axes[0], others):
        for op in ops:
            base = dict(data["size"][op]["std"])
            other = dict(data["size"][op][container])
            xs = sorted(set(base) & set(other))
            if not xs:
                continue
            ax.plot(xs, [base[x] / other[x] for x in xs], marker="o",
                    markersize=3.5, label=OP_TITLES.get(op, op))
        ax.axhline(1.0, color="black", ls="--", lw=1, alpha=0.7)
        ax.set_xscale("log")
        ax.set_title(f"{label_for(container)} speedup over std")
        ax.set_xlabel("element count N")
        ax.set_ylabel(f"speedup  (std ns/op / {container} ns/op)")
        ax.grid(True, which="both", ls=":", alpha=0.4)
        ax.legend(fontsize=8)
    fig.suptitle("Speedup over std::unordered_map  (above 1.0 = faster than std)",
                 fontsize=14)
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    path = f"{out_dir}/{prefix}_speedup.png"
    fig.savefig(path, dpi=110)
    plt.close(fig)
    return path


def plot_density(data, out_dir, prefix):
    if not data.get("density"):
        return None
    containers = containers_in(data["density"])
    fig, axes = plt.subplots(1, 2, figsize=(14, 5.5))
    for ax, op in zip(axes, ("find_hit", "find_miss")):
        for container in containers:
            points = data["density"][op][container]
            if not points:
                continue
            xs = [p[0] for p in points]
            ys = [p[1] for p in points]
            ax.plot(xs, ys, marker="o", markersize=5, label=label_for(container),
                    **style_for(container))
        ax.axvspan(0.4, 0.8, color="green", alpha=0.08, zorder=0,
                   label="fum default operating band")
        ax.axvspan(0.8, 0.97, color="red", alpha=0.06, zorder=0,
                   label="only if max_load_factor raised")
        ax.axvline(0.8, color="black", ls="--", lw=1.1, alpha=0.7, zorder=2)
        top = ax.get_ylim()[1]
        ax.text(0.8, top * 0.97, "  default rehash\n  threshold (0.8)",
                fontsize=9, va="top", ha="left", alpha=0.8)
        ax.set_title(f"{OP_TITLES.get(op, op)} vs table density")
        ax.set_xlabel("achieved load factor (elements / buckets)")
        ax.set_ylabel("ns per lookup")
        ax.grid(True, ls=":", alpha=0.4)
        ax.legend(loc="upper left", fontsize=9)
    fig.suptitle(
        "Lookup cost as the table fills up (fixed ~1M-slot table)\n"
        "By default fum rehashes at load factor 0.8, so it never operates to "
        "the right of the dashed line",
        fontsize=13)
    fig.tight_layout(rect=(0, 0, 1, 0.93))
    path = f"{out_dir}/{prefix}_density.png"
    fig.savefig(path, dpi=110)
    plt.close(fig)
    return path


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    csv_path = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "."
    prefix = sys.argv[3] if len(sys.argv) > 3 else "sweep"
    data = load(csv_path)
    for path in (plot_size(data, out_dir, prefix),
                 plot_speedup(data, out_dir, prefix),
                 plot_density(data, out_dir, prefix)):
        if path:
            print(f"wrote {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
