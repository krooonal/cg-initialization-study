#!/usr/bin/env python3
import os
import sys
import glob
import subprocess
import json
import time

EXECUTABLE = "./build/cg_study"
OUTPUT_DIR = "results_farkas_comparison"
OS_PATH_PREFIX = "/home/krunal/snap/antigravity/5/.local/bin"
ENV = dict(os.environ)
ENV["PATH"] = f"{OS_PATH_PREFIX}:{ENV.get('PATH', '')}"

os.makedirs(OUTPUT_DIR, exist_ok=True)

# Collect instances
instance_paths = sorted(
    glob.glob("instances/*.json") +
    glob.glob("instances/study_small/*.json") +
    glob.glob("instances/scale_study/*.json")
)

print(f"Found {len(instance_paths)} instances to benchmark.")

results = []

for inst_path in instance_paths:
    inst_name = os.path.basename(inst_path).replace(".json", "")
    
    with open(inst_path, "r") as f:
        inst_json = json.load(f)
    
    stratum = inst_json.get("stratum", "unknown")
    num_commodities = len(inst_json.get("commodities", []))
    
    if "toy" in inst_name:
        scale = "Toy"
    elif num_commodities <= 5:
        scale = "Small"
    elif num_commodities <= 25:
        scale = "Medium"
    elif num_commodities <= 35:
        scale = "Large"
    else:
        scale = "XL"

    topology = "Grid" if ("grid" in inst_name or "toy" in inst_name) else ("Random" if "random" in inst_name else "RMF")

    for method in ["farkas", "farkas_dual"]:
        for use_init in [True, False]:
            mode_label = "All Init Cols" if use_init else "1 Init Col Seed"
            out_file = os.path.join(OUTPUT_DIR, f"{inst_name}.{method}.{'init' if use_init else 'no_init'}.json")
            
            cmd = [EXECUTABLE, inst_path, method, "-o", out_file]
            if use_init:
                cmd.append("--use-initial-columns")

            print(f"Running {method} ({mode_label}) on {inst_name}...")
            try:
                res = subprocess.run(cmd, env=ENV, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=120)
                if res.returncode == 0 and os.path.exists(out_file):
                    with open(out_file, "r") as rf:
                        data = json.load(rf)
                    data["_scale"] = scale
                    data["_topology"] = topology
                    data["_stratum"] = stratum
                    data["_mode"] = mode_label
                    data["_method_variant"] = "Farkas (Primal Simplex)" if method == "farkas" else "Farkas (Dual Simplex)"
                    results.append(data)
                else:
                    print(f"Error running {cmd}: {res.stderr}")
            except subprocess.TimeoutExpired:
                print(f"Timeout running {cmd}")

print(f"Benchmark completed successfully. {len(results)} runs recorded.")

with open(os.path.join(OUTPUT_DIR, "all_results.json"), "w") as f:
    json.dump(results, f, indent=2)
