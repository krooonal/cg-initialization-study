# Multicommodity Flow via Column Generation — Model Description

This document describes the linear multicommodity flow (MCF) problem used as the test bed for the column generation (CG) initialization study, along with the master problem (MP), restricted master problem (RMP), and pricing subproblem (SP) models.

## 1. Problem statement

We are given a directed graph $G = (N, A)$ and a set of commodities $K$. Each commodity $k \in K$ has:

- an origin node $o_k \in N$,
- a destination node $t_k \in N$,
- a demand $d_k > 0$ to be routed from $o_k$ to $t_k$.

Each arc $e \in A$ has:

- a capacity $u_e \ge 0$ (upper bound on total flow on the arc),
- a per-unit routing cost $c_e \ge 0$.

Flow is **splittable and continuous**: the demand of a commodity may be divided arbitrarily over paths. We must route every commodity's demand from origin to destination so that no arc's capacity is exceeded, minimizing total routing cost.

This is a **linear program** (the linear multicommodity flow problem). Fractional routing is the answer; no integrality is required. This is why pure column generation (no branch-and-price) is sufficient.

## 2. Notation

| Symbol | Meaning |
|---|---|
| $N$ | set of nodes |
| $A$ | set of arcs |
| $K$ | set of commodities |
| $o_k, t_k$ | origin / destination of commodity $k$ |
| $d_k$ | demand of commodity $k$ |
| $u_e$ | capacity of arc $e$ |
| $c_e$ | per-unit cost of arc $e$ |
| $P_k$ | set of all $o_k$–$t_k$ paths in $G$ |
| $P_k^R$ | finite subset of $P_k$ present in the RMP |
| $x_p$ | amount of flow routed on path $p$ |
| $a_{ep}$ | $1$ if arc $e$ lies on path $p$, $0$ otherwise |
| $c_p$ | cost of path $p$, $c_p = \sum_{e \in p} c_e$ |
| $\alpha_k$ | dual variable of the demand constraint of $k$ (free) |
| $\beta_e$ | dual variable of the capacity constraint of $e$ ($\beta_e \le 0$) |

## 3. Master problem (MP)

The MP is the full path-based formulation.

### 3.1 Decision variables

- $x_p \ge 0$ for every $k \in K$, $p \in P_k$: amount of commodity $k$'s demand routed on path $p$.

There is one variable per path, and the number of paths can be exponential in the graph size, so the MP is solved implicitly via column generation.

### 3.2 Objective

Minimize total routing cost:

$$
\min \sum_{k \in K} \sum_{p \in P_k} c_p \, x_p
$$

### 3.3 Constraints

1. **Demand (equality).** For each commodity $k \in K$:

$$
\sum_{p \in P_k} x_p = d_k
$$

   The total flow of commodity $k$ over all its paths must equal its demand exactly. Dual variable $\alpha_k$, unrestricted in sign.

   **Why this matters for the study:** this is an equality row with no slack. If the RMP contains no path for some commodity, this row is unsatisfiable and the RMP is LP-infeasible — the situation the initialization study is about.

2. **Capacity (packing).** For each arc $e \in A$:

$$
\sum_{k \in K} \sum_{p \in P_k} a_{ep} \, x_p \le u_e
$$

   Total flow on arc $e$ (summed over all commodities and paths) must not exceed its capacity. Dual variable $\beta_e \le 0$.

   This is a packing row. Capacity infeasibility (the columns priced in by the early rounds jointly overflow an arc) is the second source of RMP infeasibility.

3. **Nonnegativity.** $x_p \ge 0$ for all $p$.

### 3.4 Remarks

- The constraint matrix is column-based: each column is a path with a "$1$" in the demand row of its commodity and a "$1$" in the capacity rows of the arcs it uses.
- The LP relaxation of this formulation is the linear multicommodity flow problem itself; the objective value of the MP is the optimum of the original problem.

## 4. Restricted master problem (RMP)

The RMP is identical to the MP, except that each commodity's path set is restricted to a finite subset:

$$
P_k^R \subseteq P_k, \qquad |P_k^R| < \infty
$$

The RMP is what is actually solved at each CG iteration. Because it is a restriction of the MP:

- its optimal value is an upper bound on... (see remark below),
- it is only meaningful once it is feasible.

### 4.1 Infeasibility of the RMP

The RMP can be LP-infeasible even though the full MP is feasible. Two causes:

