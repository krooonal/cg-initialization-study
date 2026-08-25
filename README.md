# Column Generation Initialization Study

A computational study comparing three methods for restoring LP-feasibility of the restricted master problem (RMP) in column generation:

1. **Big-M** — dummy/artificial columns at large cost M
2. **Two-phase simplex** — Phase-1 artificial columns minimized to zero, then Phase 2 warm-started
3. **Farkas pricing** — price against the dual ray of the infeasible RMP

## Problem Domain

Path-based linear multicommodity flow (MCF) with:
- Equality demand rows
- Packing capacity rows
- Pure LP (no integer constraints)

## Building

Requires Google OR-Tools C++ v9.14 (or compatible).

```bash
mkdir build && cd build
# If OR-Tools is installed in a non-standard location:
cmake -DORTOOLS_DIR=/path/to/ortools ..
# If OR-Tools is installed system-wide:
# cmake ..
make -j$(nproc)
```

## Running

```bash
# Run a single instance with a method (from build/ or with build/cg_study)
./build/cg_study instances/toy_feasible.json big_m
./build/cg_study instances/toy_feasible.json two_phase

# Farkas pricing requires initial columns to seed GLOP's primal simplex basis:
./build/cg_study instances/toy_feasible.json farkas --use-initial-columns

# Run with custom parameters and explicit output file
./build/cg_study instances/toy_feasible.json big_m --kappa 1000 --max-rounds 500 -o results/custom_run.json
```

## Testing

```bash
# Run unit and integration test binaries directly
./build/unit_tests
./build/integration_tests
```

## Data Generation

Requires Python 3 with `ortools` and `numpy` installed.

```bash
# Generate feasible grid instance
PYTHONPATH=. python3 -c "import sys, data_gen.certify, data_gen.schemas, data_gen.generators.grid, data_gen.generators.random, data_gen.generators.rmf, data_gen.generators.load_benchmark; sys.modules['generators.grid']=data_gen.generators.grid; sys.modules['generators.random']=data_gen.generators.random; sys.modules['generators.rmf']=data_gen.generators.rmf; sys.modules['generators.load_benchmark']=data_gen.generators.load_benchmark; sys.modules['schemas']=data_gen.schemas; sys.modules['certify']=data_gen.certify; from data_gen.generate import main; main()" --stratum feasible --generator grid --seed 35 --nx 10 --ny 10 --num-commodities 25 --tightness 0.6
```

## Batch Experiments

```bash
# Ensure build/cg_study is accessible or linked in working directory
./scripts/run_batch.sh
```

## Aggregating Results

```bash
python3 scripts/aggregate.py
```

## Output

Results are written to `results/` as JSON files with:
- Per-iteration logs (LP status, objective, simplex iterations, timing)
- Endpoint status (feasible/certified_infeasible/budget_exhausted)
- Metrics (total time, iterations, simplex iterations, gap to reference)
- Method-specific metrics (rays used, artificials in basis, etc.)

## Architecture

- `src/` — C++ implementation
  - `json.h` — Minimal JSON reader/writer
  - `graph.h/.cc` — Graph representation and path utilities
  - `instance.h/.cc` — Instance loading from JSON
  - `shortest_path.h/.cc` — Instrumented Dijkstra
  - `lp_solver.h/.cc` — MathOpt + GLOP wrapper (primal simplex, no presolve/scaling)
  - `rmp_model.h/.cc` — RMP model with incremental column addition
  - `init_method.h` — Abstract interface for the three methods
  - `methods/big_m.h/.cc` — Big-M implementation
  - `methods/two_phase.h/.cc` — Two-phase simplex implementation
  - `methods/farkas.h/.cc` — Farkas pricing implementation
  - `column_generation.h/.cc` — Method factory
  - `stats.h/.cc` — Metrics collection
  - `run_report.h/.cc` — JSON output
  - `main.cpp` — CLI entry point

- `data_gen/` — Python data generation
  - `generate.py` — CLI for instance generation
  - `certify.py` — Arc-based MCF certification using OR-Tools Python
  - `generators/` — Grid, Random, RMF-style generators
  - `schemas.py` — JSON schema definitions

- `scripts/` — Batch running and aggregation
  - `run_batch.sh` — Loops over instances/methods/parameters
  - `aggregate.py` — Medians, min/max, Dolan-Moré profiles

- `instances/` — Generated instances (git-ignored)
- `results/` — Run reports (git-ignored)
- `tests/` — Unit and integration tests