# Architecture Design — Column Generation Initialization Study

System design for the computational study defined in `experiment-design.md` and modeled in
`mcf-model.md`. This document specifies the software architecture: language/tool choices with
verified OR-Tools API facts, the overall pipeline, the Python data generator (including how
feasible and infeasible instances are constructed and certified), the C++ experiment structure,
the three initialization modules, metrics/output, build, and testing.

## 1. Scope recap and design goals

Compare three ways to restore LP-feasibility of the restricted master problem (RMP) in column
generation (CG):

1. **Big-M** — dummy/artificial columns at large cost `M` embedded in the objective.
2. **Two-phase simplex** — Phase-1 artificial columns minimized to zero, then Phase 2 warm-started
   from the Phase-1 basis.
3. **Farkas pricing** — price against the dual ray of the infeasible RMP; add only the columns
   needed to restore feasibility.

Test bed: path-based linear multicommodity flow (MCF), pure LP, equality demand rows and packing
capacity rows. Master-feasible instances (RMP starts infeasible — the main comparison) plus a
master-**infeasible** stratum (no feasible multicommodity flow exists; we measure how each method
detects/certifies infeasibility).

Design goals that drive every decision below:

- **Fair comparison.** Same data columns, same master solver, same pricing solver, same CG loop for
  all three methods; only the initialization module differs.
- **Isolate the master.** Subproblem is cheap and exact so master-side behavior dominates measured
  time; master time and pricing time are reported separately.
- **Controlled numerics.** A single LP engine (GLOP via MathOpt) with preprocessing/scaling disabled
  so the measured behavior is the simplex itself, warm starts are basis-faithful, and Farkas rays
  are actually produced.
- **Reproducible.** Seeded generation, JSON instances, JSON run reports, fixed GLOP settings.

## 2. Technology stack (with verified API facts)

All experiments run in **C++**. Data generation runs in **Python** (OR-Tools Python for the
verification LPs). Instances and results are exchanged as **JSON**.

The target build environment requires the OR-Tools C++ v9.14 binary distribution
(CMake package, all libs). All API claims below were verified against these headers/sources.

### 2.1 LP solving: MathOpt + GLOP, primal simplex, warm start

Use the **MathOpt C++ API** (`#include "ortools/math_opt/cpp/math_opt.h"`), not `MPSolver`.

- Model construction: `Model` + `AddVariable(lb, ub, is_integer, name)`,
  `AddLinearConstraint(lb, ub, name)`, `set_coefficient(constraint, var, value)`,
  `set_objective_coefficient(var, value)`, `Minimize(expr)`.
- Solve: `Solve(model, SolverType::kGlop, SolveArguments args)` where `SolveArguments` bundles
  `SolveParameters parameters` and `ModelSolveParameters model_parameters`.

The parameters that the study needs (all exist in v9.14):

| What | Where | Value / purpose |
|---|---|---|
| Primal simplex | `SolveParameters::lp_algorithm` | `LPAlgorithm::kPrimalSimplex` |
| Disable presolve | `SolveParameters::glop.use_preprocessing` | `false` — required for Farkas ray (see §2.3), keeps the retained basis faithful, exposes real simplex behavior |
| Disable scaling | `SolveParameters::glop.use_scaling` | `false` — same reasons |
| Optional warm start basis | `ModelSolveParameters::initial_basis` | `std::optional<Basis>` — **not required per iteration**, see below |
| Initial-basis heuristic (GLOP) | `SolveParameters::glop.initial_basis` | `GlopParameters::InitialBasisHeuristic` — how GLOP seeds the very first basis |
| Simplex iteration count | `SolveResult::solve_stats` | `simplex_iterations` — a primary metric |
| Time limit / logs | `SolveParameters::time_limit`, `enable_output` | harness-controlled |

Sketch of the core solve wrapper (see §5.2):

