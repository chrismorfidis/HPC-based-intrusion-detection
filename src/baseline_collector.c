#include "hpc_ids.h"

int compute_baseline_from_features(baseline_t *baseline, feature_vector_t *features, int count);
int save_baseline(const baseline_t *baseline, const char *filename, const char *app_name,
                 const config_t *config, int sample_count);
int build_perf_command(const config_t *config, const char *target, char *cmd_buffer, size_t buffer_size);

int collect_baseline(hpc_ids_t *ids, const char *app_name) {
    char app_path[MAX_PATH_LEN];
    char cmd[1024];
    hpc_measurement_t measurements[MAX_SAMPLES];
    int measurement_count;
    feature_vector_t feature_samples[MAX_SAMPLES];
    int feature_count = 0;
    
    snprintf(app_path, sizeof(app_path), "%s/%s", ids->config.app_directory, app_name);
    
    if (access(app_path, X_OK) != 0) {
        fprintf(stderr, "Application not found: %s\n", app_path);
        return -1;
    }
    
    printf("Collecting baseline for %s...\n", app_name);
    
    for (int run = 0; run < ids->config.runs_per_app; run++) {
        printf("Run %d/%d for %s\n", run + 1, ids->config.runs_per_app, app_name);
        
        if (build_perf_command(&ids->config, app_path, cmd, sizeof(cmd)) != 0) {
            fprintf(stderr, "Failed to build perf command\n");
            continue;
        }
        
        char timed_cmd[1200];
        snprintf(timed_cmd, sizeof(timed_cmd), "timeout %d %s", 
                ids->config.max_runtime_seconds, cmd);
        
        if (execute_perf_command(timed_cmd, measurements, &measurement_count, 
                                ids->config.max_runtime_seconds) != 0) {
            fprintf(stderr, "Failed to execute perf command for run %d\n", run + 1);
            continue;
        }
        
        // Process measurements into features
        for (int i = 0; i < measurement_count && feature_count < MAX_SAMPLES; 
             i += ids->config.num_events) {
            
            int interval_count = 0;
            for (int j = 0; j < ids->config.num_events && (i + j) < measurement_count; j++) {
                interval_count++;
            }
            
            if (interval_count == ids->config.num_events) {
                if (engineer_features(&measurements[i], interval_count, 
                                    &feature_samples[feature_count]) == 0) {
                    feature_count++;
                }
            }
        }
        
        printf("Run %d collected %d total feature samples\n", run + 1, feature_count);
    }
    
    if (feature_count < ids->config.min_samples_per_app) {
        fprintf(stderr, "Insufficient samples for %s: %d < %d\n", 
                app_name, feature_count, ids->config.min_samples_per_app);
        return -1;
    }
    
    printf("Collected %d total samples for %s\n", feature_count, app_name);
    
    // Compute baseline statistics
    baseline_t baseline;
    if (compute_baseline_from_features(&baseline, feature_samples, feature_count) != 0) {
        fprintf(stderr, "Failed to compute baseline statistics\n");
        return -1;
    }
    
    // Save baseline to file
    char baseline_file[MAX_PATH_LEN];
    snprintf(baseline_file, sizeof(baseline_file), "%s/baseline_%s.json", 
             ids->config.baseline_directory, app_name);
    
    if (save_baseline(&baseline, baseline_file, app_name, &ids->config, 
                     feature_count) != 0) {
        fprintf(stderr, "Failed to save baseline to %s\n", baseline_file);
        return -1;
    }
    
    printf("Baseline saved to %s\n", baseline_file);
    return 0;
}

int compute_baseline_from_features(baseline_t *baseline, feature_vector_t *features, int count) {
    double *values = malloc(count * sizeof(double));
    
    // Extract IPC values
    for (int i = 0; i < count; i++) {
        values[i] = features[i].ipc;
    }
    compute_baseline_stats(&baseline->ipc, values, count);
    
    // Extract branch miss rate values
    for (int i = 0; i < count; i++) {
        values[i] = features[i].branch_miss_rate;
    }
    compute_baseline_stats(&baseline->branch_miss_rate, values, count);
    
    // Extract cache miss rate values
    for (int i = 0; i < count; i++) {
        values[i] = features[i].cache_miss_rate;
    }
    compute_baseline_stats(&baseline->cache_miss_rate, values, count);
    
    // Extract L1D MPKI values
    for (int i = 0; i < count; i++) {
        values[i] = features[i].l1d_mpki;
    }
    compute_baseline_stats(&baseline->l1d_mpki, values, count);
    
    // Extract iTLB MPKI values
    for (int i = 0; i < count; i++) {
        values[i] = features[i].itlb_mpki;
    }
    compute_baseline_stats(&baseline->itlb_mpki, values, count);
    
    // Extract dTLB MPKI values
    for (int i = 0; i < count; i++) {
        values[i] = features[i].dtlb_mpki;
    }
    compute_baseline_stats(&baseline->dtlb_mpki, values, count);
    
    free(values);
    return 0;
}

