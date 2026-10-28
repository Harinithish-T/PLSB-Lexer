#!/usr/bin/env python3
"""
PLSB Lexer Presentation Dashboard - HTTP Server
Standard library only: http.server, json, subprocess, urllib.parse, os, sys
"""

import http.server
import json
import os
import subprocess
import sys
import time
import urllib.parse
from pathlib import Path

from token_display import group_preprocessor_display_tokens, token_streams_equivalent

BASE_DIR = Path(__file__).resolve().parent.parent
DASHBOARD_DIR = Path(__file__).resolve().parent
RESULTS_DIR = BASE_DIR / "results"
BIN_DIR = BASE_DIR / "bin"

REGISTERED_DATASETS = [
    {
        "id": "stress_long_tokens",
        "name": "Stress Long Tokens",
        "path": "tests/fixtures/stress_long_tokens.c",
        "category": "Boundary Stress",
        "description": "Extended block comments (>400B), long strings (>300B), long identifiers (>100B). Triggers heavy spill buffering."
    },
    {
        "id": "sample_c",
        "name": "Sample C Program",
        "path": "tests/fixtures/sample_c.c",
        "category": "Standard C",
        "description": "Standard balanced mix of keywords, identifiers, math operators, loops, and small strings."
    },
    {
        "id": "long_identifiers",
        "name": "Long Identifiers",
        "path": "tests/fixtures/long_identifiers.c",
        "category": "Enterprise / Codegen",
        "description": "Dense symbols of 32–96 characters typical of code generators, enterprise domain models, and AST nodes."
    },
    {
        "id": "long_comments",
        "name": "Long Comments",
        "path": "tests/fixtures/long_comments.c",
        "category": "Documentation Heavy",
        "description": "Multi-paragraph licenses and architectural commentary spanning multiple consecutive buffer boundaries."
    },
    {
        "id": "long_strings",
        "name": "Long Strings",
        "path": "tests/fixtures/long_strings.c",
        "category": "Embedded Literals",
        "description": "Embedded JSON schemas and SQL table definitions exceeding 400–600 bytes with escape sequences."
    },
    {
        "id": "realworld_c_sample",
        "name": "Real-World Systems C",
        "path": "benchmarks/datasets/realworld_c_sample.c",
        "category": "Systems Software",
        "description": "Virtual machine bytecode interpreter with opcodes, stack operations, switch-cases, and bitmasks."
    }
]

ALLOWED_RULES = {
    "p75", "ewma", "max", "weighted", "markov", "baseline", "combined", "plsb"
}

DEFAULT_COMPARE_POLICIES = ["baseline_16", "baseline_32", "baseline_64", "combined"]

POLICY_LABELS = {
    "baseline_16": ("Fixed Baseline 16B", "baseline"),
    "baseline_32": ("Fixed Baseline 32B", "baseline"),
    "baseline_64": ("Fixed Baseline 64B", "baseline"),
    "combined": ("PLSB — Combined", "plsb"),
    "plsb": ("PLSB — Combined", "plsb"),
}


def path_is_under(base, candidate):
    try:
        candidate.resolve().relative_to(base.resolve())
        return True
    except (ValueError, OSError):
        return False


def registered_dataset_paths():
    return {d["path"] for d in REGISTERED_DATASETS}


def resolve_dataset_file(dataset_path):
    if not dataset_path or not isinstance(dataset_path, str):
        return None, "Missing dataset path"
    normalized = dataset_path.replace("\\", "/").lstrip("/")
    if normalized.startswith("../") or "/../" in normalized:
        return None, "Dataset path is not allowed"
    full_path = (BASE_DIR / normalized).resolve()
    if not path_is_under(BASE_DIR, full_path) or not full_path.is_file():
        return None, f"Dataset file not found or invalid: {dataset_path}"
    allowed = registered_dataset_paths() | {"tests/fixtures/custom_temp.c"}
    if normalized not in allowed:
        return None, f"Dataset is not in the supported registry: {dataset_path}"
    return full_path, None


def improvement_pct(baseline_value, current_value):
    """Lower-is-better improvement. None means N/A (zero baseline)."""
    try:
        baseline_value = float(baseline_value)
        current_value = float(current_value)
    except (TypeError, ValueError):
        return None
    if baseline_value == 0:
        return None
    return round(((baseline_value - current_value) / baseline_value) * 100.0, 2)


