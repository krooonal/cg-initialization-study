import random
import numpy as np
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass
from data_gen.schemas import InstanceSchema, Arc, Commodity, GraphData, Reference, InitialColumn
from data_gen.generators.utils import compute_initial_columns


@dataclass
class RandomParams:
    num_nodes: int
    num_arcs: int
    num_commodities: int
    tightness: float
    graph_type: str = "erdos_renyi"
    erdos_p: float = 0.1
    barabasi_m: int = 3
    cost_range: Tuple[float, float] = (1.0, 10.0)
    demand_range: Tuple[float, float] = (1.0, 5.0)
    capacity_scale: float = 1.0


def generate_random(params: RandomParams, seed: int) -> InstanceSchema:
    random.seed(seed)
    np.random.seed(seed)

    num_nodes = params.num_nodes
    arcs = []

    if params.graph_type == "erdos_renyi":
        for i in range(num_nodes):
            for j in range(num_nodes):
                if i != j and random.random() < params.erdos_p:
                    cost = random.uniform(*params.cost_range)
                    arcs.append(Arc(i, j, 0.0, cost))
    elif params.graph_type == "barabasi_albert":
        edges = []
        for i in range(params.barabasi_m):
            edges.append((i, params.barabasi_m))
        for i in range(params.barabasi_m + 1, num_nodes):
            targets = []
            total_deg = sum(1 for u, v in edges if u == i or v == i)
            for _ in range(params.barabasi_m):
                deg_dist = [sum(1 for u, v in edges if u == node or v == node) for node in range(i)]
                if sum(deg_dist) == 0:
                    target = random.randint(0, i - 1)
                else:
                    target = random.choices(range(i), weights=deg_dist)[0]
                edges.append((i, target))
        for u, v in edges:
            cost = random.uniform(*params.cost_range)
            arcs.append(Arc(u, v, 0.0, cost))
            arcs.append(Arc(v, u, 0.0, cost))

    commodities = []
    for k in range(params.num_commodities):
        origin = random.randint(0, num_nodes - 1)
        destination = random.randint(0, num_nodes - 1)
        while destination == origin:
            destination = random.randint(0, num_nodes - 1)
        demand = random.uniform(*params.demand_range)
        commodities.append(Commodity(origin, destination, demand))

    # Compute initial columns before creating InstanceSchema
    initial_columns = compute_initial_columns(num_nodes, arcs, commodities)

    name = f"random_{params.graph_type}_n{num_nodes}_a{len(arcs)}_s{seed}_k{params.num_commodities}_t{params.tightness:.1f}"
    instance = InstanceSchema(
        name=name,
        generator="random",
        seed=seed,
        params={
            "num_nodes": num_nodes, "num_arcs": len(arcs),
            "num_commodities": params.num_commodities,
            "tightness": params.tightness, "graph_type": params.graph_type,
            "cost_range": params.cost_range, "demand_range": params.demand_range
        },
        stratum="feasible",
        reference=Reference(status="optimal"),
        graph=GraphData(num_nodes=num_nodes, arcs=arcs),
        commodities=commodities,
        initial_columns=initial_columns
    )

    return instance


def adjust_capacities_for_tightness(instance: InstanceSchema, params: RandomParams) -> InstanceSchema:
    from data_gen.certify import certify_instance

    num_arcs = len(instance.graph.arcs)
    arc_loads = np.zeros(num_arcs)

    for k, comm in enumerate(instance.commodities):
        import heapq
        dist = np.full(instance.graph.num_nodes, np.inf)
        dist[comm.origin] = 0.0
        prev_arc = np.full(instance.graph.num_nodes, -1, dtype=int)

        pq = [(0.0, comm.origin)]
        while pq:
            d, u = heapq.heappop(pq)
            if d > dist[u]:
                continue
            if u == comm.destination:
                break
            for e_idx, arc in enumerate(instance.graph.arcs):
                if arc.tail == u:
                    nd = d + arc.cost
                    if nd < dist[arc.head]:
                        dist[arc.head] = nd
                        prev_arc[arc.head] = e_idx
                        heapq.heappush(pq, (nd, arc.head))

        cur = comm.destination
        while cur != comm.origin and cur != -1:
            e_idx = prev_arc[cur]
            if e_idx >= 0:
                arc_loads[e_idx] += comm.demand
            cur = next((a.tail for a_idx, a in enumerate(instance.graph.arcs) if a_idx == e_idx), -1)

    for e_idx, arc in enumerate(instance.graph.arcs):
        if arc_loads[e_idx] > 0:
            r = random.uniform(0.5 * params.tightness, 1.5 * params.tightness)
            new_cap = r * arc_loads[e_idx] * params.capacity_scale
            arc.capacity = max(new_cap, 0.01)
        else:
            arc.capacity = 10.0 * params.capacity_scale

    result = certify_instance(instance.__dict__)
    if result.status == "infeasible":
        for i in range(10):
            factor = 1.0 + 0.2 * i
            for arc in instance.graph.arcs:
                arc.capacity *= factor
            result = certify_instance(instance.__dict__)
            if result.status == "optimal":
                break

    instance.reference = Reference(status=result.status, objective=result.objective)
    return instance