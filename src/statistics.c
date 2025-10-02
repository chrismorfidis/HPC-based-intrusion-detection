#include "hpc_ids.h"

static int compare_doubles(const void *a, const void *b) {
    double diff = *(double*)a - *(double*)b;
    return (diff > 0) - (diff < 0);
}

double compute_median(double *values, int count) {
    if (count == 0) return 0.0;
    
    double *sorted = malloc(count * sizeof(double));
    memcpy(sorted, values, count * sizeof(double));
    qsort(sorted, count, sizeof(double), compare_doubles);
    
    double median;
    if (count % 2 == 0) {
        median = (sorted[count/2 - 1] + sorted[count/2]) / 2.0;
    } else {
        median = sorted[count/2];
    }
    
    free(sorted);
    return median;
}

double compute_mad(double *values, int count, double median) {
    if (count == 0) return 0.0;
    
    double *deviations = malloc(count * sizeof(double));
    for (int i = 0; i < count; i++) {
        deviations[i] = fabs(values[i] - median);
    }
    
    double mad = compute_median(deviations, count);
    free(deviations);
    return mad;
}

double compute_robust_z_score(double value, double median, double mad) {
    const double epsilon = 1e-9;
    double adjusted_mad = (mad < epsilon) ? epsilon : mad;
    return (value - median) / adjusted_mad;
}

int compute_baseline_stats(baseline_stats_t *stats, double *values, int count) {
    if (count == 0) {
        memset(stats, 0, sizeof(baseline_stats_t));
        return -1;
    }
    
    stats->median = compute_median(values, count);
    stats->mad = compute_mad(values, count, stats->median);
    stats->samples = count;
    
    // Find min and max
    stats->min = values[0];
    stats->max = values[0];
    for (int i = 1; i < count; i++) {
        if (values[i] < stats->min) stats->min = values[i];
        if (values[i] > stats->max) stats->max = values[i];
    }
    
    return 0;
}

int engineer_features(hpc_measurement_t *measurements, int count, feature_vector_t *features) {
    if (!measurements || !features || count <= 0) return -1;
    
    // Initialize all counters to 0
    uint64_t cycles = 0, instructions = 0, branches = 0, branch_misses = 0;
    uint64_t cache_refs = 0, cache_misses = 0, l1d_misses = 0;
    uint64_t itlb_misses = 0, dtlb_misses = 0;
    
    int counters_found = 0;
    
    // Extract counters from measurements
    for (int i = 0; i < count; i++) {
        const char *counter = measurements[i].counter;
        uint64_t value = measurements[i].value;
        
        // Debug: print first few counter names (disabled for production)
        #ifdef DEBUG_PARSING
        if (i < 5) {
            fprintf(stderr, "  measurement[%d]: counter='%s', value=%lu\n", i, counter, value);
        }
        #endif
        
        if (strcmp(counter, "cycles") == 0) {
            cycles = value;
            counters_found++;
        } else if (strcmp(counter, "instructions") == 0) {
            instructions = value;
            counters_found++;
        } else if (strcmp(counter, "branches") == 0) {
            branches = value;
            counters_found++;
        } else if (strcmp(counter, "branch-misses") == 0) {
            branch_misses = value;
            counters_found++;
        } else if (strcmp(counter, "cache-references") == 0) {
            cache_refs = value;
            counters_found++;
        } else if (strcmp(counter, "cache-misses") == 0) {
            cache_misses = value;
            counters_found++;
        } else if (strcmp(counter, "L1-dcache-load-misses") == 0) {
            l1d_misses = value;
            counters_found++;
        } else if (strcmp(counter, "iTLB-load-misses") == 0) {
            itlb_misses = value;
            counters_found++;
        } else if (strcmp(counter, "dTLB-load-misses") == 0) {
            dtlb_misses = value;
            counters_found++;
        }
    }
    
    // Debug: show what counters we found
    fprintf(stderr, "Feature engineering: found %d counters out of %d measurements\n", 
            counters_found, count);
    fprintf(stderr, "  cycles=%lu, instructions=%lu, branches=%lu\n", 
            cycles, instructions, branches);
    
    // Check minimum required counters
    if (cycles == 0 || instructions == 0) {
        fprintf(stderr, "Error: Missing essential counters (cycles=%lu, instructions=%lu)\n", 
                cycles, instructions);
        return -1;
    }
    
    // Initialize feature vector
    memset(features, 0, sizeof(feature_vector_t));
    features->wall_time = (double)time(NULL);
    
    // Compute IPC (Instructions Per Cycle)
    features->ipc = (double)instructions / (double)cycles;
    
    // Compute branch miss rate (avoid division by zero)
    if (branches > 0) {
        features->branch_miss_rate = (double)branch_misses / (double)branches;
    } else {
        features->branch_miss_rate = 0.0;
    }
    
    // Compute cache miss rate
    if (cache_refs > 0) {
        features->cache_miss_rate = (double)cache_misses / (double)cache_refs;
    } else {
        features->cache_miss_rate = 0.0;
    }
    
    // Compute MPKI (Misses Per Kilo Instructions) metrics
    double instructions_k = (double)instructions / 1000.0;
    if (instructions_k > 0) {
        features->l1d_mpki = (double)l1d_misses / instructions_k;
        features->itlb_mpki = (double)itlb_misses / instructions_k;
        features->dtlb_mpki = (double)dtlb_misses / instructions_k;
    } else {
        features->l1d_mpki = 0.0;
        features->itlb_mpki = 0.0;
        features->dtlb_mpki = 0.0;
    }
    
    // Debug output
    fprintf(stderr, "Computed features: IPC=%.3f, BMR=%.4f, CMR=%.4f, L1D=%.2f, iTLB=%.2f, dTLB=%.2f\n",
            features->ipc, features->branch_miss_rate, features->cache_miss_rate,
            features->l1d_mpki, features->itlb_mpki, features->dtlb_mpki);
    
    return 0;
}