import json
from typing import Dict, Any, List, Optional
from dataclasses import dataclass, asdict


@dataclass
class Arc:
    tail: int
    head: int
    capacity: float
    cost: float


@dataclass
class Commodity:
    origin: int
    destination: int
    demand: float


@dataclass
class Reference:
    status: str
    objective: Optional[float] = None


@dataclass
class GraphData:
    num_nodes: int
    arcs: List[Arc]


@dataclass
class InitialColumn:
    commodity: int
    arcs: List[int]
    cost: float


@dataclass
class InstanceSchema:
    name: str
    generator: str
    seed: int
    params: Dict[str, Any]
    stratum: str
    reference: Reference
    graph: GraphData
    commodities: List[Commodity]
    initial_columns: List[InitialColumn]


def write_instance(filepath: str, instance: InstanceSchema) -> None:
    data = asdict(instance)
    with open(filepath, 'w') as f:
        json.dump(data, f, indent=2)


def read_instance(filepath: str) -> InstanceSchema:
    with open(filepath, 'r') as f:
        data = json.load(f)
    return InstanceSchema(**data)