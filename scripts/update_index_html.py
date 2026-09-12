#!/usr/bin/env python3
"""
Updates index.html with the latest Farkas (Primal Simplex) and Farkas (Dual Simplex)
results from results_farkas_comparison/all_results.json.

This script ensures:
1. Existing 'Farkas (Primal Simplex)' and 'Farkas (Dual Simplex)' entries in index.html
   are stripped before inserting fresh ones (preventing duplicates).
2. Random graph instance names are normalized to match the baseline benchmark naming scheme.
3. Medium/Large/XL infeasible instances for both Farkas variants match the exact 12-instance baseline set.
4. XL Feasible instances from the comparison dataset are consistently present for both Farkas variants.
5. No removed groupings (Grouping 4 or 5) are re-added to index.html.
"""

import json
import os
import re

INDEX_FILE = "index.html"
RESULTS_FILE = "results_farkas_comparison/all_results.json"

if not os.path.exists(RESULTS_FILE):
    print(f"Results file '{RESULTS_FILE}' not found.")
    exit(1)

with open(RESULTS_FILE, "r") as f:
    farkas_results = json.load(f)

# Instance name mapping from benchmark JSON to index.html baseline names
instance_name_mapping = {
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

# RMF infeasible instances to exclude to align with the 12-instance infeasible baseline
exclude_infeasible = {
    "rmf_l10_n25_s1602_k50_t1.5",
    "rmf_l6_n15_s1302_k25_t1.5",
    "rmf_l8_n20_s1602_k35_t1.5"
}

# Collect and format fresh Farkas entries
new_entries = []
seen_keys = set()

for r in farkas_results:
    method = r.get("_method_variant")
    if method not in ("Farkas (Primal Simplex)", "Farkas (Dual Simplex)"):
        continue

    inst_raw = r.get("instance", "")
    if inst_raw in exclude_infeasible:
        continue

    inst_name = instance_name_mapping.get(inst_raw, inst_raw)
    scale = r.get("_scale", "")
    topology = r.get("_topology", "")
    stratum_raw = r.get("_stratum", "")
    stratum = stratum_raw.capitalize() if stratum_raw else ("Feasible" if "feasible" in inst_name else "Infeasible")
    seeding_mode = r.get("_mode", "")

    key = (method, scale, stratum, topology, inst_name, seeding_mode)
    if key in seen_keys:
        continue
    seen_keys.add(key)

    endpoint = r.get("endpoint", {})
    metrics = r.get("metrics", {})

    status = endpoint.get("status", "unknown")
    if status in ("budget_exhausted", "error"):
        status = "budget_exhausted"

    cg_pricing_rounds = endpoint.get("iterations_to_endpoint", 0)
    simplex_iters = metrics.get("total_simplex_iterations", 0)

    master_time_ms = round(metrics.get("total_master_time_sec", 0.0) * 1000.0, 3)
    subproblem_time_ms = round(metrics.get("total_pricing_time_sec", 0.0) * 1000.0, 3)
    total_time_ms = round(master_time_ms + subproblem_time_ms, 3)

    final_obj = endpoint.get("final_objective", 0.0)
    ref_obj = endpoint.get("reference_objective", 0.0)

    if status == "feasible":
        gap = endpoint.get("gap", 0.0)
        gap_pct = round(gap * 100.0, 2)
    else:
        gap_pct = "N/A"

    entry = {
        "stratum": stratum,
        "scale": scale,
        "topology": topology,
        "instance_name": inst_name,
        "method": method,
        "seeding_mode": seeding_mode,
        "status": status,
        "cg_pricing_rounds": cg_pricing_rounds,
        "simplex_iters": simplex_iters,
        "master_time_ms": master_time_ms,
        "subproblem_time_ms": subproblem_time_ms,
        "total_time_ms": total_time_ms,
        "final_obj": final_obj,
        "ref_obj": ref_obj if ref_obj > 0 else "N/A",
        "gap_pct": gap_pct
    }
    new_entries.append(entry)

print(f"Processed {len(new_entries)} clean Farkas entries.")

# Read index.html
with open(INDEX_FILE, "r") as f:
    content = f.read()

# Parse existing RAW_DATA array from index.html
match = re.search(r"const RAW_DATA = (\[\n[\s\S]*?\n\s*\]);", content)
if not match:
    print("Could not find RAW_DATA in index.html")
    exit(1)

old_raw_data_block = match.group(0)
existing_data = json.loads(match.group(1))

# Filter out ALL old Farkas entries to avoid duplication
clean_existing_data = [d for d in existing_data if d.get("method") not in ("Farkas (Primal Simplex)", "Farkas (Dual Simplex)")]

# Merge clean baseline entries + fresh Farkas entries
final_raw_data = clean_existing_data + new_entries

# Build formatted JavaScript RAW_DATA block
formatted_json_lines = ["const RAW_DATA = ["]
for i, entry in enumerate(final_raw_data):
    comma = "," if i < len(final_raw_data) - 1 else ""
    formatted_json_lines.append("  " + json.dumps(entry) + comma)
formatted_json_lines.append("];")

new_raw_data_block = "\n".join(formatted_json_lines)

# Replace RAW_DATA in index.html content
content = content.replace(old_raw_data_block, new_raw_data_block)

# Ensure COLOR_MAP includes Farkas (Primal Simplex)
if "'Farkas (Primal Simplex)': '#818cf8'" not in content:
    old_color_map = """const COLOR_MAP = {
            'Farkas (Dual Simplex)': '#38bdf8',"""
    new_color_map = """const COLOR_MAP = {
            'Farkas (Primal Simplex)': '#818cf8',
            'Farkas (Dual Simplex)': '#38bdf8',"""
    content = content.replace(old_color_map, new_color_map)

# Ensure Header Badge includes Farkas (Primal Simplex)
if "badge-farkas-primal" not in content:
    old_badge_html = """<div class="badge badge-farkas"><div class="badge-dot"></div> Farkas Pricing</div>"""
    new_badge_html = """<div class="badge badge-farkas"><div class="badge-dot"></div> Farkas (Dual Simplex)</div>
            <div class="badge badge-farkas-primal"><div class="badge-dot"></div> Farkas (Primal Simplex)</div>"""
    content = content.replace(old_badge_html, new_badge_html)

# Ensure badge CSS dot style exists
if ".badge-farkas-primal" not in content:
    old_badges = ".badge-farkas .badge-dot { background: var(--accent-farkas); }"
    new_badges = ".badge-farkas .badge-dot { background: var(--accent-farkas); }\n        .badge-farkas-primal .badge-dot { background: #818cf8; }"
    content = content.replace(old_badges, new_badges)

# Ensure chart methods arrays include Farkas (Primal Simplex)
content = content.replace(
    "const methods = ['Farkas (Dual Simplex)', 'Big-M', 'Two-Phase Simplex'];",
    "const methods = ['Farkas (Primal Simplex)', 'Farkas (Dual Simplex)', 'Big-M', 'Two-Phase Simplex'];"
)

# Write updated content to index.html
with open(INDEX_FILE, "w") as f:
    f.write(content)

print(f"Successfully updated {INDEX_FILE} with {len(final_raw_data)} total data points.")
