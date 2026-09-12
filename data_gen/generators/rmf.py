import random
import numpy as np
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass
from data_gen.schemas import InstanceSchema, Arc, Commodity, GraphData, Reference, InitialColumn
from data_gen.generators.utils import compute_initial_columns


@dataclass
class RMFParams:
    layers: int
    nodes_per_layer: int
    num_commodities: int
    tightness: float
    cost_range: Tuple[float, float] = (1.0, 20.0)
    demand_range: Tuple[float, float] = (1.0, 10.0)
    capacity_scale: float = 1.0
    cross_layer_prob: float = 0.3


def generate_rmf(params: RMFParams, seed: int) -> InstanceSchema:
    random.seed(seed)
    np.random.seed(seed)

    layers = params.layers
    nodes_per_layer = params.nodes_per_layer
    num_nodes = layers * nodes_per_layer

    arcs = []
    for layer in range(layers - 1):
        for i in range(nodes_per_layer):
            u = layer * nodes_per_layer + i
            for j in range(nodes_per_layer):
                v = (layer + 1) * nodes_per_layer + j
                if random.random() < params.cross_layer_prob:
                    cost = random.uniform(*params.cost_range)
                    arcs.append(Arc(u, v, 0.0, cost))
                    arcs.append(Arc(v, u, 0.0, cost))

    for layer in range(layers):
        for i in range(nodes_per_layer):
            u = layer * nodes_per_layer + i
            for j in range(nodes_per_layer):
                if i != j:
                    v = layer * nodes_per_layer + j
                    if random.random() < 0.1:
                        cost = random.uniform(*params.cost_range)
                        arcs.append(Arc(u, v, 0.0, cost))

    commodities = []
    for k in range(params.num_commodities):
        origin_layer = random.randint(0, layers - 2)
        dest_layer = random.randint(origin_layer + 1, layers - 1)
        origin = origin_layer * nodes_per_layer + random.randint(0, nodes_per_layer - 1)
        destination = dest_layer * nodes_per_layer + random.randint(0, nodes_per_layer - 1)
        demand = random.uniform(*params.demand_range)
        commodities.append(Commodity(origin, destination, demand))

    # Compute initial columns before creating InstanceSchema
    initial_columns = compute_initial_columns(num_nodes, arcs, commodities)

    name = f"rmf_l{layers}_n{nodes_per_layer}_s{seed}_k{params.num_commodities}_t{params.tightness:.1f}"
    instance = InstanceSchema(
        name=name,
        generator="rmf",
        seed=seed,
        params={
            "layers": layers, "nodes_per_layer": nodes_per_layer,
            "num_commodities": params.num_commodities,
            "tightness": params.tightness, "cost_range": params.cost_range,
            "demand_range": params.demand_range
        },
        stratum="feasible",
        reference=Reference(status="optimal"),
        graph=GraphData(num_nodes=num_nodes, arcs=arcs),
        commodities=commodities,
        initial_columns=initial_columns
    )

    return instance


def adjust_capacities_for_tightness(instance: InstanceSchema, params: RMFParams) -> InstanceSchema:
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


def generate_infeasible_rmf(params: RMFParams, seed: int) -> InstanceSchema:
    random.seed(seed + 1000)
    np.random.seed(seed + 1000)

    instance = generate_rmf(params, seed + 1000)

    origin_layer = 0
    dest_layer = params.layers - 1

    S_nodes = set()
    for layer in range(origin_layer + 1):
        for i in range(params.nodes_per_layer):
            S_nodes.add(layer * params.nodes_per_layer + i)

    crossing_arcs = []
    for e_idx, arc in enumerate(instance.graph.arcs):
        if arc.tail in S_nodes and arc.head not in S_nodes:
            crossing_arcs.append(e_idx)

    total_cross_cap = sum(instance.graph.arcs[e].capacity for e in crossing_arcs)
    total_demand = sum(
        c.demand for c in instance.commodities
        if c.origin in S_nodes and c.destination not in S_nodes
    )

    if total_cross_cap > 0 and total_demand > 0:
        scale = 0.5 * total_demand / total_cross_cap
        for e_idx in crossing_arcs:
            instance.graph.arcs[e_idx].capacity *= scale

    instance.stratum = "infeasible"
    instance.reference = Reference(status="infeasible")

    return instance