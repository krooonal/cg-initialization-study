# Experimental Design — Initialization in Column Generation

High-level design of the computational study comparing three ways to initialize the restricted master problem (RMP) in column generation:

1. **Big-M method**
2. **Phase 1 of the two-phase simplex method**
3. **Farkas pricing**

The study focuses exclusively on the **feasibility-restoration phase** of column generation, not on full convergence of the master to optimality.

## 1. Framing point — the three methods are not independent

The three methods are not three unrelated mechanisms; they form a hierarchy, and the experiment is designed around the axes on which they genuinely differ.

- **Big-M** and **two-phase** both *materialize artificial columns/variables* in the RMP. They differ only in how they leave them:
  - Big-M pushes artificials out of the basis via a large cost coefficient $M$;
  - two-phase first solves a pure-feasibility LP (minimize the sum of artificials), then warm-starts the original objective from the Phase-1 basis.
- **Farkas pricing** is the *column-generation-aware* realization of two-phase. Instead of adding artificials for every row up front, it takes the dual ray of the infeasible RMP, reads off which rows are responsible for the infeasibility, and prices against that ray so that only the columns actually needed to restore feasibility are generated. It never forms the large artificial LP.

Consequently, the informative empirical axes are:

- **(a)** explicit-artificials vs. ray-based feasibility restoration;
- **(b)** single-phase (Big-M) vs. two-phase handling;
- **(c)** sensitivity of Big-M to the choice of $M$.

## 2. Problem selection

### 2.1 Selection criteria

The study is only informative if the RMP is **genuinely LP-infeasible at the start** and takes real work to restore. This requires:

1. The problem is a **pure LP** — no branch-and-price needed (the LP optimum is the answer).
2. The full master LP is feasible, but the **restricted** master is not.
3. The master has **equality rows or tight linking constraints with no slack** — otherwise covering/packing formulations are trivially feasible via slack or dummy variables and none of the methods activate.
4. Feasibility restoration is **non-trivial** (tight instances, not fixed by one column per row).
5. The subproblem is **cheap and exact**, so master-side initialization behavior dominates the measured time and the differences between methods are not drowned in subproblem cost.

### 2.2 Candidates considered and rejected

| Problem | Reason rejected |
|---|---|
| Cutting stock / bin packing | Covering constraints with slack → RMP trivially feasible |
| Generalized assignment (GAP) | Packing; one column per item–machine pair gives immediate feasibility |
| Crew pairing / crew scheduling | IP → would require branch-and-price (see fallback, §4) |
| Vehicle routing with time windows | IP → would require branch-and-price |

### 2.3 Selected problem — path-based linear multicommodity flow (MCF)

See `mcf-model.md` for the full model. Summary of why it fits:

- **Pure LP.** Flow is splittable and continuous; fractional routing is the answer.
- **Equality demand rows** $\sum_{p \in P_k} x_p = d_k$ — no slack, so a commodity with no column makes the RMP infeasible.
- **Tight capacity rows** $\sum_k \sum_p a_{ep} x_p \le u_e$ — the cheapest paths (priced in first) jointly overflow arcs, so the RMP stays infeasible for several pricing rounds, while the full master LP remains feasible via rerouting.
- **Trivial subproblem** — a shortest path per commodity (Dijkstra), cheap and exact.
- **Difficulty knob** — capacity tightness: scale arc capacities so that the cheapest paths overflow some arcs, controlling how many rerouting rounds feasibility restoration takes.
- **Standard instances** — standard test networks exist; full MP optimum is an offline reference.

## 3. Experiment structure

- **Controlled comparison:** for each instance, all three methods start from the *same* RMP initial state. The `--use-initial-columns` flag controls this:
  - **Default (false):** All methods start from empty RMP (0 columns). RMP is trivially infeasible by construction — every demand row $\sum x = d_k$ has no variable. Big-M and Two-Phase add mechanism columns (dummies/artificials) and succeed; Pure Farkas has no artificials and causes GLOP's primal simplex to fail due to lack of a feasible primal basis.
  - **With flag (true):** All methods load the same `initial_columns` from instance JSON (1 shortest path per commodity). The seeded RMP is still infeasible by construction (single path capacity < demand, or joint overflow). Mechanism columns (Big-M dummies, Phase-1 artificials) are added by method modules on top of this shared starting point. On feasible instances, Pure Farkas succeeds with initial columns; on infeasible instances, Pure Farkas fails in GLOP because artificial variables are absent.
- **Big-M policy:** fixed a priori (e.g., $M = \kappa \cdot \max|c_{\text{obj}}|$); a separate **$M$-sweep** robustness study is run independently so the policy is not a confound.
- **Instance stratification:** vary capacity tightness (the difficulty knob) and network size.
- **Reporting:** medians with min–max, plus performance profiles (Dolan–Moré) over the instance set for time and iteration metrics.

## 4. Optional fallback — restricted-master MIP heuristic

If an IP-flavored test bed is ever desired (e.g., crew scheduling), the fallback is:

1. Run CG on the LP relaxation to restore RMP feasibility (the object of this study).
2. Once feasible, solve a **MIP over the current RMP columns** (restricted-master heuristic) to obtain an integer feasible solution, measuring time-to-integer-feasible-solution and its gap.

This avoids a full branch-and-price search. It is kept as an **optional extension**, not part of the core study, since MCF already provides the pure-LP setting.

## 5. Data collection — feasibility-only scope

The study stops when the RMP is first LP-feasible (plus a small extra budget for the quality metric). Optimality-of-the-master metrics are dropped.

### 5.1 Primary outcome

- **Iterations to feasibility** — pricing rounds until the RMP is LP-feasible.
- **Time to feasibility** — reported separately as *master time* and *pricing time*, so subproblem cost cannot mask the initialization comparison.
- **Pricing calls** (count and time) during the feasibility phase.
- **Columns generated** per round; **final RMP size** (rows × cols) at feasibility.

### 5.2 Quality of the feasible solution

- **Gap at feasibility:** RMP objective at the moment feasibility is restored, normalized against the offline full-MP optimum.
- **Gap after a fixed extra budget** (e.g., +$k$ rounds): captures the "head start" each method leaves for downstream CG without going to optimality.

### 5.3 Method-specific metrics

- **Big-M:** number of artificials remaining in the basis at feasibility; correctness of the $M$-sweep (do artificials exit the basis? is the final objective consistent with the Phase-1-equivalent value?); numerical behavior across the $M$ range.
- **Two-phase:** Phase-1 objective (sum of artificials) trajectory over rounds; warm-start quality (how much of the Phase-1 basis survives into Phase 2).
- **Farkas pricing:** number of rays used; support size of each ray (how many violated rows per ray); rounds to feasibility.

### 5.4 Robustness / degeneracy

- Stalling and degenerate pivots during the feasibility phase.
- Instances where a method fails to restore feasibility within a time/round budget.
- Confirmation that all methods started from the same empty RMP per instance.

### 5.5 Explicitly dropped

- Total CG iterations to optimality.
- Total time to (proven) optimality.
- Tail-off / final dual-bound convergence analysis.