1. **Missing commodity columns:** some $k$ has $P_k^R = \emptyset$, so its equality demand row cannot be satisfied. In this study every run **starts from an empty RMP** ($P_k^R = \emptyset$ for all $k$), so this is the initial state by construction.
2. **Capacity violation:** the columns priced in by the early rounds jointly exceed arc capacities, and no split of the available columns fits within capacities. This is what keeps the RMP infeasible for several rounds on tight instances: feasibility restoration requires generating rerouting paths.

The equality demand rows guarantee there is no slack to absorb infeasibility: restoration *requires* generating new columns (via pricing) or introducing artificial variables (Big-M / Phase 1 / Farkas pricing). This is precisely the object of the initialization study.

### 4.2 Duality

Given an optimal basis of the feasible RMP, let $\alpha_k$ and $\beta_e$ be the corresponding dual values for the demand and capacity constraints. These duals drive the pricing subproblem.

## 5. Pricing subproblem (SP)

The SP decides whether any column of the full MP has negative reduced cost and, if so, produces one (or several).

### 5.1 Reduced cost

The reduced cost of a path $p \in P_k$ with respect to the current duals is

$$
r_p = c_p - \alpha_k - \sum_{e \in p} \beta_e
    = \sum_{e \in p} (c_e - \beta_e) - \alpha_k
$$

A column prices out (should be added to the RMP) iff $r_p < 0$.

### 5.2 Shortest-path subproblem

For a fixed commodity $k$, the term $-\alpha_k$ is constant. Minimizing the reduced cost is therefore the **shortest path problem** from $o_k$ to $t_k$ with arc weights

$$
w_e = c_e - \beta_e
$$

That is, solve for each $k \in K$:

$$
\min_{p \in P_k} \sum_{e \in p} w_e
$$

Let $\bar d_k$ be the resulting shortest-path distance. Then:

- if $\bar d_k - \alpha_k < 0$, the shortest path $p_k^*$ has negative reduced cost and is added to the RMP;
- if $\bar d_k - \alpha_k \ge 0$ for all $k$, no column prices out and the current RMP solution is optimal for the full MP.

### 5.3 Notes

- Since $\beta_e \le 0$ and $c_e \ge 0$, the weights $w_e = c_e - \beta_e \ge 0$, so Dijkstra's algorithm applies directly.
- In the Farkas phase the pricing weights are the (sign-normalized, nonnegative) ray values on the capacity rows, so the same Dijkstra subproblem applies with the same "distance minus demand multiplier" test. See `design.md` §2.3.
- When the RMP is infeasible, the duals $\alpha_k, \beta_e$ are not available from an optimal basis. The three initialization methods differ precisely in how they obtain a pricing signal in this regime:
  - **Big-M** and **two-phase** add artificial variables/columns so a (Phase-1) basis exists;
  - **Farkas pricing** uses the dual ray of the infeasible RMP as the pricing signal, targeting only the rows responsible for infeasibility.

## 6. Starting point and mechanism columns

To make the initialization comparison well defined, the study fixes the **same starting point** for
all three methods on each instance: an **empty RMP** (no paths at all). The RMP is then LP-infeasible
by construction — every demand row $\sum_{p \in P_k^R} x_p = d_k$ has no variable — so no
capacity-aware or pre-computed column set is part of the data, and no method receives an information
advantage.

Method-specific *mechanism* columns are added by the experiment code, never baked into the data:

- **Big-M:** one dummy column per commodity (coefficient $1$ in the demand row, objective cost $M_k$), which makes the RMP feasible from round 0 with all demand routed on dummies;
- **two-phase:** one artificial column per commodity (objective $1$ in Phase 1, set to $0$ in Phase 2), minimized to zero in Phase 1;
- **Farkas pricing:** no mechanism columns added by the method module — the RMP is priced against its dual ray directly. When `--use-initial-columns` is enabled, 1 shortest path per commodity is loaded to provide an initial basis for GLOP's primal simplex algorithm.

Because the mechanism columns differ per method, keeping them out of the instance data keeps the
comparison clean: the methods differ only in their initialization module, and the pricing/master
machinery is identical.

## 7. Relation to the study

- **Difficulty knob:** capacity tightness. Scale arc capacities $u_e$ so that the cheapest priced-out paths jointly overflow some arcs; the harder the joint routing feasibility (the more rerouting rounds are needed), the more interesting the initialization comparison.
- **Subproblem cost:** the SP is a plain shortest path per commodity — cheap and exact — so master-side initialization behavior dominates the measured time, which is what the study isolates.
- **Optimality reference:** the full MP optimum (obtained offline once per instance) serves as the normalization point for the "quality of the feasible solution at feasibility" metric.
