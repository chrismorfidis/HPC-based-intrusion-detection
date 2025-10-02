#include "hpc_ids.h"

// Forward declarations for simple JSON parser
char* extract_json_string(const char *json, const char *key);
double extract_json_double(const char *json, const char *key);
int extract_json_int(const char *json, const char *key);
bool extract_json_bool(const char *json, const char *key);
int extract_json_string_array(const char *json, const char *key, char results[][64], int max_items);

int load_config(config_t *config, const char *config_file) {
    // Set defaults first
    strcpy(config->app_directory, "./test_apps");
    strcpy(config->baseline_directory, "./baselines");
    strcpy(config->alert_output_file, "hpc_ids_alerts.jsonl");
    config->sampling_interval_ms = 200;
    config->runs_per_app = 10;
    config->min_samples_per_app = 50;
    config->max_runtime_seconds = 60;
    config->core_affinity = 0;
    config->robust_z_threshold_medium = 3.0;
    config->robust_z_threshold_high = 4.0;
    config->robust_z_threshold_critical = 5.0;
    config->alert_cooldown_seconds = 30;
    config->use_robust_statistics = true;
    
    // Default events
    const char *default_events[] = {
        "cycles", "instructions", "branches", "branch-misses",
        "cache-references", "cache-misses", "L1-dcache-loads",
        "L1-dcache-load-misses", "iTLB-loads", "iTLB-load-misses",
        "dTLB-loads", "dTLB-load-misses", "cpu-clock"
    };
    config->num_events = 13;
    for (int i = 0; i < config->num_events; i++) {
        strcpy(config->perf_events[i], default_events[i]);
    }

    // Try to load configuration file
    FILE *file = fopen(config_file, "r");
    if (!file) {
        fprintf(stderr, "Warning: Cannot open config file: %s, using defaults\n", config_file);
        return 0; // Continue with defaults
    }

    // Read entire file into memory
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (length <= 0) {
        fprintf(stderr, "Warning: Config file is empty, using defaults\n");
        fclose(file);
        return 0;
    }
    
    char *json_data = malloc(length + 1);
    if (!json_data) {
        fprintf(stderr, "Error: Cannot allocate memory for config file\n");
        fclose(file);
        return -1;
    }
    
    size_t read_bytes = fread(json_data, 1, length, file);
    json_data[read_bytes] = '\0';
    fclose(file);
    
    printf("Loading configuration from %s...\n", config_file);
    
    // Parse JSON values using simple parser
    char *str_val;
    double double_val;
    int int_val;
    
    if ((str_val = extract_json_string(json_data, "app_directory")) != NULL) {
        strcpy(config->app_directory, str_val);
        printf("  app_directory: %s\n", config->app_directory);
    }
    
    if ((str_val = extract_json_string(json_data, "baseline_directory")) != NULL) {
        strcpy(config->baseline_directory, str_val);
        printf("  baseline_directory: %s\n", config->baseline_directory);
    }
    
    if ((str_val = extract_json_string(json_data, "alert_output_file")) != NULL) {
        strcpy(config->alert_output_file, str_val);
        printf("  alert_output_file: %s\n", config->alert_output_file);
    }
    
    if ((int_val = extract_json_int(json_data, "sampling_interval_ms")) > 0) {
        config->sampling_interval_ms = int_val;
        printf("  sampling_interval_ms: %d\n", config->sampling_interval_ms);
    }
    
    if ((int_val = extract_json_int(json_data, "runs_per_app")) > 0) {
        config->runs_per_app = int_val;
        printf("  runs_per_app: %d\n", config->runs_per_app);
    }
    
    if ((int_val = extract_json_int(json_data, "min_samples_per_app")) > 0) {
        config->min_samples_per_app = int_val;
        printf("  min_samples_per_app: %d\n", config->min_samples_per_app);
    }
    
    if ((int_val = extract_json_int(json_data, "max_runtime_seconds")) > 0) {
        config->max_runtime_seconds = int_val;
        printf("  max_runtime_seconds: %d\n", config->max_runtime_seconds);
    }
    
    if ((int_val = extract_json_int(json_data, "core_affinity")) >= 0) {
        config->core_affinity = int_val;
        printf("  core_affinity: %d\n", config->core_affinity);
    }
    
    if ((double_val = extract_json_double(json_data, "robust_z_threshold_medium")) > 0) {
        config->robust_z_threshold_medium = double_val;
        printf("  robust_z_threshold_medium: %.1f\n", config->robust_z_threshold_medium);
    }
    
    if ((double_val = extract_json_double(json_data, "robust_z_threshold_high")) > 0) {
        config->robust_z_threshold_high = double_val;
        printf("  robust_z_threshold_high: %.1f\n", config->robust_z_threshold_high);
    }
    
    if ((double_val = extract_json_double(json_data, "robust_z_threshold_critical")) > 0) {
        config->robust_z_threshold_critical = double_val;
        printf("  robust_z_threshold_critical: %.1f\n", config->robust_z_threshold_critical);
    }
    
    if ((int_val = extract_json_int(json_data, "alert_cooldown_seconds")) > 0) {
        config->alert_cooldown_seconds = int_val;
        printf("  alert_cooldown_seconds: %d\n", config->alert_cooldown_seconds);
    }
    
    config->use_robust_statistics = extract_json_bool(json_data, "use_robust_statistics");
    printf("  use_robust_statistics: %s\n", config->use_robust_statistics ? "true" : "false");
    
    // Parse perf_events array
    char events[MAX_EVENTS][64];
    int num_events = extract_json_string_array(json_data, "perf_events", events, MAX_EVENTS);
    if (num_events > 0) {
        config->num_events = num_events;
        printf("  perf_events: [");
        for (int i = 0; i < config->num_events; i++) {
            strcpy(config->perf_events[i], events[i]);
            printf("%s%s", events[i], (i < config->num_events - 1) ? ", " : "");
        }
        printf("]\n");
    }
    
    free(json_data);
    printf("Configuration loaded successfully\n");
    return 0;
}