def higher_is_better_pct(baseline_value, current_value):
    try:
        baseline_value = float(baseline_value)
        current_value = float(current_value)
    except (TypeError, ValueError):
        return None
    if baseline_value == 0:
        return None
    return round(((current_value - baseline_value) / baseline_value) * 100.0, 2)


def find_executable(name):
    # Try Windows .exe first, then Linux binary
    candidates = [
        BIN_DIR / f"{name}.exe",
        BIN_DIR / name,
    ]
    for c in candidates:
        if c.exists() and os.access(c, os.X_OK):
            return str(c)
    # Check fallback without x_ok for windows
    for c in candidates:
        if c.exists():
            return str(c)
    return None

class PLSBDashboardHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(DASHBOARD_DIR), **kwargs)

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)

        if path == "/api/datasets":
            self.send_json(self.get_datasets_list())
            return
        elif path == "/api/dataset":
            file_path = query.get("path", [None])[0]
            if not file_path:
                self.send_error(400, "Missing 'path' query parameter")
                return
            full_path, err = resolve_dataset_file(file_path)
            if err:
                status = 404 if "not found" in err.lower() or "invalid" in err.lower() or "not in the supported" in err else 400
                self.send_json({"error": err}, status=status)
                return
            try:
                with open(full_path, "r", encoding="utf-8", errors="replace") as f:
                    content = f.read()
                self.send_json({
                    "path": file_path,
                    "size_bytes": os.path.getsize(full_path),
                    "content": content
                })
            except Exception as e:
                self.send_error(500, str(e))
            return
        elif path == "/api/results":
            self.send_json(self.get_results_files())
            return
        elif path == "/api/results/latest":
            latest_path = RESULTS_DIR / "benchmark_latest.json"
            if not latest_path.exists():
                self.send_json({"error": "No benchmark results yet. Run a benchmark first."})
                return
            try:
                with open(latest_path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                self.send_json(data)
            except Exception as e:
                self.send_error(500, str(e))
            return
        elif path == "/api/results/file":
            filename = query.get("name", [None])[0]
            if not filename:
                self.send_error(400, "Missing 'name' query parameter")
                return
            file_path = (RESULTS_DIR / filename).resolve()
            if not path_is_under(RESULTS_DIR, file_path) or not file_path.exists():
                self.send_error(404, "Result file not found")
                return
            try:
                with open(file_path, "r", encoding="utf-8", errors="replace") as f:
                    if filename.endswith(".json"):
                        self.send_json(json.load(f))
                    else:
                        content = f.read()
                        self.send_json({"filename": filename, "content": content})
            except Exception as e:
                self.send_error(500, str(e))
            return

        # Serve static files from dashboard/
        super().do_GET()

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        length = int(self.headers.get("Content-Length", 0))
        post_data = self.rfile.read(length) if length > 0 else b"{}"

        try:
            body = json.loads(post_data.decode("utf-8")) if post_data else {}
        except Exception:
            self.send_error(400, "Invalid JSON payload")
            return

        if path == "/api/run" or path == "/api/analyze":
            self.handle_run_single(body)
        elif path == "/api/compare":
            self.handle_compare(body)
        elif path == "/api/benchmark":
            self.handle_run_benchmark(body)
        elif path == "/api/custom-dataset":
            self.handle_custom_dataset(body)
        else:
            self.send_error(404, "Endpoint not found")

    def send_json(self, data, status=200):
        payload = json.dumps(data, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def get_datasets_list(self):
        result = []
        for d in REGISTERED_DATASETS:
            full_path = BASE_DIR / d["path"]
            exists = full_path.exists()
            size = os.path.getsize(full_path) if exists else 0
            result.append({
                **d,
                "exists": exists,
                "size_bytes": size
            })
        return result

    def get_results_files(self):
        RESULTS_DIR.mkdir(exist_ok=True)
        files = []
        for p in RESULTS_DIR.iterdir():
            if p.is_file():
                files.append({
                    "name": p.name,
                    "size_bytes": p.stat().st_size,
                    "mtime": p.stat().st_mtime,
                    "type": "json" if p.suffix == ".json" else ("csv" if p.suffix == ".csv" else "other")
                })
        files.sort(key=lambda x: x["mtime"], reverse=True)
        return files

    def handle_custom_dataset(self, body):
        content = body.get("content", "")
        if not content.strip():
            self.send_json({"error": "Empty content"}, status=400)
            return
        dest = BASE_DIR / "tests" / "fixtures" / "custom_temp.c"
        with open(dest, "w", encoding="utf-8") as f:
            f.write(content)
        self.send_json({
            "success": True,
            "path": "tests/fixtures/custom_temp.c",
            "size_bytes": len(content)
        })

    def handle_run_single(self, body):
        exe = find_executable("plsb_lexer")
        if not exe:
            self.send_json({"error": "plsb_lexer executable not found. Please build the project first."}, status=500)
            return

        source_text = body.get("source")
        if source_text is not None:
            dest = BASE_DIR / "tests" / "fixtures" / "custom_temp.c"
            with open(dest, "w", encoding="utf-8") as f:
                f.write(source_text)
            full_dataset_path = dest.resolve()
            full_dataset = str(full_dataset_path)
        else:
            dataset_path = body.get("dataset", "tests/fixtures/stress_long_tokens.c")
            full_dataset_path, err = resolve_dataset_file(dataset_path)
            if err:
                status = 404 if "not found" in err.lower() or "invalid" in err.lower() else 400
                if "not in the supported registry" in err:
                    status = 404
                self.send_json({"error": err}, status=status)
                return
            full_dataset = str(full_dataset_path)
            try:
                with open(full_dataset_path, "r", encoding="utf-8", errors="replace") as f:
                    source_text = f.read()
            except OSError:
                source_text = None

        try:
            buffer_size = int(body.get("buffer_size", 32))
            baseline_cap = int(body.get("baseline_cap", 16))
            alpha = float(body.get("alpha", 0.20))
            if buffer_size <= 0 or baseline_cap <= 0 or alpha <= 0:
                raise ValueError("Parameters must be positive")
        except (ValueError, TypeError) as ex:
            self.send_json({"error": f"Invalid numerical configuration: {ex}"}, status=400)
            return

        rule = body.get("rule", "combined")
        if rule not in ALLOWED_RULES:
            rule = "combined"
        if rule == "plsb":
            rule = "combined"
        mode = body.get("mode", "compare")
        if mode not in ["compare", "baseline", "plsb"]:
            mode = "compare"

        # 1. Run with JSON output for report
        cmd = [
            exe,
            "--mode", mode,
            "--buffer-size", str(buffer_size),
            "--baseline-cap", str(baseline_cap),
            "--rule", rule,
            "--alpha", str(alpha),
            "--format", "json",
            full_dataset
        ]

        try:
            proc = subprocess.run(cmd, cwd=str(BASE_DIR), capture_output=True, text=True, timeout=10)
            stdout = proc.stdout
            stderr = proc.stderr
            returncode = proc.returncode

            if returncode != 0:
                err_msg = stderr.strip() or stdout.strip() or f"Process failed with exit code {returncode}"
                self.send_json({
                    "success": False,
                    "returncode": returncode,
                    "command": " ".join(cmd),
                    "stdout": stdout,
                    "stderr": stderr,
                    "error": err_msg,
                    "report": {},
                    "tokens_baseline": [],
                    "tokens_plsb": [],
                    "tokens_total_count": 0
                })
                return

            report_data = {}
            try:
                decoder = json.JSONDecoder()
                s = stdout.strip()
                idx = 0
                while idx < len(s):
                    while idx < len(s) and s[idx].isspace():
                        idx += 1
                    if idx >= len(s) or s[idx] != '{':
                        break
                    obj, end_pos = decoder.raw_decode(s[idx:])
                    label = obj.get("label", "").lower()
                    if "baseline" in label:
                        report_data["baseline"] = obj
                    elif "plsb" in label:
                        report_data["plsb"] = obj
                    else:
                        report_data[f"run_{len(report_data)}"] = obj
                    idx += end_pos
            except Exception as ex:
                pass

            # 2. Run with --dump-tokens to get token stream for visual inspector
            tokens_cmd = [
                exe,
                "--mode", "baseline",
                "--buffer-size", str(buffer_size),
                "--baseline-cap", str(baseline_cap),
                "--dump-tokens",
                full_dataset
            ]
            tokens_proc = subprocess.run(tokens_cmd, cwd=str(BASE_DIR), capture_output=True, text=True, timeout=10)
            if tokens_proc.returncode != 0:
                err_msg = tokens_proc.stderr.strip() or tokens_proc.stdout.strip() or f"token dump failed ({tokens_proc.returncode})"
                self.send_json({
                    "success": False,
                    "returncode": tokens_proc.returncode,
                    "error": err_msg,
                    "report": report_data,
                    "tokens_baseline": [],
                    "tokens_plsb": [],
                    "tokens_total_count": 0
                })
                return
            tokens_baseline = self.parse_dumped_tokens(tokens_proc.stdout)

            tokens_cmd_plsb = [
                exe,
                "--mode", "plsb",
                "--buffer-size", str(buffer_size),
                "--baseline-cap", str(baseline_cap),
                "--rule", rule,
                "--alpha", str(alpha),
                "--dump-tokens",
                full_dataset
            ]
            tokens_proc_plsb = subprocess.run(tokens_cmd_plsb, cwd=str(BASE_DIR), capture_output=True, text=True, timeout=10)
            if tokens_proc_plsb.returncode != 0:
                err_msg = tokens_proc_plsb.stderr.strip() or tokens_proc_plsb.stdout.strip() or f"PLSB token dump failed ({tokens_proc_plsb.returncode})"
                self.send_json({
                    "success": False,
                    "returncode": tokens_proc_plsb.returncode,
                    "error": err_msg,
                    "report": report_data,
                    "tokens_baseline": tokens_baseline[:5000],
                    "tokens_plsb": [],
                    "tokens_total_count": len(tokens_baseline)
                })
                return
            tokens_plsb = self.parse_dumped_tokens(tokens_proc_plsb.stdout)

            equivalent, equiv_pct = token_streams_equivalent(tokens_baseline, tokens_plsb)
            display_tokens = group_preprocessor_display_tokens(tokens_baseline, source_text)

            self.send_json({
                "success": (returncode == 0),
                "returncode": returncode,
                "command": " ".join(cmd),
                "stdout": stdout,
                "stderr": stderr,
                "report": report_data,
                "tokens_native": tokens_baseline[:5000],
                "tokens_baseline": display_tokens[:5000],
                "tokens_plsb": tokens_plsb[:5000],
                "tokens_display": display_tokens[:5000],
                "tokens_total_count": len(tokens_baseline),
                "token_equivalence": equivalent,
                "token_equivalence_pct": equiv_pct,
                "metric_notes": {
                    "token_equivalence": "Percentage of native tokens whose type, lexeme, line, and column match between baseline and PLSB. This is not next-token prediction accuracy.",
                    "accuracy": "Next-token type accuracy is computed in the dashboard from observed transitions; it is not token equivalence."
                }
            })
        except subprocess.TimeoutExpired:
            self.send_json({"error": "Execution timed out"}, status=500)
        except Exception as e:
            self.send_json({"error": str(e)}, status=500)

    def handle_compare(self, body):
        exe = find_executable("plsb_lexer")
        if not exe:
            self.send_json({"error": "plsb_lexer executable not found. Please build the project first."}, status=500)
            return

        source_text = body.get("source")
        if source_text is not None:
            if not str(source_text).strip():
                self.send_json({"error": "Source is empty. Paste code or pick a registered dataset."}, status=400)
                return
            dest = BASE_DIR / "tests" / "fixtures" / "custom_temp.c"
            with open(dest, "w", encoding="utf-8") as f:
                f.write(source_text)
            full_dataset_path = dest.resolve()
            full_dataset = str(full_dataset_path)
            dataset_name = "Current analyzed source"
            dataset_path = "tests/fixtures/custom_temp.c"
        else:
            dataset_path = body.get("dataset", "tests/fixtures/sample_c.c")
            full_dataset_path, err = resolve_dataset_file(dataset_path)
            if err:
                status = 404 if "not found" in err.lower() or "invalid" in err.lower() or "not in the supported" in err else 400
                self.send_json({"success": False, "error": err}, status=status)
                return
            full_dataset = str(full_dataset_path)
            dataset_name = full_dataset_path.name

        try:
            buffer_size = int(body.get("buffer_size", 32))
            if buffer_size <= 0:
                raise ValueError("Buffer size must be positive")
        except (ValueError, TypeError) as ex:
            self.send_json({"success": False, "error": f"Invalid buffer_size: {ex}"}, status=400)
            return

        ablation = bool(body.get("ablation", False))
        requested = body.get("policies")
        if ablation and requested:
            policies = list(requested)
        else:
            policies = list(DEFAULT_COMPARE_POLICIES)

        run_id = f"cmp_{time.strftime('%Y%m%d_%H%M%S')}"
        results = []
        errors = []
        decoder = json.JSONDecoder()
        token_cache = {}

        def dump_tokens_for(mode, cap, rule):
            cmd = [
                exe,
                "--mode", mode,
                "--buffer-size", str(buffer_size),
                "--baseline-cap", str(cap),
                "--dump-tokens",
                full_dataset,
            ]
            if mode == "plsb":
                cmd = [
                    exe,
                    "--mode", "plsb",
                    "--buffer-size", str(buffer_size),
                    "--baseline-cap", str(cap),
                    "--rule", rule,
                    "--alpha", "0.20",
                    "--dump-tokens",
                    full_dataset,
                ]
            proc = subprocess.run(cmd, cwd=str(BASE_DIR), capture_output=True, text=True, timeout=30)
            if proc.returncode != 0:
                raise RuntimeError(proc.stderr.strip() or f"token dump failed ({proc.returncode})")
            return self.parse_dumped_tokens(proc.stdout)

        for policy in policies:
            if policy in POLICY_LABELS:
                label, category = POLICY_LABELS[policy]
            elif policy.startswith("baseline"):
                label, category = (policy, "baseline")
            else:
                label, category = (policy, "other")

            if policy.startswith("baseline"):
                cap = 16
                if policy == "baseline_32":
                    cap = 32
                elif policy == "baseline_64":
                    cap = 64
                elif policy not in ("baseline", "baseline_16"):
                    try:
                        cap = int(policy.split("_")[1])
                    except (IndexError, ValueError):
                        cap = 16
                cmd = [
                    exe,
                    "--mode", "baseline",
                    "--buffer-size", str(buffer_size),
                    "--baseline-cap", str(cap),
                    "--format", "json",
                    full_dataset,
                ]
                mode, rule, dump_cap = "baseline", "baseline", cap
            else:
                rule = "combined" if policy in ("combined", "plsb") else policy
                if rule not in ALLOWED_RULES:
                    errors.append(f"Unsupported policy: {policy}")
                    continue
                cmd = [
                    exe,
                    "--mode", "plsb",
                    "--buffer-size", str(buffer_size),
                    "--baseline-cap", "16",
                    "--rule", rule,
                    "--alpha", "0.20",
                    "--format", "json",
                    full_dataset,
                ]
                mode, dump_cap = "plsb", 16
                if not ablation and policy in ("combined", "plsb"):
                    label, category = POLICY_LABELS["combined"]

            try:
                proc = subprocess.run(cmd, cwd=str(BASE_DIR), capture_output=True, text=True, timeout=30)
                if proc.returncode != 0:
                    err_msg = proc.stderr.strip() or proc.stdout.strip() or f"exit {proc.returncode}"
                    errors.append(f"{policy}: {err_msg}")
                    continue
                s = proc.stdout.strip()
                if not s.startswith("{"):
                    errors.append(f"{policy}: native output was not JSON")
                    continue
                obj, _ = decoder.raw_decode(s)
                tokens = dump_tokens_for(mode, dump_cap, rule)
                obj["config_id"] = "combined" if policy in ("combined", "plsb") else policy
                token_cache[policy] = tokens
                token_cache[obj["config_id"]] = tokens
                obj["display_label"] = label
                obj["category"] = "plsb" if obj["config_id"] == "combined" else category
                obj["native_ok"] = True
                results.append(obj)
            except subprocess.TimeoutExpired:
                errors.append(f"{policy}: timed out")
            except Exception as ex:
                errors.append(f"{policy}: {ex}")

        if not results:
            self.send_json({
                "success": False,
                "run_id": run_id,
                "dataset": dataset_name,
                "dataset_path": dataset_path,
                "buffer_size": buffer_size,
                "error": errors[0] if errors else "Comparison produced no measurements",
                "errors": errors,
                "runs": [],
            }, status=500)
            return

        ref_tokens = token_cache.get("baseline_16") or (next(iter(token_cache.values())) if token_cache else [])
        ref_baseline = next((r for r in results if r.get("config_id") == "baseline_16"), None)
        if not ref_baseline:
            ref_baseline = results[0]

        enhanced_results = []
        for r in results:
            item = dict(r)
            cfg = r.get("config_id")
            toks = token_cache.get(cfg) or token_cache.get(r.get("config_id")) or []
            equivalent, equiv_pct = token_streams_equivalent(ref_tokens, toks)
            item["token_equivalence"] = equivalent
            item["token_equivalence_pct"] = equiv_pct

            base_reallocs = ref_baseline.get("spill_reallocations", 0)
            base_copies = ref_baseline.get("growth_copy_bytes", 0)
            base_tput = ref_baseline.get("tokens_per_second", 0)
            item["realloc_improvement_pct"] = improvement_pct(base_reallocs, r.get("spill_reallocations", 0))
            item["copy_improvement_pct"] = improvement_pct(base_copies, r.get("growth_copy_bytes", 0))
            item["throughput_improvement_pct"] = higher_is_better_pct(base_tput, r.get("tokens_per_second", 0))
            # Keep legacy keys for older clients, mapped to the improvement formula.
            item["realloc_delta_pct"] = item["realloc_improvement_pct"]
            item["copy_delta_pct"] = item["copy_improvement_pct"]
            enhanced_results.append(item)

        spill_events = [r.get("lexeme_spill_events", 0) for r in enhanced_results]
        reallocs = [r.get("spill_reallocations", 0) for r in enhanced_results]
        copies = [r.get("growth_copy_bytes", 0) for r in enhanced_results]
        zero_note = None
        if all(v == 0 for v in spill_events) and all(v == 0 for v in reallocs):
            zero_note = (
                "Measured zeros are real: this dataset and buffer size did not trigger "
                "spill-buffer growth. Use a smaller buffer or a stress dataset to measure reallocations."
            )

        self.send_json({
            "success": len(errors) == 0,
            "run_id": run_id,
            "dataset": dataset_name,
            "dataset_path": dataset_path,
            "buffer_size": buffer_size,
            "baseline_reference": ref_baseline.get("display_label"),
            "baseline_reference_id": ref_baseline.get("config_id"),
            "improvement_notes": {
                "lower_is_better": "Improvement (%) = (Baseline - PLSB) / Baseline * 100",
                "higher_is_better": "Improvement (%) = (PLSB - Baseline) / Baseline * 100",
                "zero_baseline": "N/A when the baseline value is 0 (no division by zero).",
                "reference": "Fixed Baseline 16B",
            },
            "zero_note": zero_note,
            "errors": errors,
            "error": ("; ".join(errors) if errors else None),
            "runs": enhanced_results,
            "charts": {
                "reallocations": {
                    "labels": [r.get("display_label") for r in enhanced_results],
                    "values": reallocs,
                    "unit": "count",
                },
                "growth_copy_bytes": {
                    "labels": [r.get("display_label") for r in enhanced_results],
                    "values": copies,
                    "unit": "bytes",
                },
            },
        })

    def parse_dumped_tokens(self, text):
        tokens = []
        for line in text.splitlines():
            line = line.strip()
            if not line or not line.startswith("["):
                continue
            # Format: [line:col] TOKEN_NAME 'lexeme' [SPILLED]
            try:
                pos_end = line.find("]")
                pos_str = line[1:pos_end]
                line_no, col_no = pos_str.split(":")
                rest = line[pos_end+1:].strip()
                is_spilled = "[SPILLED]" in rest
                rest = rest.replace("[SPILLED]", "").strip()
                first_quote = rest.find("'")
                last_quote = rest.rfind("'")
                type_name = rest[:first_quote].strip() if first_quote != -1 else rest
                lexeme = rest[first_quote+1:last_quote] if (first_quote != -1 and last_quote != -1) else ""
                tokens.append({
                    "line": int(line_no),
                    "col": int(col_no),
                    "type": type_name,
                    "lexeme": lexeme,
                    "spilled": is_spilled,
                    "length": len(lexeme)
                })
            except Exception:
                continue
        return tokens

    def handle_run_benchmark(self, body):
        exe = find_executable("benchmark_runner")
        if not exe:
            self.send_json({
                "success": False,
                "error": "benchmark_runner executable not found in bin/. Please run 'build.ps1' or 'make' first.",
                "data": None
            }, status=404)
            return

        cmd = [exe]
        dataset = body.get("dataset")
        all_datasets = bool(body.get("all_datasets", False))
        buffer_size = body.get("buffer_size")
        iterations = body.get("iterations")
        quick = bool(body.get("quick", False))
        full = bool(body.get("full", False))

        if all_datasets:
            cmd.append("--all-datasets")
        elif dataset:
            ds_path = (BASE_DIR / dataset).resolve()
            if not str(ds_path).startswith(str(BASE_DIR)) or not ds_path.is_file():
                self.send_json({"success": False, "error": f"Invalid or missing dataset: {dataset}"}, status=400)
                return
            cmd.extend(["--dataset", str(ds_path)])

        if buffer_size:
            try:
                buf_int = int(buffer_size)
                if buf_int <= 0: raise ValueError()
                cmd.extend(["--buffer-size", str(buf_int)])
            except ValueError:
                self.send_json({"success": False, "error": "Invalid buffer_size"}, status=400)
                return

        if iterations:
            try:
                iter_int = int(iterations)
                if iter_int <= 0 or iter_int > 100: raise ValueError()
                cmd.extend(["--iterations", str(iter_int)])
            except ValueError:
                self.send_json({"success": False, "error": "Invalid iterations (must be 1..100)"}, status=400)
                return

        if quick:
            cmd.append("--quick")
        elif full:
            cmd.append("--full")

        try:
            proc = subprocess.run(cmd, cwd=str(BASE_DIR), capture_output=True, text=True, timeout=120)
            
            if proc.returncode != 0:
                err_msg = proc.stderr.strip() or proc.stdout.strip() or f"Process failed with exit code {proc.returncode}"
                self.send_json({
                    "success": False,
                    "returncode": proc.returncode,
                    "command": " ".join(cmd),
                    "stdout": proc.stdout,
                    "stderr": proc.stderr,
                    "error": err_msg,
                    "data": None
                })
                return

            latest_path = RESULTS_DIR / "benchmark_latest.json"
            benchmark_data = None
            if latest_path.exists():
                with open(latest_path, "r", encoding="utf-8") as f:
                    benchmark_data = json.load(f)

            self.send_json({
                "success": True,
                "returncode": 0,
                "command": " ".join(cmd),
                "stdout": proc.stdout,
                "stderr": proc.stderr,
                "data": benchmark_data
            })
        except subprocess.TimeoutExpired:
            self.send_json({
                "success": False,
                "error": "Benchmark execution timed out (>120s)",
                "data": None
            }, status=504)
        except OSError as e:
            self.send_json({
                "success": False,
                "returncode": -1,
                "command": " ".join(cmd),
                "error": f"Failed to launch process ({e}). If an operating system policy blocked execution, please run the benchmark directly via terminal (e.g. '.\\bin\\benchmark_runner.exe --quick' or '.\\build.ps1 -Benchmark') and refresh the dashboard.",
                "stderr": str(e),
                "data": None
            })
        except Exception as e:
            self.send_json({
                "success": False,
                "error": f"Unexpected server error: {e}",
                "data": None
            }, status=500)

def main():
    port = 8080
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            pass

    server_address = ("127.0.0.1", port)
    httpd = http.server.HTTPServer(server_address, PLSBDashboardHandler)
    print(f"======================================================================")
    print(f"  PLSB Interactive Presentation Dashboard Server")
    print(f"  Listening at: http://127.0.0.1:{port}/")
    print(f"  Document Root: {DASHBOARD_DIR}")
    print(f"======================================================================")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server.")
        httpd.server_close()

if __name__ == "__main__":
    main()
