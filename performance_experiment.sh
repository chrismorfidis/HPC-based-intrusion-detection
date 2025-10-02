#!/bin/bash
# Performance Experiment for HPC-IDS C System
# Measures CPU overhead, memory usage, and detection latency

# Tunables (override via env: RUNS, BASELINE_TIMEOUT, IDS_TIMEOUT, SYS_DURATION)
RESULTS_DIR="../experiment_results"
CONFIG="config/rigorous_hpc_config.json"
APPS_DIR="../test_apps"
BASELINE_DIR="../baselines"
ALERT_FILE="logs/hpc_ids_alerts.jsonl"

# Create results directory
mkdir -p "$RESULTS_DIR" "$(dirname "$ALERT_FILE")"

RUNS=${RUNS:-3}
BASELINE_TIMEOUT=${BASELINE_TIMEOUT:-10}
IDS_TIMEOUT=${IDS_TIMEOUT:-15}
SYS_DURATION=${SYS_DURATION:-60}

echo "=== HPC-IDS Performance Experiment ==="
echo "Starting at: $(date)"

# Function to measure baseline performance (no monitoring)
measure_baseline() {
    local app_name=$1
    local output_file="${RESULTS_DIR}/baseline_${app_name}.csv"
    
    echo "Measuring baseline performance for $app_name..."
    echo "timestamp,cpu_percent,memory_mb,runtime_ms" > "$output_file"
    
    for run in $(seq 1 $RUNS); do
        echo "  Run $run/$RUNS..."
        start_time=$(date +%s%3N)
        
        # Start resource monitoring in background
        (while true; do
            # Aggregate CPU and memory across all matching PIDs
            pids=$(pgrep -f "^$app_name( |$)" || pgrep -f "$app_name" || true)
            if [ -n "$pids" ]; then
                cpu_total=0
                mem_total=0
                for pid in $pids; do
                    cpu=$(ps -p $pid -o %cpu --no-headers 2>/dev/null | awk '{print $1}' || echo "0")
                    mem=$(ps -p $pid -o rss --no-headers 2>/dev/null | awk '{print $1}' || echo "0")
                    cpu_total=$(echo "$cpu_total + ${cpu:-0}" | bc)
                    mem_total=$(echo "$mem_total + ${mem:-0}" | bc)
                done
                mem_mb=$(echo "scale=2; $mem_total/1024" | bc)
                timestamp=$(date +%s%3N)
                echo "$timestamp,$cpu_total,$mem_mb,0" >> "$output_file"
            fi
            sleep 0.1
        done) &
        monitor_pid=$!
        
        # Run the application repeatedly for the duration
        cd "$APPS_DIR"
        # Start energy monitor in parallel
        cd - > /dev/null
        ./energy_monitor "${RESULTS_DIR}/energy_baseline_${app_name}.csv" "$BASELINE_TIMEOUT" &
        energy_pid=$!
        cd "$APPS_DIR"
        timeout "$BASELINE_TIMEOUT" bash -c "while :; do ./'$app_name' >/dev/null 2>&1; done" 
        
        end_time=$(date +%s%3N)
        runtime=$((end_time - start_time))
        
        # Kill monitoring
        kill $monitor_pid 2>/dev/null
        wait $monitor_pid 2>/dev/null
        kill $energy_pid 2>/dev/null
        wait $energy_pid 2>/dev/null
        
        # Update last entry with actual runtime
        sed -i "$ s/,0$/,$runtime/" "$output_file"
        
        cd - > /dev/null
        sleep 1
    done
}