int load_baseline(baseline_t *baseline, const char *baseline_file) {
    FILE *file = fopen(baseline_file, "r");
    if (!file) {
        return -1;
    }
    
    // Read entire file into memory
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *json_content = malloc(file_size + 1);
    if (!json_content) {
        fclose(file);
        return -1;
    }
    
    size_t bytes_read = fread(json_content, 1, file_size, file);
    json_content[bytes_read] = '\0';
    fclose(file);
    
    memset(baseline, 0, sizeof(baseline_t));
    
    // Parse nested JSON structure: baseline_statistics -> feature -> median/mad/etc
    char *baseline_stats = strstr(json_content, "\"baseline_statistics\":");
    if (!baseline_stats) {
        free(json_content);
        return -1;
    }
    
    // Extract each feature manually since the JSON is nested
    // Look for each feature section and extract values
    
    // IPC baseline
    char *ipc_section = strstr(baseline_stats, "\"ipc\":");
    if (ipc_section) {
        baseline->ipc.median = extract_json_double(ipc_section, "median");
        baseline->ipc.mad = extract_json_double(ipc_section, "mad");
        baseline->ipc.min = extract_json_double(ipc_section, "min");
        baseline->ipc.max = extract_json_double(ipc_section, "max");
        baseline->ipc.samples = extract_json_int(ipc_section, "samples");
    }
    
    // Branch miss rate baseline
    char *bmr_section = strstr(baseline_stats, "\"branch_miss_rate\":");
    if (bmr_section) {
        baseline->branch_miss_rate.median = extract_json_double(bmr_section, "median");
        baseline->branch_miss_rate.mad = extract_json_double(bmr_section, "mad");
        baseline->branch_miss_rate.min = extract_json_double(bmr_section, "min");
        baseline->branch_miss_rate.max = extract_json_double(bmr_section, "max");
        baseline->branch_miss_rate.samples = extract_json_int(bmr_section, "samples");
    }
    
    // Cache miss rate baseline
    char *cmr_section = strstr(baseline_stats, "\"cache_miss_rate\":");
    if (cmr_section) {
        baseline->cache_miss_rate.median = extract_json_double(cmr_section, "median");
        baseline->cache_miss_rate.mad = extract_json_double(cmr_section, "mad");
        baseline->cache_miss_rate.min = extract_json_double(cmr_section, "min");
        baseline->cache_miss_rate.max = extract_json_double(cmr_section, "max");
        baseline->cache_miss_rate.samples = extract_json_int(cmr_section, "samples");
    }
    
    // L1D MPKI baseline
    char *l1d_section = strstr(baseline_stats, "\"l1d_mpki\":");
    if (l1d_section) {
        baseline->l1d_mpki.median = extract_json_double(l1d_section, "median");
        baseline->l1d_mpki.mad = extract_json_double(l1d_section, "mad");
        baseline->l1d_mpki.min = extract_json_double(l1d_section, "min");
        baseline->l1d_mpki.max = extract_json_double(l1d_section, "max");
        baseline->l1d_mpki.samples = extract_json_int(l1d_section, "samples");
    }
    
    // iTLB MPKI baseline
    char *itlb_section = strstr(baseline_stats, "\"itlb_mpki\":");
    if (itlb_section) {
        baseline->itlb_mpki.median = extract_json_double(itlb_section, "median");
        baseline->itlb_mpki.mad = extract_json_double(itlb_section, "mad");
        baseline->itlb_mpki.min = extract_json_double(itlb_section, "min");
        baseline->itlb_mpki.max = extract_json_double(itlb_section, "max");
        baseline->itlb_mpki.samples = extract_json_int(itlb_section, "samples");
    }
    
    // dTLB MPKI baseline
    char *dtlb_section = strstr(baseline_stats, "\"dtlb_mpki\":");
    if (dtlb_section) {
        baseline->dtlb_mpki.median = extract_json_double(dtlb_section, "median");
        baseline->dtlb_mpki.mad = extract_json_double(dtlb_section, "mad");
        baseline->dtlb_mpki.min = extract_json_double(dtlb_section, "min");
        baseline->dtlb_mpki.max = extract_json_double(dtlb_section, "max");
        baseline->dtlb_mpki.samples = extract_json_int(dtlb_section, "samples");
    }
    
    free(json_content);
    return 0;
}