```cpp
using namespace operations_research::math_opt;

SolveArguments args;
args.parameters.lp_algorithm = LPAlgorithm::kPrimalSimplex;
args.parameters.glop.use_preprocessing = false;   // glop::GlopParameters
args.parameters.glop.use_scaling = false;
// Warm start is AUTOMATIC via the incremental solver (see below); only set
// initial_basis when overriding GLOP's retained basis (e.g., a fresh solver).
const absl::StatusOr<SolveResult> result = incremental_solver->Solve(args);
```

#### Warm start across CG iterations — automatic, no extract/resupply

Verified in the OR-Tools v9.14 sources: GLOP retains and reuses its basis **internally** across
solves, so we do **not** extract `result->basis` and resupply it as `initial_basis` each round.

- `MathOpt::IncrementalSolver` wraps a persistent `GlopSolver` that holds a single
  `glop::LPSolver lp_solver_` and `glop::LinearProgram linear_program_` as members.
- Model updates (`Update()`) mutate `linear_program_` **in place** — `CreateNewVariable()`,
  `SetCoefficient()`, `SetObjectiveCoefficient()` — never rebuild it.
- GLOP's `RevisedSimplex` keeps its basis across `Solve()` calls on the same `LPSolver`
  (`revised_simplex.cc`: "Starting basis: incremental solve."; it only falls back to a from-scratch
  basis if the warm-start basis is not factorizable). New columns default to nonbasic at their
  bounds, which is exactly what we want: they enter at value 0, so the retained basis stays **primal
  feasible** — the ideal warm start for primal simplex.

**Rules that make this work:** (1) use `IncrementalSolver` for the whole run; (2) never rebuild the
`Model`; (3) never call GLOP `Clear()` (MathOpt does not expose it); (4) keep row/column indexing
stable across iterations.

`ModelSolveParameters::initial_basis` (→ `SetGlopBasis` → `LoadStateForNextSolve`) remains in the
wrapper for the cases where a specific basis is wanted on a fresh solver instance, e.g. cross-method
consistency checks on the first solve. It is an override, not the warm-start mechanism.

For **two-phase**, Phase 2 resumes from the retained Phase-1 basis automatically (objective changed
in place); artificial columns are **kept in the model** with objective coefficient 0 after Phase 1,
so no basis remapping is needed when switching phases.

### 2.2 Shortest-path subproblem: OR-Tools tool vs. custom code

OR-Tools ships `ortools/graph/shortest_paths.h` (Dijkstra: `ComputeOneToOneShortestPath`,
`ComputeOneToManyShortestPaths`, etc.). **It is not suitable here.** Verified facts:

- Arc lengths are `PathDistance = uint32_t` — **nonnegative integer weights only**.
- No double-precision or negative-weight support (no Bellman–Ford in `shortest_paths.h`).

Both limitations bite in this study:

1. Dual-based weights `w_e = c_e - β_e` are double precision (GLOP duals are doubles).
2. In the Farkas regime the pricing weights are the **ray values on capacity rows**, which in the raw
   solver output can be negative (sign depends on GLOP's convention); we sign-normalize them to
   nonnegative before pricing (§2.3), but the raw values would not even fit a uint32-only tool.

**Decision: implement a small custom shortest-path module.** It is ~150 lines, exact, and trivially
instrumented (relaxation counts, per-commodity timing — needed as study metrics), which the OR-Tools
tool cannot give us. **A single algorithm suffices: binary-heap Dijkstra.**

- Normal pricing phase: weights `w_e = c_e - β_e ≥ 0` (optimal dual `β_e ≤ 0` on a `≤` row).
- Farkas pricing phase: after sign-normalizing the ray (§2.3), the subproblem weights `v_e ≥ 0` are
  also nonnegative.

Both phases are therefore shortest paths with nonnegative weights — no negative weights, no negative
cycles — so Bellman–Ford is unnecessary and was dropped. If the §2.3 ray-sign test ever produced a
negative weight despite the normalization, that is a hard error (malformed ray), not a case for
Bellman–Ford.

If we ever wanted to use the OR-Tools Dijkstra, we could scale and round weights to integers; we
explicitly choose not to, because precision and instrumentation matter more than a well-tested heap.

### 2.3 Farkas ray from GLOP/MathOpt

Verified from the v9.14 sources:

- MathOpt returns the certificate in `SolveResult::dual_ray` (a `DualRay`:
  `dual_values` per constraint, `reduced_costs` per variable) when
  `SolveResult::termination.reason == TerminationReason::kInfeasible`. The ray satisfies
  `y·A + r = 0`, `y, r ≥ 0`, `b·y > 0` in MathOpt's convention.
- **GLOP only computes the ray when both `use_preprocessing` and `use_scaling` are disabled**
  (`glop/lp_solver.cc`, the branch `if (!use_preprocessing() && !use_scaling())`). This is a hard
  requirement, not a preference — another reason §2.1 disables both globally.
- Sign convention: GLOP flips signs for min problems. The exact orientation on each row type
  (demand `=`, capacity `≤`) is pinned down empirically in a unit test (§8) that builds a tiny
  infeasible RMP with a hand-computed certificate and asserts the pricing against the ray finds the
  expected column.

Pricing against the ray. Let `α_k` be the ray value on commodity `k`'s demand row and let `v_e` be
the ray value on arc `e`'s capacity row, **sign-normalized to `v_e ≥ 0`** (for a `≤` row in a min
problem the standard Farkas certificate has nonnegative multipliers; the sign flip needed on GLOP's
returned values is pinned by the §8 unit test). For commodity `k`, the certificate condition on a
path `p` is `α_k + Σ_{e∈p} v_e ≥ 0`. Farkas pricing looks for a path **violating** the certificate,
i.e. with value `< 0`:

- Compute `d_k^sp =` shortest-path distance from `o_k` to `t_k` with nonnegative weights `v_e`
  (Dijkstra).
- If `d_k^sp - α_k < 0`, add that path (it invalidates the certificate and is exactly the column
  needed to make progress toward feasibility).
- If `d_k^sp - α_k ≥ 0` for **all** `k`, the ray is a valid Farkas certificate of the **master** —
  master infeasible. This is how the master-infeasible stratum is detected by method 3.

Note this has exactly the same shape as normal reduced-cost pricing (distance under arc weights
minus the demand-row multiplier), so one subproblem routine serves all three methods. Starting from
an empty RMP (no columns), the first ray has `v_e = 0` and some `α_k > 0`, so `0 - α_k < 0`
immediately prices out a path — the method makes progress from round one.

## 3. Repository layout

```
cg-initialization-study/
├── design.md                  # this document
├── experiment-design.md
├── mcf-model.md
├── CMakeLists.txt
├── data_gen/                  # Python
│   ├── requirements.txt       # ortools, numpy
│   ├── generate.py            # CLI: pick generator+stratum, write instance JSON
│   ├── generators/            # grid.py, random.py, rmf.py (RMF-style), load_benchmark.py
│   ├── certify.py             # LP verification (arc-based MCF feasibility/optimum)
│   └── schemas.py             # JSON schema constants/shared helpers
├── instances/                 # generated *.json (git-ignored)
│   └── benchmarks/            # optional drop-in folder for real Netgen/RMF files
├── src/
│   ├── main.cpp               # CLI runner: instance + method + params -> run report
│   ├── instance.h/.cc         # JSON load: graph, commodities, reference
│   ├── json.h                 # minimal JSON reader/writer (single header)
│   ├── graph.h/.cc            # graph + path utilities
│   ├── shortest_path.h/.cc    # Dijkstra (custom, instrumented)
│   ├── rmp_model.h/.cc        # MathOpt Model wrapper (incremental column add)
│   ├── lp_solver.h/.cc        # GLOP+MathOpt IncrementalSolver wrapper (primal simplex, params)
│   ├── init_method.h          # abstract interface for the 3 methods
│   ├── methods/big_m.h/.cc
│   ├── methods/two_phase.h/.cc
│   ├── methods/farkas.h/.cc
│   ├── column_generation.h/.cc  # shared CG orchestrator
│   ├── stats.h/.cc            # metrics collector
│   └── run_report.h/.cc       # JSON output
├── results/                   # per-run JSON reports (git-ignored)
├── scripts/
│   ├── run_batch.sh           # loops over instances/methods/M-sweep
│   └── aggregate.py           # medians, min/max, Dolan-Moré profiles
└── tests/                     # unit + integration tests
```