# Function to measure IDS overhead
measure_ids_overhead() {
    local app_name=$1
    local output_file="${RESULTS_DIR}/ids_overhead_${app_name}.csv"
    
    echo "Measuring IDS overhead for $app_name..."
    echo "timestamp,app_cpu_percent,app_memory_mb,ids_cpu_percent,ids_memory_mb,runtime_ms,detection_latency_ms" > "$output_file"
    
    for run in $(seq 1 $RUNS); do
        echo "  Run $run/$RUNS..."
        start_time=$(date +%s%3N)
        
        # Start the application repeatedly for the duration
        cd "$APPS_DIR"
        timeout "$IDS_TIMEOUT" bash -c "while :; do ./'$app_name' >/dev/null 2>&1; done" &
        app_driver_pid=$!
        cd - > /dev/null
        
        sleep 1  # Let app start
        
        # Start IDS monitoring and energy monitor
        ./hpc_ids --monitor --pid $(pgrep -f "$app_name" | head -1) -d "$IDS_TIMEOUT" &
        ids_pid=$!
        ./energy_monitor "${RESULTS_DIR}/energy_ids_${app_name}.csv" "$IDS_TIMEOUT" &
        energy_pid=$!
        
        # Monitor resources
        (while kill -0 $app_driver_pid 2>/dev/null && kill -0 $ids_pid 2>/dev/null; do
            # Aggregate current app resources across matching PIDs
            pids=$(pgrep -f "^$app_name( |$)" || pgrep -f "$app_name" || true)
            app_cpu_total=0
            app_mem_total=0
            for pid in $pids; do
                cpu=$(ps -p $pid -o %cpu --no-headers 2>/dev/null | awk '{print $1}' || echo "0")
                mem=$(ps -p $pid -o rss --no-headers 2>/dev/null | awk '{print $1}' || echo "0")
                app_cpu_total=$(echo "$app_cpu_total + ${cpu:-0}" | bc)
                app_mem_total=$(echo "$app_mem_total + ${mem:-0}" | bc)
            done
            app_mem_mb=$(echo "scale=2; $app_mem_total/1024" | bc)
            
            # Get IDS resources
            ids_cpu=$(ps -p $ids_pid -o %cpu --no-headers 2>/dev/null || echo "0")
            ids_mem=$(ps -p $ids_pid -o rss --no-headers 2>/dev/null || echo "0")
            ids_mem_mb=$(echo "scale=2; $ids_mem/1024" | bc)
            
            timestamp=$(date +%s%3N)
            echo "$timestamp,$app_cpu_total,$app_mem_mb,$ids_cpu,$ids_mem_mb,0,0" >> "$output_file"
            
            sleep 0.2  # Match sampling interval
        done) &
        monitor_pid=$!
        
        # Wait for app to finish
        wait $app_driver_pid 2>/dev/null
        end_time=$(date +%s%3N)
        runtime=$((end_time - start_time))
        
        # Calculate detection latency from alerts file
        if [ -f "$ALERT_FILE" ]; then
            first_alert=$(jq -r "select(.timestamp*1000 >= $start_time) | .timestamp" "$ALERT_FILE" | head -n 1)
            if [ -n "$first_alert" ] && [ "$first_alert" != "null" ]; then
                detection_latency=$((first_alert * 1000 - start_time))
            else
                detection_latency=0
            fi
        else
            detection_latency=0
        fi
        
        # Clean up
        kill $ids_pid $monitor_pid $energy_pid 2>/dev/null
        wait $ids_pid $monitor_pid $energy_pid 2>/dev/null
        
        # Update last entry with actual values
        sed -i "$ s/,0,0$/,$runtime,$detection_latency/" "$output_file"
        
        sleep 2  # Cooldown
    done
}

# Function to measure system-wide overhead
measure_system_overhead() {
    local output_file="${RESULTS_DIR}/system_overhead.csv"
    
    echo "Measuring system-wide overhead..."
    echo "timestamp,system_cpu_percent,system_memory_mb,ids_cpu_percent,ids_memory_mb" > "$output_file"
    
    echo "  Starting $SYS_DURATION-second system monitoring..."
    
    # Start IDS in system-wide mode
    ./hpc_ids --monitor --duration "$SYS_DURATION" &
    ids_pid=$!
    
    start_time=$(date +%s)
    
    # Monitor system resources
    while kill -0 $ids_pid 2>/dev/null; do
        # Get system CPU usage
        system_cpu=$(top -bn1 | grep "Cpu(s)" | sed "s/.*, *\([0-9.]*\)%* id.*/\1/" | awk '{print 100 - $1}')
        
        # Get system memory usage
        system_mem=$(free -m | awk 'NR==2{printf "%.2f", $3}')
        
        # Get IDS resources
        ids_cpu=$(ps -p $ids_pid -o %cpu --no-headers 2>/dev/null || echo "0")
        ids_mem=$(ps -p $ids_pid -o rss --no-headers 2>/dev/null || echo "0")
        ids_mem_mb=$(echo "scale=2; $ids_mem/1024" | bc)
        
        timestamp=$(date +%s)
        echo "$timestamp,$system_cpu,$system_mem,$ids_cpu,$ids_mem_mb" >> "$output_file"
        
        sleep 1
    done
    
    wait $ids_pid 2>/dev/null
}

# Main experiment execution
echo "Step 1: Building C system..."
make clean && make

if [ $? -ne 0 ]; then
    echo "Error: Failed to build C system"
    exit 1
fi

echo "Step 2: Checking baselines..."
for app in matmul quicksort crypto memory_scan; do
    if [ ! -f "${BASELINE_DIR}/baseline_${app}.json" ]; then
        echo "Warning: Missing baseline for $app, using global baseline"
    fi
done

echo "Step 3: Measuring baseline performance (no monitoring)..."
for app in matmul quicksort crypto memory_scan; do
    if [ -f "${APPS_DIR}/$app" ]; then
        measure_baseline "$app"
    else
        echo "Warning: $app not found in $APPS_DIR"
    fi
done

echo "Step 4: Measuring IDS overhead..."
for app in matmul quicksort crypto memory_scan; do
    if [ -f "${APPS_DIR}/$app" ]; then
        measure_ids_overhead "$app"
    fi
done

