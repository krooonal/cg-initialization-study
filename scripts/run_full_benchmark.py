#!/usr/bin/env python3
"""
Master Benchmark Runner Script.
Runs all 4 initialization methods (Two-Phase, Big-M, Farkas Dual Simplex, Farkas Primal Simplex)
across ALL 32 instances (Toy, Small, Medium, Large, XL; Feasible and Infeasible)
for both Warm Start (All Init Cols) and Cold Start modes.
Saves unified results into results_full/all_results.json.
"""

import os
import sys
import glob
import subprocess
import json
import time

EXECUTABLE = "./build/cg_study"
OUTPUT_DIR = "results_full"
OS_PATH_PREFIX = "/home/krunal/snap/antigravity/5/.local/bin"
ENV = dict(os.environ)
ENV["PATH"] = f"{OS_PATH_PREFIX}:{ENV.get('PATH', '')}"

os.makedirs(OUTPUT_DIR, exist_ok=True)

# Collect all instance paths
instance_paths = sorted(
    glob.glob("instances/*.json") +
    glob.glob("instances/study_small/*.json") +
    glob.glob("instances/scale_study/*.json") +
    glob.glob("instances/scale_study_loose/*.json")
)

print(f"Collected {len(instance_paths)} instance files for full benchmark suite.")

def determine_metadata(inst_path, inst_json):
    inst_name = os.path.basename(inst_path).replace(".json", "")
    
    stratum_raw = inst_json.get("stratum", "")
    stratum = stratum_raw.capitalize() if stratum_raw else ("Feasible" if "feasible" in inst_name else "Infeasible")
    
    if "toy" in inst_name:
        scale = "Toy"
    elif "4x4" in inst_name or "n15_a60" in inst_name or "l3_n4" in inst_name:
        scale = "Small"
    elif "10x10" in inst_name or "n80_a" in inst_name or "l6_n15" in inst_name:
        scale = "Medium"
    elif "15x15" in inst_name or "n150_a" in inst_name or "l8_n20" in inst_name:
        scale = "Large"
    elif "20x20" in inst_name or "n250_a" in inst_name or "l10_n25" in inst_name:
        scale = "XL"
    else:
        scale = "Medium"
        
    if "grid" in inst_name or "toy" in inst_name:
        topology = "Grid"
    elif "random" in inst_name:
        topology = "Random"
    else:
        topology = "RMF"

    # Normalize instance name for baseline mapping
    mapping = {
        "random_erdos_renyi_n80_a610_s202_k25_t1.5": "random_n80_a610_s202_k25",
        "random_erdos_renyi_n150_a1776_s502_k35_t1.5": "random_n150_a1776_s502_k35",
        "random_erdos_renyi_n250_a3007_s502_k50_t1.5": "random_n250_a3007_s502_k50",
        "random_erdos_renyi_n150_a1811_s501_k35_t1.5": "random_n150_a1811_s501",
        "random_erdos_renyi_n250_a3082_s501_k50_t1.5": "random_n250_a3082_s501",
        "random_erdos_renyi_n80_a678_s201_k25_t1.5": "random_n80_a678_s201_k25",
        "random_erdos_renyi_n15_a60_s201_k5_t0.6": "random_n15_a60_s201_k3",
        "random_erdos_renyi_n15_a60_s202_k5_t0.6": "random_n15_a60_s202_k3_t2.0",
        "grid_4x4_s101_k5_t0.6": "grid_4x4_s101_k3_t2.0",
        "grid_4x4_s102_k5_t0.6": "grid_4x4_s102_k3_t2.0"
    }
    normalized_name = mapping.get(inst_name, inst_name)

    return scale, stratum, topology, inst_name, normalized_name

methods_config = [
    ("two_phase", "Two-Phase Simplex"),
    ("big_m", "Big-M"),
    ("farkas_dual", "Farkas (Dual Simplex)"),
    ("farkas", "Farkas (Primal Simplex)")
]

all_results = []
start_time = time.time()
total_runs = len(instance_paths) * len(methods_config) * 2
run_count = 0

for inst_path in instance_paths:
    with open(inst_path, "r") as f:
        inst_json = json.load(f)
        
    scale, stratum, topology, raw_name, norm_name = determine_metadata(inst_path, inst_json)
    
    for cli_method, display_name in methods_config:
        for use_init in [True, False]:
            run_count += 1
            if use_init:
                mode_label = "All Init Cols"
            else:
                if "Farkas" in display_name or stratum == "Infeasible":
                    mode_label = "1 Init Col Seed"
                else:
                    mode_label = "No Init"

            out_file = os.path.join(OUTPUT_DIR, f"{raw_name}.{cli_method}.{'init' if use_init else 'cold'}.json")
            
            cmd = [EXECUTABLE, inst_path, cli_method, "-o", out_file]
            if use_init:
                cmd.append("--use-initial-columns")

            print(f"[{run_count}/{total_runs}] Running {display_name} ({mode_label}) on {raw_name}...")
            
            try:
                res = subprocess.run(cmd, env=ENV, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=180)
                if res.returncode == 0 and os.path.exists(out_file):
                    with open(out_file, "r") as rf:
                        data = json.load(rf)
                    
                    data["_scale"] = scale
                    data["_topology"] = topology
                    data["_stratum"] = stratum
                    data["_mode"] = mode_label
                    data["_method_variant"] = display_name
                    data["_normalized_instance"] = norm_name
                    
                    all_results.append(data)
                else:
                    print(f"  ERROR executing {cmd}: {res.stderr}")
            except subprocess.TimeoutExpired:
                print(f"  TIMEOUT running {cmd}")

elapsed = time.time() - start_time
print(f"\nFinished full benchmark suite: {len(all_results)} successful runs in {elapsed:.2f} seconds.")

with open(os.path.join(OUTPUT_DIR, "all_results.json"), "w") as f:
    json.dump(all_results, f, indent=2)

print(f"Saved aggregated results to {os.path.join(OUTPUT_DIR, 'all_results.json')}.")