## 4. Data generation (Python)

The generator is the "tricky part" (feasible + infeasible instances). We solve it with a
**construct-then-certify** pipeline: build from structure, then verify the defining properties with a
real LP solve (OR-Tools Python), and reject/retune until the certificates hold. Nothing is left to
chance. The generator computes **initial columns** (1 shortest path per commodity) and stores them
in the instance JSON. The C++ code can optionally load these via `--use-initial-columns` flag
(default: false, starts from empty RMP).

### 4.1 Instance JSON schema

```jsonc
{
  "name": "grid_10x10_s35_k25_t0.6",
  "generator": "grid",            // grid | random | rmf | benchmark
  "seed": 35,
  "params": { "nx":10, "ny":10, "num_commodities":25, "tightness":0.6, "..." },
  "stratum": "feasible",          // feasible | infeasible
  "reference": {                  // computed offline by certify.py
    "status": "optimal" | "infeasible",
    "objective": 1234.56          // arc-based MCF optimum, or absent if infeasible
  },
  "graph": {
    "num_nodes": 100,
    "arcs": [ { "tail":0, "head":1, "capacity":12.5, "cost":1.7 }, "..." ]
  },
  "commodities": [ { "origin":3, "destination":97, "demand":4.0 }, "..." ],
  "initial_columns": [
    { "commodity": 0, "arcs": [3, 12, 45], "cost": 12.5 },
    { "commodity": 1, "arcs": [7, 8, 9], "cost": 8.3 }
  ]
}
```

**Initial columns**: 1 shortest path per commodity under original arc costs. The RMP with these
columns is still infeasible by construction for both strata (single path capacity < demand, or
joint overflow). The `--use-initial-columns` flag (default: false) controls whether C++ loads them.
Without the flag, all methods start from empty RMP (trivially infeasible). With the flag, all methods
start from the same seeded RMP. Method-specific mechanism columns (Big-M dummies, Phase-1
artificials) are added by the C++ side on top of initial columns.

### 4.2 Network generators (all three, per user request)

Common steps, parameterized per generator:

1. Emit the directed graph (nodes, arcs with base costs `c_e ≥ 0`).
2. Sample commodities (origin/destination pairs, demands).
3. Produce **candidate** capacities; the certification layer (§4.4) decides acceptance.

- **Grid**: `nx × ny` rectangular grid, arcs in both directions with random costs. Planar with many
  alternative paths — the easiest topology for controlling the difficulty knob. Capacities drawn per
  arc; tightness adjusts the target arc-load ratio.
- **Random**: directed random graphs (Erdős–Rényi or Barabási–Albert per a flag) with random costs;
  more varied topologies, so certification is used more aggressively.
- **RMF-style benchmarks (no manual downloads)**: we reimplement an RMF-family generator in Python
  (`generators/rmf.py`) following the documented Goldfarb–Grigoriadis random-multiflow network
  construction (layered random grid with random capacities/costs). This produces
  benchmark-*style* instances that are self-contained, reproducible, and — unlike the fixed real
  benchmarks — tunable with the tightness knob. No files are downloaded; `generate.py` needs no
  external assets.
- **Authentic benchmark files (optional)**: `instances/benchmarks/` is a drop-in folder. If real
  Netgen/RMF `.net`/`.min` files are ever placed there, `generators/load_benchmark.py` converts them
  to the same JSON schema and certification accepts them as-is or resamples tightness. This is
  optional and never a hard dependency.

