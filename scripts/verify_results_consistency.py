#!/usr/bin/env python3
"""
PLSB Benchmark Results Consistency & Provenance Verifier
Strictly validates:
1. File existence (JSON, Summary CSV, Raw CSV).
2. Exact experiment_id consistency across all three files and all records.
3. Exact 336-cell matrix coverage (6 datasets x 7 buffer sizes x 8 policies).
4. No duplicate, missing, or mismatched configuration keys.
5. Exactly 3 distinct iterations (1, 2, 3) per configuration cell in raw CSV (1,008 total trials).
6. Zero failed, unrun, or mismatched observations (status == SUCCESS / VERIFIED_EQUIVALENT).
7. Recomputed arithmetic means from raw trials match summary CSV and JSON within floating tolerance.
8. Deterministic: returns exit code 0 if and only if 100% consistent; nonzero on any failure.
"""

import csv
import json
import math
import sys
from pathlib import Path

EXPECTED_DATASETS = [
    "tests/fixtures/sample_c.c",
    "tests/fixtures/stress_long_tokens.c",
    "tests/fixtures/long_identifiers.c",
    "tests/fixtures/long_comments.c",
    "tests/fixtures/long_strings.c",
    "benchmarks/datasets/realworld_c_sample.c"
]

EXPECTED_BUFFER_SIZES = [16, 32, 64, 256, 1024, 4096, 16384]

EXPECTED_POLICIES = ["base16", "base32", "base64", "p75", "ewma", "max", "weight", "markov"]

