import heapq
import numpy as np
from typing import List, Tuple
from data_gen.schemas import Arc, Commodity, InitialColumn


def dijkstra_shortest_path(
    origin: int,
    destination: int,
    num_nodes: int,
    arcs: List[Arc]
) -> Tuple[List[int], float]:
    """
    Compute shortest path using Dijkstra's algorithm.
    Returns (path_arcs, path_cost) or ([], inf) if no path exists.
    """
    dist = np.full(num_nodes, np.inf)
    dist[origin] = 0.0
    prev_arc = np.full(num_nodes, -1, dtype=int)

    pq = [(0.0, origin)]
    while pq:
        d, u = heapq.heappop(pq)
        if d > dist[u]:
            continue
        if u == destination:
            break
        for e_idx, arc in enumerate(arcs):
            if arc.tail == u:
                nd = d + arc.cost
                if nd < dist[arc.head]:
                    dist[arc.head] = nd
                    prev_arc[arc.head] = e_idx
                    heapq.heappush(pq, (nd, arc.head))

    if dist[destination] == np.inf:
        return [], float('inf')

    # Reconstruct path
    path_arcs = []
    cur = destination
    while cur != origin and cur != -1:
        e_idx = prev_arc[cur]
        if e_idx >= 0:
            path_arcs.append(int(e_idx))
        cur = next((a.tail for a_idx, a in enumerate(arcs) if a_idx == e_idx), -1)

    path_arcs.reverse()
    return path_arcs, float(dist[destination])


def compute_initial_columns(
    num_nodes: int,
    arcs: List[Arc],
    commodities: List[Commodity]
) -> List[InitialColumn]:
    """
    Compute 1 shortest path per commodity using original arc costs.
    This gives each method the same infeasible starting point.
    """
    initial_cols = []
    for k, comm in enumerate(commodities):
        path_arcs, path_cost = dijkstra_shortest_path(
            comm.origin, comm.destination,
            num_nodes, arcs
        )
        if path_arcs:
            initial_cols.append(InitialColumn(
                commodity=k,
                arcs=path_arcs,
                cost=path_cost
            ))
    return initial_cols