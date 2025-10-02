# HPC-IDS C Implementation

Hardware Performance Counter based Intrusion Detection System implemented in C for embedded deployment.

## Features

- **Real-time monitoring** of hardware performance counters
- **Per-application and global baselines** for anomaly detection  
- **Robust statistical analysis** using median + MAD
- **Multi-threshold alerting** (medium/high/critical)
- **Embedded-ready** C implementation without external dependencies
- **Compatible** with Python version functionality

## Architecture

```
c_system/
├── hpc_ids.h              # Main header with data structures
├── core.c                 # Core system functionality
├── config.c               # Configuration management
├── statistics.c           # Statistical analysis engine
├── perf_integration.c     # Linux perf interface
├── detection.c            # Anomaly detection algorithms
├── baseline_collector.c   # Baseline collection system
├── hpc_ids_main.c         # Main monitoring application
├── baseline_collector_main.c # Baseline collection tool
└── Makefile              # Build system
```

## Build Requirements

- GCC compiler
- Linux perf tools
- Standard C libraries (libc, libm)

## Quick Start

```bash
# Build the system
make all

# System setup (run once)
make setup

# Collect baselines
./baseline_collector

# Monitor system-wide for 30 seconds
./hpc_ids --monitor --duration 30

# Monitor specific application
./hpc_ids --monitor --app-name matmul

# Monitor specific PID
./hpc_ids --monitor --pid 1234
```

## Hardware Performance Counters Monitored

- **Core Performance**: `cycles`, `instructions`, `cpu-clock`
- **Control Flow**: `branches`, `branch-misses` (ROP/JOP detection)
- **Memory Hierarchy**: `cache-references`, `cache-misses`, `L1-dcache-loads/misses`  
- **Address Translation**: `iTLB-loads/misses`, `dTLB-loads/misses`

## Statistical Features Computed

- **IPC** (Instructions Per Cycle)
- **Branch Miss Rate** (malware often has irregular control flow)
- **Cache Miss Rate** (unusual memory access patterns)
- **L1D MPKI** (L1 Data cache Misses Per Kilo Instructions)
- **iTLB MPKI** (Instruction TLB misses)
- **dTLB MPKI** (Data TLB misses)

## Detection Algorithm

1. **Feature Engineering**: Raw HPC data -> normalized features
2. **Baseline Comparison**: Current features vs. established baselines
3. **Robust Z-Score**: `z = (value - median) / MAD`
4. **Multi-threshold Alerting**:
   - Medium: |z| ≥ 3.0
   - High: |z| ≥ 4.0  
   - Critical: |z| ≥ 5.0

## Embedded Deployment

This C implementation is designed for embedded systems:

- **Minimal Dependencies**: Only standard C libraries
- **Low Memory Footprint**: Static data structures
- **Real-time Capable**: Direct perf interface
- **ARM Compatible**: Standard POSIX/Linux APIs

## Comparison with Python Version

| Feature | Python | C |
|---------|--------|---|
| Functionality | Full | Full |
| Performance | ~100ms overhead | ~1ms overhead |
| Memory Usage | ~50MB | ~1MB |
| Dependencies | NumPy, JSON | libc, libm |
| Deployment | Desktop/Server | Embedded/IoT |

## Usage Examples

```bash
# Help
./hpc_ids --help
./baseline_collector --help

# Baseline Collection
./baseline_collector --app matmul --runs 15
./baseline_collector  # All applications

# Monitoring
./hpc_ids --monitor --duration 60
./hpc_ids --monitor --pid $(pgrep firefox)
./hpc_ids --monitor --app-name crypto

# Combined Pipeline
./baseline_collector && ./hpc_ids --monitor --duration 30
```

## Output

Alerts are logged to `hpc_ids_alerts.jsonl` in JSONL format:

```json
{
  "timestamp": 1694123456.789,
  "application_name": "suspicious_app", 
  "baseline_type": "per_app",
  "feature": "branch_miss_rate",
  "measured_value": 0.089,
  "baseline_median": 0.047,
  "robust_z_score": 4.2,
  "threshold": 4.0,
  "severity": "high"
}
```

## System Requirements

- **OS**: Linux with perf support
- **Permissions**: `perf_event_paranoid ≤ 1`
- **CPU Governor**: `performance` (recommended)
- **Architecture**: AMD Zen+ (tested), Intel compatible

## License

Research/Educational Use