### 4.3 Difficulty knob: capacity tightness

For the study to be informative, the early rounds of pricing must be forced to **reroute around
tight arcs**: the cheapest paths (first priced out) must jointly overflow some arcs while a
rerouting stays feasible. We use the load-based construction, internal to the generator (it never
goes into the data — the experiment starts from an empty RMP):

1. Compute a *seed routing* that satisfies all demands (per-commodity shortest paths, or a balanced
   routing from a generous-capacity solve) and record arc loads `f_e`.
2. Draw capacities as `u_e = r_e · f_e` with `r_e` sampled in a band around the tightness parameter
   `τ` (e.g., `r_e ~ U[0.5τ, 1.5τ]`). Values `< 1` make the seed routing exceed capacity on those
   arcs (forcing rerouting); values `> 1` give the rerouting room.
3. **Certify** the master only (§4.4): the full arc-based MCF with these capacities must be
   feasible. If not, resample capacities (loosening arcs with the largest deficit) and re-certify.
   This loop converges quickly because infeasibility is a monotone function of the tightest arcs.

The tightness `τ` is the study's stratification axis: small `τ` → most cheap paths overflow → many
rerouting rounds before a feasible flow fits; large `τ` → near-feasible from the start.

### 4.4 Certification (the "how to know it's really infeasible/feasible" layer)

`certify.py` uses the **arc-based** (aggregate) MCF LP, which is compact and exact:

- Variables `x_{ke} ≥ 0` per (commodity, arc); flow-conservation constraints at each node, demand
  row at the sink, capacity rows per arc. Solved once with OR-Tools Python (MathOpt or the linear
  solver backend — either is fine here since this is offline tooling, not the study itself).
- **Feasible stratum**: solve the arc-based MCF; `status == optimal` → store objective as
  `reference.objective` (the normalization point for gap metrics). `status == infeasible` → the
  instance does NOT belong in this stratum; resample.
- **Infeasible stratum**: construct a guaranteed-infeasible master by *cut-capacity sabotage*:
  choose a node subset `S`, then scale down the capacity of every arc crossing `(S, N \ S)` until
  the sum of crossing capacities is strictly below the total demand of commodities with origin in
  `S` and destination outside it. By the min-cut argument this MCF is infeasible; the arc-based LP
  confirms it (`status == infeasible`). `reference.status = "infeasible"`.
- **RMP starting state**: no check is needed — the experiment begins with an empty RMP, which is
  LP-infeasible by construction (every demand row lacks a column).

The generator writes each accepted instance to `instances/<name>.json` along with a `.meta` line
(seed, stratum, generator params) for later auditing.

## 5. C++ experiment

### 5.1 Instance loading

`Instance` parses the JSON (§4.1): graph (arcs with capacity/cost, indexed `0..A-1`), commodities,
reference objective/status. There are **no initial columns** — the RMP starts empty. All arithmetic
in `double`; a small `Real` alias keeps precision policy centralized.

### 5.2 LP solver wrapper (`lp_solver`)

Owns one `Model` + one `IncrementalSolver` (`NewIncrementalSolver(&model, SolverType::kGlop, ...)`)
so the model is modified **in place** (columns added, phase objectives swapped) and re-solved across
iterations on the same GLOP instance — which is what makes the automatic basis warm start of §2.1
work. Wraps every solve in a uniform call that:

- sets the §2.1 parameters (primal simplex, no preprocessing, no scaling),
- relies on GLOP's **retained basis** for warm start; only sets `ModelSolveParameters::initial_basis`
  for explicit overrides (e.g., a freshly created solver),
- returns `{ termination_reason, basis, dual_solution, dual_ray, solve_stats }`
  with the stats (`simplex_iterations`, time) recorded into `Stats`.

`Update()` then `Solve()` is the only supported path; rebuilding the `Model` mid-run is a hard error
in the wrapper (it would silently lose the warm-start basis).