int save_baseline(const baseline_t *baseline, const char *filename, const char *app_name,
                 const config_t *config, int sample_count) {
    FILE *file = fopen(filename, "w");
    if (!file) {
        return -1;
    }
    
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", gmtime(&now));
    
    // Write JSON baseline file
    fprintf(file, "{\n");
    fprintf(file, "  \"metadata\": {\n");
    fprintf(file, "    \"application_name\": \"%s\",\n", app_name);
    fprintf(file, "    \"collection_timestamp\": \"%s\",\n", timestamp);
    fprintf(file, "    \"runs_executed\": %d,\n", config->runs_per_app);
    fprintf(file, "    \"samples_collected\": %d,\n", sample_count);
    fprintf(file, "    \"events\": [");
    
    for (int i = 0; i < config->num_events; i++) {
        fprintf(file, "\"%s\"", config->perf_events[i]);
        if (i < config->num_events - 1) fprintf(file, ", ");
    }
    
    fprintf(file, "],\n");
    fprintf(file, "    \"config\": {\n");
    fprintf(file, "      \"sampling_interval_ms\": %d,\n", config->sampling_interval_ms);
    fprintf(file, "      \"core_affinity\": %d\n", config->core_affinity);
    fprintf(file, "    }\n");
    fprintf(file, "  },\n");
    
    fprintf(file, "  \"baseline_statistics\": {\n");
    
    fprintf(file, "    \"ipc\": {\n");
    fprintf(file, "      \"median\": %.15f,\n", baseline->ipc.median);
    fprintf(file, "      \"mad\": %.15f,\n", baseline->ipc.mad);
    fprintf(file, "      \"method\": \"robust_median_mad\",\n");
    fprintf(file, "      \"min\": %.15f,\n", baseline->ipc.min);
    fprintf(file, "      \"max\": %.15f,\n", baseline->ipc.max);
    fprintf(file, "      \"samples\": %d\n", baseline->ipc.samples);
    fprintf(file, "    },\n");
    
    fprintf(file, "    \"branch_miss_rate\": {\n");
    fprintf(file, "      \"median\": %.15f,\n", baseline->branch_miss_rate.median);
    fprintf(file, "      \"mad\": %.15f,\n", baseline->branch_miss_rate.mad);
    fprintf(file, "      \"method\": \"robust_median_mad\",\n");
    fprintf(file, "      \"min\": %.15f,\n", baseline->branch_miss_rate.min);
    fprintf(file, "      \"max\": %.15f,\n", baseline->branch_miss_rate.max);
    fprintf(file, "      \"samples\": %d\n", baseline->branch_miss_rate.samples);
    fprintf(file, "    },\n");
    
    fprintf(file, "    \"cache_miss_rate\": {\n");
    fprintf(file, "      \"median\": %.15f,\n", baseline->cache_miss_rate.median);
    fprintf(file, "      \"mad\": %.15f,\n", baseline->cache_miss_rate.mad);
    fprintf(file, "      \"method\": \"robust_median_mad\",\n");
    fprintf(file, "      \"min\": %.15f,\n", baseline->cache_miss_rate.min);
    fprintf(file, "      \"max\": %.15f,\n", baseline->cache_miss_rate.max);
    fprintf(file, "      \"samples\": %d\n", baseline->cache_miss_rate.samples);
    fprintf(file, "    },\n");
    
    fprintf(file, "    \"l1d_mpki\": {\n");
    fprintf(file, "      \"median\": %.15f,\n", baseline->l1d_mpki.median);
    fprintf(file, "      \"mad\": %.15f,\n", baseline->l1d_mpki.mad);
    fprintf(file, "      \"method\": \"robust_median_mad\",\n");
    fprintf(file, "      \"min\": %.15f,\n", baseline->l1d_mpki.min);
    fprintf(file, "      \"max\": %.15f,\n", baseline->l1d_mpki.max);
    fprintf(file, "      \"samples\": %d\n", baseline->l1d_mpki.samples);
    fprintf(file, "    },\n");
    
    fprintf(file, "    \"itlb_mpki\": {\n");
    fprintf(file, "      \"median\": %.15f,\n", baseline->itlb_mpki.median);
    fprintf(file, "      \"mad\": %.15f,\n", baseline->itlb_mpki.mad);
    fprintf(file, "      \"method\": \"robust_median_mad\",\n");
    fprintf(file, "      \"min\": %.15f,\n", baseline->itlb_mpki.min);
    fprintf(file, "      \"max\": %.15f,\n", baseline->itlb_mpki.max);
    fprintf(file, "      \"samples\": %d\n", baseline->itlb_mpki.samples);
    fprintf(file, "    },\n");
    
    fprintf(file, "    \"dtlb_mpki\": {\n");
    fprintf(file, "      \"median\": %.15f,\n", baseline->dtlb_mpki.median);
    fprintf(file, "      \"mad\": %.15f,\n", baseline->dtlb_mpki.mad);
    fprintf(file, "      \"method\": \"robust_median_mad\",\n");
    fprintf(file, "      \"min\": %.15f,\n", baseline->dtlb_mpki.min);
    fprintf(file, "      \"max\": %.15f,\n", baseline->dtlb_mpki.max);
    fprintf(file, "      \"samples\": %d\n", baseline->dtlb_mpki.samples);
    fprintf(file, "    }\n");
    
    fprintf(file, "  }\n");
    fprintf(file, "}\n");
    
    fclose(file);
    return 0;
}

int collect_all_baselines(hpc_ids_t *ids) {
    char apps[MAX_APPS][128];
    int app_count = get_available_apps(ids->config.app_directory, apps, MAX_APPS);
    
    if (app_count == 0) {
        fprintf(stderr, "No applications found in %s\n", ids->config.app_directory);
        return -1;
    }
    
    printf("Found %d applications\n", app_count);
    
    int success_count = 0;
    for (int i = 0; i < app_count; i++) {
        printf("\n%s\n", "==================================================");
        printf("Collecting baseline for: %s\n", apps[i]);
        printf("%s\n", "==================================================");
        
        if (collect_baseline(ids, apps[i]) == 0) {
            success_count++;
            printf("Successfully collected baseline for %s\n", apps[i]);
        } else {
            printf("Failed to collect baseline for %s\n", apps[i]);
        }
    }
    
    printf("\n%s\n", "==================================================");
    printf("Baseline collection completed\n");
    printf("Success: %d/%d applications\n", success_count, app_count);
    printf("%s\n", "==================================================");
    
    return success_count;
}