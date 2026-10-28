#!/usr/bin/env python3
"""
Comprehensive End-to-End Test Suite for PLSB Dashboard Server.
Tests both standard happy paths and error cases:
1. Standard GET endpoints (/, /app.js, /api/datasets, /api/results/latest, /api/results, /api/results/file)
2. Interactive execution via POST /api/run
3. Invalid request parameters (negative/non-numeric buffer size, negative alpha) -> HTTP 400
4. Nonexistent dataset requests -> HTTP 404
5. Nonexistent result files -> HTTP 404
6. Missing query parameters -> HTTP 400
7. Invalid benchmark options on POST /api/benchmark (out of range iterations, invalid dataset) -> HTTP 400
8. Error response integrity: failed runs return success=False, returncode, and descriptive error.
"""

import json
import socket
import subprocess
import sys
import time
import urllib.request
import urllib.error

def get_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

def make_request(url, method="GET", data=None, timeout=30):
    headers = {"Content-Type": "application/json"} if data else {}
    body = json.dumps(data).encode("utf-8") if data else None
    req = urllib.request.Request(url, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8")
    except urllib.error.HTTPError as ex:
        return ex.code, ex.read().decode("utf-8")

def main():
    port = get_free_port()
    print(f"[TEST] Starting dashboard server on port {port}...")
    proc = subprocess.Popen([sys.executable, "dashboard/server.py", str(port)])
    time.sleep(1.5)

    base_url = f"http://127.0.0.1:{port}"
    passed = 0
    failed = 0

    def assert_test(name, condition, detail=""):
        nonlocal passed, failed
        if condition:
            print(f"  -> PASS: {name}")
            passed += 1
        else:
            print(f"  -> FAIL: {name} ({detail})")
            failed += 1

    try:
        print("\n=== 1. Standard Endpoints (Happy Paths) ===")

        # 1. GET / (index.html)
        status, content = make_request(f"{base_url}/")
        assert_test(
            "GET / (index.html)",
            status == 200
            and "Predictive Lexeme Spill Buffering" in content
            and "PLSB — Combined" in content
            and "PLSB Weighted" not in content
            and "kpiAccuracy" in content
        )

        # 2. GET /app.js
        status, content = make_request(f"{base_url}/app.js")
        assert_test("GET /app.js", status == 200 and "renderReallocsChart" in content)

        # 3. GET /api/datasets
        status, content = make_request(f"{base_url}/api/datasets")
        data = json.loads(content)
        assert_test("GET /api/datasets", status == 200 and len(data) == 6)

        # 4. GET /api/results/latest
        status, content = make_request(f"{base_url}/api/results/latest")
        data = json.loads(content)
        assert_test("GET /api/results/latest", status == 200 and data.get("summary_count") == 336)

        # 5. GET /api/results
        status, content = make_request(f"{base_url}/api/results")
        data = json.loads(content)
        assert_test("GET /api/results", status == 200 and any(f["name"] == "benchmark_summary.csv" for f in data))

        # 6. GET /api/results/file?name=benchmark_summary.csv
        status, content = make_request(f"{base_url}/api/results/file?name=benchmark_summary.csv")
        data = json.loads(content)
        assert_test("GET /api/results/file (existing)", status == 200 and "experiment_id" in data.get("content", ""))

        # 7. POST /api/run (valid run)
        status, content = make_request(f"{base_url}/api/run", method="POST", data={
            "dataset": "tests/fixtures/sample_c.c",
            "buffer_size": 32,
            "baseline_cap": 16,
            "rule": "max",
            "alpha": 0.20,
            "mode": "compare"
        })
        data = json.loads(content)
        assert_test("POST /api/run (valid run)", status == 200 and data.get("success") is True and "baseline" in data.get("report", {}))

        print("\n=== 2. Focused Negative & Error Cases ===")

        # 8. POST /api/run with invalid buffer_size (-10)
        status, content = make_request(f"{base_url}/api/run", method="POST", data={
            "dataset": "tests/fixtures/sample_c.c",
            "buffer_size": -10,
            "baseline_cap": 16
        })
        data = json.loads(content)
        assert_test("POST /api/run negative buffer_size -> 400", status == 400 and "error" in data)

        # 9. POST /api/run with non-numeric buffer_size
        status, content = make_request(f"{base_url}/api/run", method="POST", data={
            "dataset": "tests/fixtures/sample_c.c",
            "buffer_size": "not_a_number"
        })
        data = json.loads(content)
        assert_test("POST /api/run non-numeric buffer_size -> 400", status == 400 and "error" in data)

        # 10. POST /api/run with negative alpha (-0.5)
        status, content = make_request(f"{base_url}/api/run", method="POST", data={
            "dataset": "tests/fixtures/sample_c.c",
            "alpha": -0.5
        })
        data = json.loads(content)
        assert_test("POST /api/run negative alpha -> 400", status == 400 and "error" in data)

        # 11. POST /api/run with nonexistent dataset
        status, content = make_request(f"{base_url}/api/run", method="POST", data={
            "dataset": "tests/fixtures/this_dataset_does_not_exist.c",
            "buffer_size": 32
        })
        data = json.loads(content)
        assert_test("POST /api/run nonexistent dataset -> 404", status == 404 and "error" in data)

        # 12. GET /api/dataset with nonexistent path
        status, content = make_request(f"{base_url}/api/dataset?path=tests/fixtures/does_not_exist.c")
        assert_test("GET /api/dataset nonexistent path -> 404", status == 404)

        # 13. GET /api/dataset without path parameter
        status, content = make_request(f"{base_url}/api/dataset")
        assert_test("GET /api/dataset missing path query -> 400", status == 400)

        # 14. GET /api/results/file with nonexistent file
        status, content = make_request(f"{base_url}/api/results/file?name=nonexistent_results_file.csv")
        assert_test("GET /api/results/file nonexistent file -> 404", status == 404)

        # 15. GET /api/results/file without name parameter
        status, content = make_request(f"{base_url}/api/results/file")
        assert_test("GET /api/results/file missing name query -> 400", status == 400)

        # 16. POST /api/benchmark with invalid iterations (0 or > 100)
        status, content = make_request(f"{base_url}/api/benchmark", method="POST", data={
            "dataset": "tests/fixtures/sample_c.c",
            "iterations": 500
        })
        data = json.loads(content)
        assert_test("POST /api/benchmark out-of-range iterations -> 400", status == 400 and data.get("success") is False)

        # 17. POST /api/benchmark with nonexistent dataset
        status, content = make_request(f"{base_url}/api/benchmark", method="POST", data={
            "dataset": "tests/fixtures/missing_corpus.c"
        })
        data = json.loads(content)
        assert_test("POST /api/benchmark nonexistent dataset -> 400", status == 400 and data.get("success") is False)

        print("\n=== 3. Analyzer Display, Equivalence, and Comparison ===")

        # 18. Analyzer source with preprocessor grouping
        status, content = make_request(f"{base_url}/api/run", method="POST", data={
            "source": "#include <stdio.h>\nint x;\n",
            "buffer_size": 32,
            "baseline_cap": 16,
            "rule": "combined",
            "mode": "compare"
        }, timeout=30)
        data = json.loads(content)
        display = data.get("tokens_display") or []
        pre = [t for t in display if str(t.get("type", "")).upper() == "PREPROCESSOR"]
        native = data.get("tokens_native") or data.get("tokens_plsb") or []
        assert_test(
            "POST /api/run groups #include as PREPROCESSOR display token",
            status == 200 and data.get("success") is True and len(pre) == 1
            and pre[0].get("lexeme", "").startswith("#include")
        )
        assert_test(
            "POST /api/run keeps native OP_HASH stream for equivalence",
            any(str(t.get("type", "")).upper() in ("OP_HASH", "TOKEN_OP_HASH") for t in native)
            and "token_equivalence_pct" in data
            and "Accuracy" not in json.dumps(data.get("metric_notes", {}))
        )

        # 19. Comparison on a registered dataset (one combined PLSB + three baselines)
        status, content = make_request(f"{base_url}/api/compare", method="POST", data={
            "dataset": "tests/fixtures/sample_c.c",
            "buffer_size": 32
        }, timeout=90)
        data = json.loads(content)
        runs = data.get("runs") or []
        ids = [r.get("config_id") for r in runs]
        labels = " ".join(str(r.get("display_label", "")) for r in runs)
        competing = any(x in labels.lower() for x in ["p75", "ewma", "weighted", "markov"]) and "combined" not in "".join(ids)
        assert_test(
            "POST /api/compare returns 16/32/64 baselines + combined PLSB",
            status == 200 and data.get("success") is True
            and set(ids) == {"baseline_16", "baseline_32", "baseline_64", "combined"}
            and "PLSB — Combined" in labels
            and not competing
        )
        plsb = next((r for r in runs if r.get("config_id") == "combined"), {})
        assert_test(
            "POST /api/compare includes measured metrics and N/A-safe improvement fields",
            "spill_reallocations" in plsb and "growth_copy_bytes" in plsb
            and "token_equivalence_pct" in plsb
            and "realloc_improvement_pct" in plsb
            and data.get("run_id")
        )

        # 20. Comparison rejects unregistered dataset
        status, content = make_request(f"{base_url}/api/compare", method="POST", data={
            "dataset": "tests/fixtures/this_dataset_does_not_exist.c",
            "buffer_size": 32
        })
        data = json.loads(content)
        assert_test("POST /api/compare nonexistent dataset -> 404", status == 404 and "error" in data)

        # 21. Comparison rejects empty analyzer source
        status, content = make_request(f"{base_url}/api/compare", method="POST", data={
            "source": "   ",
            "buffer_size": 32
        })
        data = json.loads(content)
        assert_test("POST /api/compare empty source -> 400", status == 400 and "error" in data)

        print(f"\n==================================================")
        print(f"Results: {passed} passed, {failed} failed (Total: {passed + failed})")
        print(f"==================================================")
    except Exception as e:
        print(f"\n[ERROR] Test harness exception: {e}")
        failed += 1
    finally:
        print("[TEST] Shutting down test server...")
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()

    sys.exit(failed)

if __name__ == "__main__":
    main()
