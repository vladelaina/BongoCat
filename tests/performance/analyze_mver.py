#!/usr/bin/env python3
"""Summarize repeated, alternating Mver A/B batches (not event latencies)."""
import csv
import math
import statistics
import sys
from collections import defaultdict

samples = defaultdict(lambda: defaultdict(list))
pairs = defaultdict(dict)
for filename in sys.argv[1:]:
    with open(filename, newline="", encoding="utf-8") as source:
        for row in csv.DictReader(source):
            for clock in ("wall", "cpu"):
                elapsed = int(row[clock + "_ns"]) / int(row["iterations"]) / 1000
                samples[(row["scenario"], clock)][row["variant"]].append(elapsed)
                trial = (filename, row["trial"])
                pairs[(row["scenario"], clock, trial)][row["variant"]] = elapsed

print("scenario,clock,upstream_median_us,cached_median_us,median_change_pct,"
      "upstream_p95_batch_mean_us,cached_p95_batch_mean_us,"
      "paired_median_delta_us,paired_median_change_pct")
for (scenario, clock), variants in samples.items():
    before, after = variants["upstream"], variants["cached"]
    def p95(values):
        return sorted(values)[math.ceil(len(values) * .95) - 1]
    old, new = statistics.median(before), statistics.median(after)
    matching = [v for (s, c, _), v in pairs.items() if (s, c) == (scenario, clock)]
    delta = statistics.median(v["cached"] - v["upstream"] for v in matching)
    change = statistics.median((v["cached"] / v["upstream"] - 1) * 100
                               for v in matching)
    print(f"{scenario},{clock},{old:.6f},{new:.6f},{(new / old - 1) * 100:.3f},"
          f"{p95(before):.6f},{p95(after):.6f},{delta:.6f},{change:.3f}")