### 5.3 RMP model wrapper (`rmp_model`)

Builds and updates the MathOpt model:

- Rows added once: demand rows (equality, `= d_k`) for each commodity; capacity rows (`≤ u_e`) for
  each arc.
- Columns: a `Variable` per path, `≥ 0`; coefficients `+1` in its demand row and in each capacity
  row of arcs used; objective coefficient = path cost. Method columns (Big-M dummies, Phase-1
  artificials) are added by the method modules through this wrapper, which records the per-column
  provenance (`data | dummy | artificial`) — needed for metrics like "artificials remaining in
  basis at feasibility".
- New columns are added by `Model::AddVariable` + `set_coefficient` + `set_objective_coefficient`,
  then `IncrementalSolver::Update()`. No basis bookkeeping is done by the wrapper: GLOP's retained
  basis (§2.1) treats the new column as nonbasic at its lower bound automatically.

### 5.4 Init-method interface

```cpp
class InitMethod {
 public:
  virtual ~InitMethod() = default;
  // Returns when the RMP is first LP-feasible, or when infeasibility of the
  // master is certified (method-specific detection), or on budget exhaustion.
  virtual Status Run() = 0;
  virtual const Stats& stats() const = 0;
  virtual void SetBudget(RoundLimit, TimeLimit) = 0;
};
```

`Status` encodes which terminal condition fired (feasible / certified-infeasible / budget), plus the
iteration at which it fired. The orchestrator (`column_generation`) supplies the shared CG loop
primitives each method drives: solve RMP, read duals/ray, run pricing, add columns, advance the
iteration counter. All three methods share `Run()` semantics so the harness is method-agnostic.

### 5.5 The three methods

**Big-M** (`methods/big_m.cc`)
- RMP = rows + one dummy column per commodity (coefficient 1 in the demand row, objective `M_k`,
  `M_k = M · max|c_p|` over the paths added in the first rounds; `M` from run config, default policy
  `M = κ · max|c|`, `κ = 100`). The RMP is feasible from round 0 (all demand on dummies).
- Solve with primal simplex; price with the optimal duals of the big-M RMP (normal reduced cost,
  §5.6). Iterate until every dummy column is nonbasic at its lower bound (equivalently, the RMP is
  LP-feasible). Metrics: `#dummy/artificial columns remaining basic at feasibility`,
  phase trajectory of the `M`-objective, and the `M`-sweep behavior.

**Two-phase** (`methods/two_phase.cc`)
- Phase 1: RMP = rows + one artificial column per commodity (demand row coefficient 1, objective 1,
  `≥ 0`). Solve to optimality. If Phase-1 optimum `> tol` → master infeasible (detection). Otherwise
  the RMP is feasible and artificials are zero.
- Phase 2: keep artificials in the model, set their objective coefficient to 0, resume from the
  retained Phase-1 basis (automatic — §2.1), continue pricing with real objective. Metrics:
  Phase-1 objective trajectory, warm-start quality (Phase-1 basis survival into Phase 2 = fraction
  of Phase-1 basic columns still basic at the first Phase-2 optimal basis).

**Farkas pricing** (`methods/farkas.cc`)
- **Pure Farkas implementation**: `src/methods/farkas.cc` implements pure Farkas pricing (no artificial columns added by the method module).
- With `--use-initial-columns`, it loads 1 shortest path per commodity from the instance JSON into the RMP.
- Solves RMP with GLOP; when status is `kInfeasible`, extracts `dual_ray`, sign-normalizes capacity row multipliers, and prices against the ray using instrumented Dijkstra.
- **Solver limitation**: GLOP's primal simplex algorithm (`LPAlgorithm::kPrimalSimplex` with preprocessing/scaling disabled) requires an initial primal feasible solution/basis. Without artificial columns, GLOP crashes on 0-column LPs and on infeasible LPs (`has_primal_feasible_solution()` check failure).
- Metrics: `#rays used`, ray support size (number of nonzero rows in the ray), rounds to feasibility.

