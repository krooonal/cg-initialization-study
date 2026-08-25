import json
import os
from typing import Dict, List, Optional
from ..schemas import InstanceSchema, Arc, Commodity, GraphData, Reference


def parse_netgen_file(filepath: str) -> Optional[InstanceSchema]:
    """Parse Netgen .net format file."""
    try:
        with open(filepath, 'r') as f:
            lines = f.readlines()
    except FileNotFoundError:
        return None

    if not lines or lines[0].strip() != 'c MIN':
        return None

    num_nodes = 0
    num_arcs = 0
    num_commodities = 0

    i = 1
    while i < len(lines):
        line = lines[i].strip()
        if line.startswith('p min'):
            parts = line.split()
            num_nodes = int(parts[2])
            num_arcs = int(parts[3])
        elif line.startswith('n '):
            pass
        elif line.startswith('a '):
            pass
        elif line.startswith('c commodities'):
            i += 1
            num_commodities = int(lines[i].strip())
            break
        i += 1

    arcs = []
    i = 1
    while i < len(lines):
        line = lines[i].strip()
        if line.startswith('a '):
            parts = line.split()
            tail = int(parts[1]) - 1
            head = int(parts[2]) - 1
            capacity = float(parts[4])
            cost = float(parts[5])
            arcs.append(Arc(tail, head, capacity, cost))
        i += 1

    commodities = []
    while i < len(lines):
        line = lines[i].strip()
        if line.startswith('c commodity'):
            i += 1
            parts = lines[i].strip().split()
            origin = int(parts[0]) - 1
            destination = int(parts[1]) - 1
            demand = float(parts[2])
            commodities.append(Commodity(origin, destination, demand))
        i += 1

    name = os.path.basename(filepath).replace('.net', '')
    return InstanceSchema(
        name=name,
        generator="benchmark",
        seed=0,
        params={},
        stratum="feasible",
        reference=Reference(status="optimal"),
        graph=GraphData(num_nodes=num_nodes, arcs=arcs),
        commodities=commodities
    )


def load_benchmarks(benchmark_dir: str) -> List[InstanceSchema]:
    """Load all benchmark files from directory."""
    instances = []
    for filename in os.listdir(benchmark_dir):
        if filename.endswith('.net') or filename.endswith('.min'):
            filepath = os.path.join(benchmark_dir, filename)
            instance = parse_netgen_file(filepath)
            if instance:
                instances.append(instance)
    return instances