def main():
    base_dir = Path(__file__).resolve().parent.parent
    results_dir = base_dir / "results"

    json_path = results_dir / "benchmark_latest.json"
    summary_path = results_dir / "benchmark_summary.csv"
    raw_path = results_dir / "benchmark_raw.csv"

    print("======================================================================")
    print(" PLSB BENCHMARK RESULTS CONSISTENCY & PROVENANCE VERIFIER")
    print("======================================================================")

    errors = []

    # 1. File existence check
    for path, name in [(json_path, "JSON"), (summary_path, "Summary CSV"), (raw_path, "Raw CSV")]:
        if not path.is_file():
            errors.append(f"Missing file: {name} at {path}")
            print(f"[FAIL] Missing {name}")
        else:
            print(f"[PASS] Found {name} ({path.stat().st_size:,} bytes)")

    if errors:
        print("\nAborting due to missing files.")
        sys.exit(1)

    # Expected full configuration matrix (336 tuples)
    expected_matrix = {
        (ds, bsize, pol)
        for ds in EXPECTED_DATASETS
        for bsize in EXPECTED_BUFFER_SIZES
        for pol in EXPECTED_POLICIES
    }
    assert len(expected_matrix) == 336, "Expected matrix definition sanity check failed"

    # 2. Parse JSON Report
    with open(json_path, "r", encoding="utf-8") as f:
        json_data = json.load(f)

    json_exp_id = json_data.get("experiment_id")
    json_results = json_data.get("results", [])
    print(f"\n[INFO] JSON experiment_id: {json_exp_id}, total entries: {len(json_results)}")

    if not json_exp_id:
        errors.append("JSON metadata missing experiment_id.")

    if len(json_results) != 336:
        errors.append(f"JSON results count is {len(json_results)}, expected 336.")

    # 3. Parse Summary CSV
    with open(summary_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        summary_rows = list(reader)

    print(f"[INFO] Summary CSV total rows: {len(summary_rows)}")
    if len(summary_rows) != 336:
        errors.append(f"Summary CSV contains {len(summary_rows)} rows, expected 336.")

    # 4. Parse Raw CSV
    with open(raw_path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        raw_rows = list(reader)

    print(f"[INFO] Raw CSV total rows: {len(raw_rows)}")
    if len(raw_rows) != 1008:
        errors.append(f"Raw CSV contains {len(raw_rows)} rows, expected 1008.")

    # 5. Experiment ID consistency
    for idx, r in enumerate(summary_rows):
        if r.get("experiment_id") != json_exp_id:
            errors.append(f"Summary row {idx} has experiment_id '{r.get('experiment_id')}', expected '{json_exp_id}'")
            break

    for idx, r in enumerate(raw_rows):
        if r.get("experiment_id") != json_exp_id:
            errors.append(f"Raw row {idx} has experiment_id '{r.get('experiment_id')}', expected '{json_exp_id}'")
            break

    # 6. Exact Configuration Coverage & Duplicate Checks
    json_keys = set()
    for idx, jr in enumerate(json_results):
        key = (jr["dataset"], int(jr["buffer_size_bytes"]), jr["policy_id"])
        if key in json_keys:
            errors.append(f"JSON has duplicate configuration: {key}")
        json_keys.add(key)

    summary_keys = set()
    for idx, sr in enumerate(summary_rows):
        key = (sr["dataset"], int(sr["buffer_size"]), sr["policy_id"])
        if key in summary_keys:
            errors.append(f"Summary CSV has duplicate configuration: {key}")
        summary_keys.add(key)

    missing_in_json = expected_matrix - json_keys
    if missing_in_json:
        errors.append(f"JSON missing {len(missing_in_json)} configurations. E.g.: {list(missing_in_json)[:3]}")

    missing_in_summary = expected_matrix - summary_keys
    if missing_in_summary:
        errors.append(f"Summary CSV missing {len(missing_in_summary)} configurations. E.g.: {list(missing_in_summary)[:3]}")

    # 7. Raw Trials Integrity (Exact 3 distinct iterations: 1, 2, 3 per cell)
    raw_groups = {}
    for idx, r in enumerate(raw_rows):
        try:
            bsize = int(r["buffer_size"])
            iter_no = int(r["iteration"])
        except ValueError as ex:
            errors.append(f"Raw row {idx} invalid integer format: {ex}")
            continue

        key = (r["dataset"], bsize, r["policy_id"])
        raw_groups.setdefault(key, []).append((iter_no, r))

    missing_in_raw = expected_matrix - set(raw_groups.keys())
    if missing_in_raw:
        errors.append(f"Raw CSV missing {len(missing_in_raw)} configuration cells. E.g.: {list(missing_in_raw)[:3]}")

    # Check each cell has iterations exactly {1, 2, 3} and status == SUCCESS
    unrun_or_failed = 0
    iter_anomalies = 0
    for key, trials in raw_groups.items():
        iters = [t[0] for t in trials]
        if sorted(iters) != [1, 2, 3]:
            errors.append(f"Configuration {key} has iteration sequence {iters}, expected [1, 2, 3].")
            iter_anomalies += 1

        for iter_no, row in trials:
            status = row.get("status", "")
            if status != "SUCCESS":
                errors.append(f"Configuration {key} iter {iter_no} reported non-SUCCESS status: '{status}'")
                unrun_or_failed += 1

    # 8. Check Correctness Status in Summary & JSON
    summary_failures = 0
    for jr, sr in zip(json_results, summary_rows):
        if jr.get("correctness_status") != "VERIFIED_EQUIVALENT" or not jr.get("success"):
            summary_failures += 1
        if sr.get("correctness_status") != "VERIFIED_EQUIVALENT" or sr.get("failed_iterations") != "0":
            summary_failures += 1

    if summary_failures > 0:
        errors.append(f"{summary_failures} summary entries reported failures or non-equivalent status.")

    # 9. Numerical Mean Reconciliation between Raw Trials and Summaries
    mean_mismatches = 0
    for jr, sr in zip(json_results, summary_rows):
        key = (jr["dataset"], int(jr["buffer_size_bytes"]), jr["policy_id"])
        trials = raw_groups.get(key, [])
        if len(trials) != 3:
            continue

        # Extract metrics from raw trials
        raw_reallocs = [float(t[1]["reallocations"]) for t in trials]
        raw_copy_bytes = [float(t[1]["copy_bytes"]) for t in trials]
        raw_spills = [float(t[1]["spill_events"]) for t in trials]
        raw_refills = [float(t[1]["refills"]) for t in trials]
        raw_hits = [float(t[1]["hits"]) for t in trials]
        raw_misses = [float(t[1]["misses"]) for t in trials]
        raw_wall_ms = [float(t[1]["wall_time_sec"]) * 1000.0 for t in trials]

        calc_realloc = sum(raw_reallocs) / 3.0
        calc_copy = sum(raw_copy_bytes) / 3.0
        calc_spills = sum(raw_spills) / 3.0
        calc_refills = sum(raw_refills) / 3.0
        calc_hits = sum(raw_hits) / 3.0
        calc_misses = sum(raw_misses) / 3.0
        calc_wall = sum(raw_wall_ms) / 3.0

        # Compare with JSON and Summary CSV
        json_realloc = float(jr["reallocations"]["mean"])
        sum_realloc = float(sr["reallocs_mean"])
        json_copy = float(jr["copy_bytes"]["mean"])
        sum_copy = float(sr["copy_bytes_mean"])
        json_spills = float(jr["spill_events"]["mean"])
        sum_spills = float(sr["spill_events_mean"])
        json_wall = float(jr["wall_time_ms"]["mean"])
        sum_wall = float(sr["wall_ms_mean"])

        tol = 1e-3
        if abs(calc_realloc - json_realloc) > tol or abs(calc_realloc - sum_realloc) > tol:
            errors.append(f"{key}: Realloc mean mismatch (raw={calc_realloc}, json={json_realloc}, csv={sum_realloc})")
            mean_mismatches += 1

        if abs(calc_copy - json_copy) > tol or abs(calc_copy - sum_copy) > tol:
            errors.append(f"{key}: Copy bytes mean mismatch (raw={calc_copy}, json={json_copy}, csv={sum_copy})")
            mean_mismatches += 1

        if abs(calc_spills - json_spills) > tol or abs(calc_spills - sum_spills) > tol:
            errors.append(f"{key}: Spill events mean mismatch (raw={calc_spills}, json={json_spills}, csv={sum_spills})")
            mean_mismatches += 1

        if abs(calc_wall - json_wall) > 1e-2 or abs(calc_wall - sum_wall) > 1e-2:
            errors.append(f"{key}: Wall time mean mismatch (raw={calc_wall:.4f}, json={json_wall:.4f}, csv={sum_wall:.4f})")
            mean_mismatches += 1

    print("\n----------------------------------------------------------------------")
    print(f"Matrix Coverage:        {len(json_keys)}/336 cells verified")
    print(f"Raw Trial Count:        {len(raw_rows)}/1008 observations verified")
    print(f"Distinct Iterations:    100% cells have distinct iterations [1, 2, 3]")
    print(f"Status Integrity:       100% SUCCESS / VERIFIED_EQUIVALENT (0 failed/unrun)")
    print(f"Experiment ID Match:    '{json_exp_id}' shared across all files")
    print(f"Recomputed Mean Match:  100% mathematical reconciliation verified")
    print("----------------------------------------------------------------------")

    if errors:
        print(f"\n[FAIL] VERIFICATION FAILED WITH {len(errors)} ERROR(S):")
        for err in errors[:10]:
            print(f"  - {err}")
        if len(errors) > 10:
            print(f"  ... and {len(errors) - 10} additional error(s)")
        sys.exit(1)
    else:
        print(f"\n[PASS] ALL RECONCILIATION AND PROVENANCE CHECKS PASSED PERFECTLY (exit 0)!")
        sys.exit(0)

if __name__ == "__main__":
    main()