echo "Step 5: Measuring system-wide overhead..."
measure_system_overhead

echo "Step 6: Generating summary report..."
python3 -c "
import json
import pandas as pd
import os

results_dir = '../experiment_results'
summary = {}

# Analyze baseline performance
for app in ['matmul', 'quicksort', 'crypto', 'memory_scan']:
    baseline_file = f'{results_dir}/baseline_{app}.csv'
    ids_file = f'{results_dir}/ids_overhead_{app}.csv'
    
    if os.path.exists(baseline_file) and os.path.exists(ids_file):
        baseline_df = pd.read_csv(baseline_file)
        ids_df = pd.read_csv(ids_file)
        # Energy (optional)
        e_base = f'{results_dir}/energy_baseline_{app}.csv'
        e_ids = f'{results_dir}/energy_ids_{app}.csv'
        base_power = None
        ids_power = None
        if os.path.exists(e_base):
            try:
                base_power = pd.read_csv(e_base)['package_power_watts'].mean()
            except Exception:
                base_power = None
        if os.path.exists(e_ids):
            try:
                ids_power = pd.read_csv(e_ids)['package_power_watts'].mean()
            except Exception:
                ids_power = None
        
        # Calculate averages
        baseline_avg_cpu = baseline_df['cpu_percent'].mean()
        baseline_avg_mem = baseline_df['memory_mb'].mean()
        baseline_avg_runtime = baseline_df['runtime_ms'].mean()
        
        ids_avg_app_cpu = ids_df['app_cpu_percent'].mean()
        ids_avg_app_mem = ids_df['app_memory_mb'].mean()
        ids_avg_ids_cpu = ids_df['ids_cpu_percent'].mean()
        ids_avg_ids_mem = ids_df['ids_memory_mb'].mean()
        ids_avg_runtime = ids_df['runtime_ms'].mean()
        ids_avg_latency = ids_df['detection_latency_ms'].mean()
        
        # Calculate overhead
        cpu_overhead = ((ids_avg_app_cpu + ids_avg_ids_cpu) - baseline_avg_cpu) / baseline_avg_cpu * 100
        mem_overhead = ((ids_avg_app_mem + ids_avg_ids_mem) - baseline_avg_mem) / baseline_avg_mem * 100
        runtime_overhead = (ids_avg_runtime - baseline_avg_runtime) / baseline_avg_runtime * 100
        
        summary[app] = {
            'baseline_cpu_percent': round(baseline_avg_cpu, 2),
            'baseline_memory_mb': round(baseline_avg_mem, 2),
            'baseline_runtime_ms': round(baseline_avg_runtime, 2),
            'ids_total_cpu_percent': round(ids_avg_app_cpu + ids_avg_ids_cpu, 2),
            'ids_total_memory_mb': round(ids_avg_app_mem + ids_avg_ids_mem, 2),
            'ids_runtime_ms': round(ids_avg_runtime, 2),
            'ids_component_cpu_percent': round(ids_avg_ids_cpu, 2),
            'ids_component_memory_mb': round(ids_avg_ids_mem, 2),
            'detection_latency_ms': round(ids_avg_latency, 2),
            'cpu_overhead_percent': round(cpu_overhead, 2),
            'memory_overhead_percent': round(mem_overhead, 2),
            'runtime_overhead_percent': round(runtime_overhead, 2),
            'baseline_avg_power_w': round(base_power, 3) if base_power is not None else None,
            'ids_avg_power_w': round(ids_power, 3) if ids_power is not None else None,
            'power_overhead_percent': (round(((ids_power - base_power)/base_power*100), 2) if (base_power and ids_power) else None)
        }

# Save summary
with open(f'{results_dir}/performance_summary.json', 'w') as f:
    json.dump(summary, f, indent=2)

print('Performance Summary:')
print('=' * 80)
for app, metrics in summary.items():
    print(f'{app.upper()}:')
    print(f'  CPU Overhead: {metrics[\"cpu_overhead_percent\"]}%')
    print(f'  Memory Overhead: {metrics[\"memory_overhead_percent\"]}%')
    print(f'  Runtime Overhead: {metrics[\"runtime_overhead_percent\"]}%')
    print(f'  Detection Latency: {metrics[\"detection_latency_ms\"]}ms')
    print(f'  IDS CPU Usage: {metrics[\"ids_component_cpu_percent\"]}%')
    print(f'  IDS Memory Usage: {metrics[\"ids_component_memory_mb\"]}MB')
    if metrics.get('baseline_avg_power_w') is not None:
        print('  Power Overhead: {po}%, baseline={bp}W, with_ids={ip}W'.format(
            po=metrics.get('power_overhead_percent', 'n/a'),
            bp=metrics.get('baseline_avg_power_w'),
            ip=metrics.get('ids_avg_power_w')
        ))
    print()
"

echo "Experiment completed at: $(date)"
echo "Results saved in: $RESULTS_DIR"
