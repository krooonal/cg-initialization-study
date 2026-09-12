import random
import numpy as np
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass
from data_gen.schemas import InstanceSchema, Arc, Commodity, GraphData, Reference, InitialColumn
from data_gen.generators.utils import compute_initial_columns


@dataclass
class GridParams:
    nx: int
    ny: int
    num_commodities: int
    tightness: float
    cost_range: Tuple[float, float] = (1.0, 10.0)
    demand_range: Tuple[float, float] = (1.0, 5.0)
    capacity_scale: float = 1.0


def generate_grid(params: GridParams, seed: int) -> InstanceSchema:
    random.seed(seed)
    np.random.seed(seed)

    nx, ny = params.nx, params.ny
    num_nodes = nx * ny
    num_commodities = params.num_commodities

    arcs = []
    arc_id = 0

    for i in range(nx):
        for j in range(ny):
            node = i * ny + j
            if i + 1 < nx:
                neighbor = (i + 1) * ny + j
                cost = random.uniform(*params.cost_range)
                arcs.append(Arc(node, neighbor, 0.0, cost))
                arcs.append(Arc(neighbor, node, 0.0, cost))
                arc_id += 2
            if j + 1 < ny:
                neighbor = i * ny + (j + 1)
                cost = random.uniform(*params.cost_range)
                arcs.append(Arc(node, neighbor, 0.0, cost))
                arcs.append(Arc(neighbor, node, 0.0, cost))
                arc_id += 2

    commodities = []
    for k in range(num_commodities):
        origin = random.randint(0, num_nodes - 1)
        destination = random.randint(0, num_nodes - 1)
        while destination == origin:
            destination = random.randint(0, num_nodes - 1)
        demand = random.uniform(*params.demand_range)
        commodities.append(Commodity(origin, destination, demand))

    # Compute initial columns before creating InstanceSchema
    initial_columns = compute_initial_columns(num_nodes, arcs, commodities)

    name = f"grid_{nx}x{ny}_s{seed}_k{num_commodities}_t{params.tightness:.1f}"
    instance = InstanceSchema(
        name=name,
        generator="grid",
        seed=seed,
        params={
            "nx": nx, "ny": ny, "num_commodities": num_commodities,
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


def adjust_capacities_for_tightness(instance: InstanceSchema, params: GridParams) -> InstanceSchema:
    """Adjust capacities based on tightness parameter using seed routing."""
    from data_gen.certify import certify_instance

    num_arcs = len(instance.graph.arcs)
    num_commodities = len(instance.commodities)

    arc_loads = np.zeros(num_arcs)

    for k, comm in enumerate(instance.commodities):
        import heapq
        dist = np.full(instance.graph.num_nodes, np.inf)
        dist[comm.origin] = 0.0
        prev = np.full(instance.graph.num_nodes, -1, dtype=int)
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
                        prev[arc.head] = u
                        prev_arc[arc.head] = e_idx
                        heapq.heappush(pq, (nd, arc.head))

        cur = comm.destination
        while cur != comm.origin and cur != -1:
            e_idx = prev_arc[cur]
            if e_idx >= 0:
                arc_loads[e_idx] += comm.demand
            cur = prev[cur]

    for e_idx, arc in enumerate(instance.graph.arcs):
        if arc_loads[e_idx] > 0:
            r = random.uniform(0.5 * params.tightness, 1.5 * params.tightness)
            new_cap = r * arc_loads[e_idx] * params.capacity_scale
            arc.capacity = max(new_cap, 0.01)
        else:
            arc.capacity = 10.0 * params.capacity_scale

    result = certify_instance(instance.__dict__)
    if result.status == "infeasible":
        max_deficit_idx = np.argmax(arc_loads / np.array([a.capacity for a in instance.graph.arcs]))
        for i in range(10):
            factor = 1.0 + 0.2 * i
            for e_idx, arc in enumerate(instance.graph.arcs):
                if e_idx == max_deficit_idx:
                    arc.capacity *= factor
            result = certify_instance(instance.__dict__)
            if result.status == "optimal":
                break

    instance.reference = Reference(status=result.status, objective=result.objective)
    return instance