#!/usr/bin/env python3
"""Inspect score contributions and remove one component at a time from a trace.

This is a counterfactual replay: candidates, pitch extraction and the fitted
artist-held-out profiles stay fixed. No weights are fitted. Top-1 bounds
expose score ties rather than guessing how a changed C++ sort would break them.
"""

import argparse
import collections
import csv
import json
import math
from pathlib import Path
import statistics
import sys


COMPONENTS = ("tonic_prior", "rare_prior", "held_fit", "missing_notes",
              "foreign_notes", "direction", "learned_profile", "phrases")
TIE_TOLERANCE = 1e-12


def read_report(path):
    with Path(path).open(encoding="utf-8") as stream:
        objects = [json.loads(line) for line in stream if line.strip()]
    if not objects or objects[0].get("type") != "settings" or objects[0].get("version", 0) < 2:
        raise ValueError("Expected a version 2 joint report with score components")
    if objects[-1].get("type") != "complete":
        raise ValueError("Joint report is incomplete")
    records = [r for r in objects if r.get("type") == "recording"]
    if len(records) != objects[-1]["recordings"] or len({r["file"] for r in records}) != len(records):
        raise ValueError("Joint report recording inventory is inconsistent")
    for record in records:
        for group in (record["hypotheses"], record["supplied_sa_matches"]):
            for match in group:
                values = [match["components"][name] for name in COMPONENTS]
                if not all(isinstance(x, (int, float)) and math.isfinite(x) for x in values + [match["score"]]):
                    raise ValueError(f'Nonfinite score in {record["file"]}')
                if not math.isclose(sum(values), match["score"], rel_tol=0, abs_tol=1e-9):
                    raise ValueError(f'Score components do not sum to the total in {record["file"]}')
    return objects[0], records


def rank_bounds(matches, truth, removed=None):
    """Maximize over all Sas again before comparing raags."""
    best = {}
    for match in matches:
        score = match["score"] - (match["components"][removed] if removed else 0)
        best[match["raag"]] = max(best.get(match["raag"], -math.inf), score)
    if truth not in best:
        return {"unique_top1": False, "possible_top1": False, "possible_top3": False}
    value = best[truth]
    higher = sum(score > value + TIE_TOLERANCE for score in best.values())
    ties = sum(abs(score - value) <= TIE_TOLERANCE for score in best.values())
    return {"unique_top1": higher == 0 and ties == 1,
            "possible_top1": higher == 0, "possible_top3": higher < 3}


def summarize_mode(records, group):
    eligible = [r for r in records if r[group]]
    results = {}
    winner_key = "winner" if group == "hypotheses" else "supplied_sa_winner"
    actual = {r["file"]: r[winner_key]["raag"] == r["truth_raag"] for r in eligible}
    for removed in (None,) + COMPONENTS:
        counts = collections.Counter()
        for record in eligible:
            bounds = rank_bounds(record[group], record["truth_raag"], removed)
            counts.update({key: int(value) for key, value in bounds.items()})
            counts["definite_gains"] += not actual[record["file"]] and bounds["unique_top1"]
            counts["definite_losses"] += actual[record["file"]] and not bounds["possible_top1"]
        results[removed or "baseline"] = {"recordings": len(eligible), **dict(counts)}
    return {"actual_baseline_top1": sum(actual.values()), "variants": results}


def analyze(records, metadata=None):
    metadata = metadata or {}
    contributions = []
    for record in records:
        pair = record["reference_pair"]
        if record["winner_raag_correct"] or pair is None:
            continue
        winner = record["winner"]
        deltas = {name: winner["components"][name] - pair["components"][name] for name in COMPONENTS}
        largest = max(deltas, key=deltas.get)
        if deltas[largest] <= TIE_TOLERANCE:
            largest = "tie"
        source = metadata.get(Path(record["file"]).name, {})
        contributions.append({"file": record["file"], "title": source.get("title", ""),
                              "artist": source.get("artist", ""), "truth_raag": record["truth_raag"],
                              "winner_raag": winner["raag"], "winner_sa_hz": winner["sa_hz"],
                              "reference_pair_sa_hz": pair["sa_hz"],
                              "score_gap": winner["score"] - pair["score"],
                              "largest_advantage": largest, **deltas})
    summary = {"recordings": len(records), "automatic": summarize_mode(records, "hypotheses"),
               "supplied_sa": summarize_mode(records, "supplied_sa_matches"),
               "errors_with_reference_pair": len(contributions),
               "largest_advantage_counts": dict(collections.Counter(r["largest_advantage"] for r in contributions)),
               "mean_winner_minus_reference": {name: statistics.mean(r[name] for r in contributions)
                                               for name in COMPONENTS} if contributions else {}}
    return summary, contributions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--manifest", type=Path, help="Optional Saraga manifest for titles and artist names")
    parser.add_argument("--output", type=Path, help="New output directory for summary.json and contributions.csv")
    args = parser.parse_args()
    try:
        settings, records = read_report(args.report)
        metadata = {}
        if args.manifest:
            metadata = {r["file"]: r for r in json.loads(args.manifest.read_text(encoding="utf-8"))["records"]}
        summary, contributions = analyze(records, metadata)
        summary["settings"] = settings
        summary["source_report"] = str(args.report)
        if args.output:
            args.output.mkdir(parents=True, exist_ok=False)
            (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
            with (args.output / "contributions.csv").open("w", encoding="utf-8", newline="") as stream:
                columns = ["file", "title", "artist", "truth_raag", "winner_raag", "winner_sa_hz",
                           "reference_pair_sa_hz", "score_gap", "largest_advantage", *COMPONENTS]
                writer = csv.DictWriter(stream, fieldnames=columns)
                writer.writeheader()
                writer.writerows(contributions)
        print("Top-1 bounds: uniquely correct .. correct under some tie ordering")
        print(f'{"Removed component":22} {"Automatic":16} {"Supplied Sa":16}')
        for name in ("baseline",) + COMPONENTS:
            values = []
            for mode in ("automatic", "supplied_sa"):
                variant = summary[mode]["variants"][name]
                values.append(f'{variant.get("unique_top1", 0)}..{variant.get("possible_top1", 0)}/{variant["recordings"]}')
            print(f'{name:22} {values[0]:16} {values[1]:16}')
        print("Largest wrong-pair score advantages:", summary["largest_advantage_counts"])
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(f"Score analysis failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
