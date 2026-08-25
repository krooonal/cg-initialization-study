#!/bin/bash
# Batch runner for CG initialization study

set -e

METHODS=("big_m" "two_phase" "farkas")
KAPPA_VALUES=(10 100 1000 10000)
SEEDS=(35 42 123 456 789)
GENERATORS=("grid" "random" "rmf")

for generator in "${GENERATORS[@]}"; do
    for seed in "${SEEDS[@]}"; do
        for stratum in "feasible" "infeasible"; do
            instance_name="${generator}_s${seed}_t0.6"
            instance_file="instances/${instance_name}.json"

            if [[ ! -f "$instance_file" ]]; then
                echo "Generating $instance_file..."
                python3 data_gen/generate.py --stratum "$stratum" --generator "$generator" --seed "$seed" --tightness 0.6
            fi

            if [[ ! -f "$instance_file" ]]; then
                echo "Failed to generate $instance_file, skipping..."
                continue
            fi

            for method in "${METHODS[@]}"; do
                echo "Running $method on $instance_name..."
                ./cg_study "$instance_file" "$method" --kappa 100 -o "results/${instance_name}.${method}.json"

                for kappa in "${KAPPA_VALUES[@]}"; do
                    if [[ "$method" == "big_m" ]]; then
                        echo "Running $method (kappa=$kappa) on $instance_name..."
                        ./cg_study "$instance_file" "$method" --kappa "$kappa" -o "results/${instance_name}.${method}.M${kappa}.json"
                    fi
                done
            done
        done
    done
done

echo "Batch run complete. Running aggregation..."
python3 scripts/aggregate.py