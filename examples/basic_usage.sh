#!/bin/bash
# Basic HPC-IDS Usage Example

echo "HPC-IDS Basic Usage Example"
echo "================================"

# 1. Build the system
echo "Building HPC-IDS..."
make clean && make all

# 2. Check system requirements
echo "Checking system requirements..."
if [ ! -f /proc/sys/kernel/perf_event_paranoid ]; then
    echo "ERROR: Perf subsystem not available"
    exit 1
fi

current_paranoid=$(cat /proc/sys/kernel/perf_event_paranoid)
echo "Current perf_event_paranoid: $current_paranoid"

if [ $current_paranoid -gt 1 ]; then
    echo "WARNING: Consider setting perf_event_paranoid to 1 for better access:"
    echo "   sudo sysctl kernel.perf_event_paranoid=1"
fi

# 3. Collect baseline (if test apps available)
if [ -d "tests" ]; then
    echo "Collecting baseline for test applications..."
    ./baseline_collector --app tests/matmul --runs 5 --output baselines/matmul_baseline.json
fi

# 4. Start monitoring
echo "Starting HPC-IDS monitoring..."
echo "Press Ctrl+C to stop"
./hpc_ids --config config/default.json

echo "Example completed"
