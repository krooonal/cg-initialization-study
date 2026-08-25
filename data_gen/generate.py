#!/usr/bin/env python3
"""
Data generation CLI for column generation initialization study.
"""

import argparse
import json
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from generators.grid import generate_grid, adjust_capacities_for_tightness, GridParams
from generators.random import generate_random, adjust_capacities_for_tightness as adjust_random, RandomParams
from generators.rmf import generate_rmf, adjust_capacities_for_tightness as adjust_rmf, generate_infeasible_rmf, RMFParams
from generators.load_benchmark import load_benchmarks
from schemas import write_instance, Reference
from certify import certify_instance


def generate_feasible_instance(args):
    if args.generator == "grid":
        params = GridParams(
            nx=args.nx,
            ny=args.ny,
            num_commodities=args.num_commodities,
            tightness=args.tightness,
            cost_range=(args.cost_min, args.cost_max),
            demand_range=(args.demand_min, args.demand_max)
        )
        instance = generate_grid(params, args.seed)
        instance = adjust_capacities_for_tightness(instance, params)
    elif args.generator == "random":
        params = RandomParams(
            num_nodes=args.num_nodes,
            num_arcs=args.num_arcs,
            num_commodities=args.num_commodities,
            tightness=args.tightness,
            graph_type=args.graph_type,
            erdos_p=args.erdos_p,
            barabasi_m=args.barabasi_m,
            cost_range=(args.cost_min, args.cost_max),
            demand_range=(args.demand_min, args.demand_max)
        )
        instance = generate_random(params, args.seed)
        instance = adjust_random(instance, params)
    elif args.generator == "rmf":
        params = RMFParams(
            layers=args.layers,
            nodes_per_layer=args.nodes_per_layer,
            num_commodities=args.num_commodities,
            tightness=args.tightness,
            cost_range=(args.cost_min, args.cost_max),
            demand_range=(args.demand_min, args.demand_max)
        )
        instance = generate_rmf(params, args.seed)
        instance = adjust_rmf(instance, params)
    else:
        raise ValueError(f"Unknown generator: {args.generator}")

    instance.stratum = "feasible"
    return instance


def generate_infeasible_instance(args):
    if args.generator == "grid":
        params = GridParams(
            nx=args.nx,
            ny=args.ny,
            num_commodities=args.num_commodities,
            tightness=args.tightness,
            cost_range=(args.cost_min, args.cost_max),
            demand_range=(args.demand_min, args.demand_max)
        )
        instance = generate_grid(params, args.seed)
        instance = adjust_capacities_for_tightness(instance, params)

        num_nodes = instance.graph.num_nodes
        S_size = num_nodes // 2
        S = set(range(S_size))

        total_demand = sum(
            c.demand for c in instance.commodities
            if c.origin in S and c.destination not in S
        )

        for arc in instance.graph.arcs:
            if arc.tail in S and arc.head not in S:
                arc.capacity *= 0.5

        result = certify_instance(instance.__dict__)
        if result.status == "optimal":
            for arc in instance.graph.arcs:
                if arc.tail in S and arc.head not in S:
                    arc.capacity *= 0.1
            result = certify_instance(instance.__dict__)

        instance.reference = Reference(status=result.status)
        instance.stratum = "infeasible"

    elif args.generator == "rmf":
        params = RMFParams(
            layers=args.layers,
            nodes_per_layer=args.nodes_per_layer,
            num_commodities=args.num_commodities,
            tightness=args.tightness,
            cost_range=(args.cost_min, args.cost_max),
            demand_range=(args.demand_min, args.demand_max)
        )
        instance = generate_infeasible_rmf(params, args.seed)
    elif args.generator == "random":
        params = RandomParams(
            num_nodes=args.num_nodes,
            num_arcs=args.num_arcs,
            num_commodities=args.num_commodities,
            tightness=args.tightness,
            graph_type=args.graph_type,
            erdos_p=args.erdos_p,
            barabasi_m=args.barabasi_m,
            cost_range=(args.cost_min, args.cost_max),
            demand_range=(args.demand_min, args.demand_max)
        )
        instance = generate_random(params, args.seed)
        instance = adjust_random(instance, params)

        num_nodes = instance.graph.num_nodes
        S_size = num_nodes // 2
        S = set(range(S_size))

        for arc in instance.graph.arcs:
            if arc.tail in S and arc.head not in S:
                arc.capacity *= 0.1

        result = certify_instance(instance.__dict__)
        if result.status == "optimal":
            for arc in instance.graph.arcs:
                if arc.tail in S and arc.head not in S:
                    arc.capacity *= 0.01
            result = certify_instance(instance.__dict__)

        instance.reference = Reference(status=result.status)
        instance.stratum = "infeasible"
    else:
        raise ValueError(f"Generator {args.generator} does not support infeasible stratum")

    return instance


def main():
    parser = argparse.ArgumentParser(description="Generate MCF instances for CG initialization study")
    parser.add_argument("--stratum", choices=["feasible", "infeasible"], required=True)
    parser.add_argument("--generator", choices=["grid", "random", "rmf", "benchmark"], required=True)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--output-dir", default="instances")

    parser.add_argument("--nx", type=int, default=10)
    parser.add_argument("--ny", type=int, default=10)
    parser.add_argument("--num-nodes", type=int, default=50)
    parser.add_argument("--num-arcs", type=int, default=200)
    parser.add_argument("--num-commodities", type=int, default=25)
    parser.add_argument("--tightness", type=float, default=0.6)
    parser.add_argument("--cost-min", type=float, default=1.0)
    parser.add_argument("--cost-max", type=float, default=10.0)
    parser.add_argument("--demand-min", type=float, default=1.0)
    parser.add_argument("--demand-max", type=float, default=5.0)
    parser.add_argument("--layers", type=int, default=5)
    parser.add_argument("--nodes-per-layer", type=int, default=20)
    parser.add_argument("--graph-type", choices=["erdos_renyi", "barabasi_albert"], default="erdos_renyi")
    parser.add_argument("--erdos-p", type=float, default=0.1)
    parser.add_argument("--barabasi-m", type=int, default=3)

    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)

    if args.stratum == "feasible":
        instance = generate_feasible_instance(args)
    else:
        instance = generate_infeasible_instance(args)

    output_file = os.path.join(args.output_dir, f"{instance.name}.json")
    write_instance(output_file, instance)
    print(f"Generated: {output_file}")
    print(f"  Stratum: {instance.stratum}")
    print(f"  Nodes: {instance.graph.num_nodes}")
    print(f"  Arcs: {len(instance.graph.arcs)}")
    print(f"  Commodities: {len(instance.commodities)}")
    print(f"  Reference: {instance.reference.status}" + (f", obj={instance.reference.objective:.2f}" if instance.reference.objective else ""))


if __name__ == "__main__":
    main()