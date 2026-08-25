#!/usr/bin/env python3
"""
Certification module for MCF instances.
Uses OR-Tools Python to solve the arc-based MCF LP and verify feasibility/optimality.
"""

import json
import sys
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass
from ortools.math_opt.python import mathopt
import numpy as np


@dataclass
class CertificationResult:
    status: str
    objective: Optional[float] = None
    is_feasible: bool = False


def build_arc_mcf_model(instance_data: dict) -> mathopt.Model:
    """Build the arc-based MCF model from instance data."""
    model = mathopt.Model(name="arc_mcf")

    num_nodes = instance_data["graph"]["num_nodes"]
    arcs = instance_data["graph"]["arcs"]
    commodities = instance_data["commodities"]

    num_arcs = len(arcs)
    num_commodities = len(commodities)

    x = {}
    for k in range(num_commodities):
        for e in range(num_arcs):
            x[(k, e)] = model.add_variable(lb=0.0, name=f"x_{k}_{e}")

    for k in range(num_commodities):
        comm = commodities[k]
        origin = comm["origin"]
        destination = comm["destination"]
        demand = comm["demand"]

        for v in range(num_nodes):
            flow_balance = mathopt.LinearExpression()
            for e in range(num_arcs):
                if arcs[e]["head"] == v:
                    flow_balance += x[(k, e)]
                if arcs[e]["tail"] == v:
                    flow_balance -= x[(k, e)]

            if v == origin:
                model.add_linear_constraint(flow_balance == -demand)
            elif v == destination:
                model.add_linear_constraint(flow_balance == demand)
            else:
                model.add_linear_constraint(flow_balance == 0.0)

    for e in range(num_arcs):
        capacity = arcs[e]["capacity"]
        arc_flow = mathopt.LinearExpression()
        for k in range(num_commodities):
            arc_flow += x[(k, e)]
        model.add_linear_constraint(arc_flow <= capacity)

    objective = sum(
        arcs[e]["cost"] * x[(k, e)]
        for k in range(num_commodities)
        for e in range(num_arcs)
    )
    model.minimize(objective)

    return model


import dataclasses


def certify_instance(instance_data) -> CertificationResult:
    """Solve the arc-based MCF and return certification result."""
    if dataclasses.is_dataclass(instance_data):
        instance_data = dataclasses.asdict(instance_data)
    elif isinstance(instance_data, dict):
        def _convert(obj):
            if dataclasses.is_dataclass(obj):
                return dataclasses.asdict(obj)
            elif isinstance(obj, dict):
                return {k: _convert(v) for k, v in obj.items()}
            elif isinstance(obj, list):
                return [_convert(x) for x in obj]
            return obj
        instance_data = _convert(instance_data)

    model = build_arc_mcf_model(instance_data)

    from datetime import timedelta
    params = mathopt.SolveParameters()
    params.lp_algorithm = mathopt.LPAlgorithm.PRIMAL_SIMPLEX
    params.enable_output = False
    params.time_limit = timedelta(seconds=60)

    result = mathopt.solve(model, mathopt.SolverType.GLOP, params=params)

    if result.termination.reason == mathopt.TerminationReason.OPTIMAL:
        return CertificationResult(
            status="optimal",
            objective=result.objective_value(),
            is_feasible=True
        )
    elif result.termination.reason == mathopt.TerminationReason.INFEASIBLE:
        return CertificationResult(
            status="infeasible",
            is_feasible=False
        )
    else:
        return CertificationResult(
            status="unknown",
            is_feasible=False
        )


def certify_file(filepath: str) -> CertificationResult:
    """Load instance from file and certify it."""
    with open(filepath, 'r') as f:
        instance_data = json.load(f)
    return certify_instance(instance_data)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python certify.py <instance.json>")
        sys.exit(1)

    result = certify_file(sys.argv[1])
    print(f"Status: {result.status}")
    if result.objective is not None:
        print(f"Objective: {result.objective}")
    print(f"Feasible: {result.is_feasible}")