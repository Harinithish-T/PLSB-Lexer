#!/usr/bin/env python3
"""
Create and verify final handoff ZIP for PLSB_Lexer project.
"""

import hashlib
import os
import sys
import zipfile
from pathlib import Path

def get_sha256(filepath):
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def main():
    root_dir = Path(__file__).resolve().parent.parent
    handoff_dir = root_dir / "handoff"
    handoff_dir.mkdir(exist_ok=True)
    zip_path = handoff_dir / "PLSB_Lexer_Handoff_Final.zip"

    # Exclude patterns
    excluded_extensions = {".pdb", ".tmp", ".pyc"}
    excluded_dirs = {".git", "__pycache__", ".vscode", ".idea", "handoff"}

    print(f"Creating handoff ZIP from: {root_dir}")
    print(f"Target archive: {zip_path}")

    total_files = 0
    total_uncompressed_bytes = 0

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for root, dirs, files in os.walk(root_dir):
            dirs[:] = [d for d in dirs if d not in excluded_dirs]
            
            for file in files:
                ext = os.path.splitext(file)[1].lower()
                if ext in excluded_extensions:
                    continue
                if file.endswith("_probe.exe") or file.endswith("_probe.c"):
                    continue
                if file == "PLSB_Lexer_Handoff_Final.zip":
                    continue

                full_path = Path(root) / file
                rel_path = full_path.relative_to(root_dir)
                archive_name = Path("PLSB_Lexer") / rel_path

                zf.write(full_path, str(archive_name))
                total_files += 1
                total_uncompressed_bytes += full_path.stat().st_size

    zip_size = zip_path.stat().st_size
    checksum = get_sha256(zip_path)

    print("\n----------------------------------------------------------------------")
    print(f"Archive Created Successfully:")
    print(f"  Primary Path:       {zip_path}")
    print(f"  Archived Files:     {total_files} files")
    print(f"  Uncompressed Size:  {total_uncompressed_bytes:,} bytes ({total_uncompressed_bytes / (1024*1024):.2f} MB)")
    print(f"  Compressed Size:    {zip_size:,} bytes ({zip_size / 1024:.1f} KB)")
    print(f"  SHA256:             {checksum}")
    print("----------------------------------------------------------------------")

    # Verify archive readability
    print("Verifying archive readability and file integrity...")
    with zipfile.ZipFile(zip_path, "r") as zf:
        bad_file = zf.testzip()
        if bad_file:
            print(f"[FAIL] Corrupted file in archive: {bad_file}")
            sys.exit(1)
        namelist = zf.namelist()
        required_samples = [
            "PLSB_Lexer/src/lexer.c",
            "PLSB_Lexer/src/token_array.h",
            "PLSB_Lexer/dashboard/app.js",
            "PLSB_Lexer/dashboard/server.py",
            "PLSB_Lexer/docs/final_implementation_verification_report.md",
            "PLSB_Lexer/results/benchmark_latest.json",
            "PLSB_Lexer/results/benchmark_summary.csv",
            "PLSB_Lexer/results/benchmark_raw.csv",
            "PLSB_Lexer/scripts/verify_results_consistency.py",
            "PLSB_Lexer/scripts/test_dashboard_endpoints.py"
        ]
        for req in required_samples:
            if req not in namelist:
                print(f"[FAIL] Missing required artifact in archive: {req}")
                sys.exit(1)
            print(f"  [PASS] Verified in ZIP: {req}")

    print("\n[PASS] Archive 100% verified and ready for handoff!")

if __name__ == "__main__":
    main()
