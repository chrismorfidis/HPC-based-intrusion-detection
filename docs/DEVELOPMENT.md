# Development Guide

## Building from Source

```bash
# Clone repository
git clone <your-repo-url>
cd hpc-ids-c-system

# Build all components
make all
```

## Code Structure

- `src/core.c` - Main HPC monitoring engine
- `src/detection.c` - Anomaly detection algorithms
- `src/perf_integration.c` - Linux perf subsystem integration
- `include/hpc_ids.h` - Main header file

## Adding New Features

1. Create feature branch
2. Implement changes
3. Add tests if applicable
4. Update documentation
5. Submit pull request

## Performance Testing

Use the provided scripts:

```bash
./scripts/performance_experiment.sh
./scripts/detection_experiment.sh
```