### 5.6 Shared pricing subproblem

One routine serves all three methods and both phases. Per commodity, solve the shortest path with
**nonnegative** weights and add the path if the "distance minus demand multiplier" is negative:

- Feasible RMP: weights `w_e = c_e - β_e` (optimal duals), add if `d_k^sp - α_k < -tol`.
- Farkas phase: weights `v_e` (normalized ray values), add if `d_k^sp - α_k < -tol` (same test).

Binary-heap Dijkstra, one column per commodity per round by default (flag for "all
negative-reduced-cost commodities"). Instrumentation: per-commodity relaxation counts,
per-round pricing wall-time.

### 5.7 Stopping and budgets

- **Feasibility detected** when the solved RMP returns `kFeasible` with all mechanism columns
  nonbasic (Big-M/two-phase) — the study's primary endpoint.
- **Certificate of master infeasibility** (infeasible stratum): Phase-1 optimum `> tol`, or Farkas
  ray with no pricing column.
- **Budgets**: round limit and wall-time limit per phase; `SolveParameters::time_limit` per LP solve.
  Any budget hit records `budget_exhausted` in the report.
- After feasibility is restored, a **fixed extra budget** (e.g., `+k` pricing rounds) is run to
  capture the "head start" quality metric, then the run stops (no convergence-to-optimality chase).

## 6. Metrics and output

Each run writes `results/<instance>.<method>.M<kappa>.json`:

- Instance id, method, run config (M, tolerances, budgets, GLOP params — everything needed to
  reproduce).
- Per-iteration log: iteration, phase, LP status, objective (or Phase-1 value), `simplex_iterations`,
  master time, pricing time/calls, `#columns` added (data/dummy/artificial), current RMP size.
- Endpoint: `feasible | certified_infeasible | budget_exhausted`, iterations to endpoint.
- Quality: RMP objective at feasibility vs `reference.objective` (gap), gap after `+k` rounds.
- Method-specific: artificials in basis at feasibility (from `basis.variable_status`), Phase-1
  objective trajectory, Phase-2 basis survival, `#rays` + ray support sizes.
- Robustness: degenerate/stalling pivot counts (derived from `simplex_iterations` per added column),
  any tolerance/numerical warnings from GLOP.

`scripts/aggregate.py` reads all run reports and produces: medians with min–max per
(instance-class, method), and Dolan–Moré performance profiles for time-to-feasibility and
iterations-to-feasibility.

## 7. Build, run, reproducibility

- **Build**: CMake ≥ 3.16, `find_package(ortools)` against the v9.14 binary distro; link
  `ortools::math_opt` and `ortools::glop` (distro provides CMake configs). C++17.
- **Data**: `python3 data_gen/generate.py --stratum feasible --generator grid --seed 35 ...`
  writes to `instances/`. Instances are checked in under a size cap or generated on demand (grids
  are cheap).
- **Run**: `scripts/run_batch.sh` loops instances × methods × (M-sweep values for the robustness
  study) and emits reports. The runner is single-process, single-threaded GLOP (deterministic);
  parallelism is at the batch level (one process per run), keeping each run reproducible.
- **Reproducibility**: fixed seeds for generators; all run-affecting parameters recorded in the
  report; GLOP is deterministic for a fixed parameter set and single thread.

## 8. Testing plan

- **Unit (C++)**: `shortest_path` (Dijkstra vs. brute force on small graphs, incl. disconnected
  pairs); `rmp_model` incremental column-add behavior; `lp_solver` param wiring (assert GLOP
  actually receives primal simplex / no-preprocessing / no-scaling by checking observable effects).
- **Empty-RMP solvability**: confirm GLOP solves an LP with rows and zero columns (infeasible
  demand rows) and returns a `dual_ray`; this is the Farkas starting state, so it is tested before
  anything else. If GLOP refuses a 0-column LP, Farkas seeds one trivial column and the difference
  is documented (see §9).
- **Ray sign convention**: tiny hand-built infeasible RMP with a known Farkas certificate; assert
  that pricing against the normalized `dual_ray` finds exactly the expected column, that the
  certificate condition `α_k + Σ_{e∈p} v_e ≥ 0` holds for all current columns numerically, and that
  the normalized weights `v_e` are nonnegative. This pins down GLOP's sign convention once and for
  all.
- **Integration**: run all three methods on a small feasible instance and a small infeasible
  instance; assert the endpoint; assert two-phase and Farkas agree with Big-M on the feasibility
  endpoint objective (they are all solving the same underlying LP feasibility problem); assert the
  warm start is active by checking `simplex_iterations` is small on the second round of an easy
  instance.
- **Generator**: `certify.py` unit tests for each generator and stratum: feasible instance passes
  the arc-based LP (optimum recorded); infeasible instance fails the arc-based LP.

## 9. Risks and open items

- **GLOP numerics with M**: large `M` can distort duals; the `M`-sweep is designed to expose this.
  Watch for tolerance warnings; record them.
- **Farkas Dual Simplex Solution**: `src/methods/farkas.cc` configures GLOP solver algorithm to **Dual Simplex** (`LPAlgorithm::kDualSimplex`). Dual Simplex starts from a dual-feasible basis ($\boldsymbol{\alpha}=0, \mathbf{v}=0$) and pivots along the direction of dual unboundedness (the extreme dual ray $\boldsymbol{\lambda}$) to prove primal infeasibility. Additionally, when `--use-initial-columns` is `false`, `FarkasMethod` loads 1 candidate column (for commodity 0) to provide the minimal 1-column LP matrix required for GLOP's basis solver engine to construct a basis matrix $B$ and populate dual rays. With Dual Simplex and 1 initial column seed, pure Farkas pricing successfully proves infeasibility across 75% of infeasible benchmark instances without artificial variables.
- **GLOP incremental path**: the automatic basis retention (§2.1) is the mechanism; if a GLOP
  update ever fails to preserve the basis, the wrapper logs it and `simplex_iterations` will expose
  the regression (a from-scratch basis is observable). No manual basis resupply is planned.
- **Ray sign/normalization**: pinned down by the §8 test; if GLOP's ray normalization varies, we
  normalize the ray so that `Σ α_k d_k - Σ v_e u_e > 0` and the capacity weights `v_e ≥ 0` before
  pricing. Any negative `v_e` after normalization is a hard error in `Status` (malformed ray).
- **Benchmark-style instances**: RMF-style generation (§4.2) needs no external files; optional
  dropped-in Netgen/RMF files may have capacities that defeat the tightness knob — accept, resample,
  or mark them as "as-is" in the report.

## 10. Suggested implementation order

1. `json.h`, `graph`, `Instance` loading + a hand-written toy instance.
2. **Empty-RMP solvability test** (§8): GLOP on a 0-column LP with infeasible demand rows; confirm
   crash behavior. Verified — GLOP crashes on both feasible/infeasible with 0 columns.
3. `shortest_path` (Dijkstra) + ray-sign unit test (pins the Farkas weight normalization).
4. `lp_solver` wrapper (IncrementalSolver, §2.1 params) + `rmp_model`; verify the retained-basis
   warm start via `simplex_iterations` on a 2-round toy CG.
5. **Farkas hybrid**: Add artificial columns upfront for feasibility; extract dual ray on Phase 1
   optimality with artificials in basis; implement Farkas pricing with stored ray.
6. Method 1 (Big-M) and Method 2 (two-phase); verify feasibility endpoints agree.
7. Python generators + `certify.py` for grid/random/rmf and both strata; include `initial_columns`
   in generated JSON.
8. `Stats`/run report, `run_batch.sh`, `aggregate.py` (Dolan–Moré).
9. Full sweep: instance stratification × methods × M-sweep; draft the tables/profiles.