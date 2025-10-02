#include "hpc_ids.h"

int build_perf_command(const config_t *config, const char *target, char *cmd_buffer, size_t buffer_size);

int hpc_ids_init(hpc_ids_t *ids, const char *config_file) {
    memset(ids, 0, sizeof(hpc_ids_t));
    
    // Load configuration
    if (load_config(&ids->config, config_file) != 0) {
        fprintf(stderr, "Failed to load configuration from %s\n", config_file);
        return -1;
    }
    
    // Load global baseline from baseline directory
    char global_baseline_path[MAX_PATH_LEN];
    snprintf(global_baseline_path, sizeof(global_baseline_path), "%s/rigorous_baseline.json", 
             ids->config.baseline_directory);
    if (load_baseline(&ids->global_baseline, global_baseline_path) != 0) {
        fprintf(stderr, "Warning: Failed to load global baseline\n");
    }
    
    // Load per-app baselines
    load_app_baselines(ids);
    
    ids->last_alert_time = 0;
    
    printf("HPC-IDS initialized with %d events and %d app baselines\n", 
           ids->config.num_events, ids->num_apps);
    
    return 0;
}

void hpc_ids_cleanup(hpc_ids_t *ids) {
    if (ids->alert_file) {
        fclose(ids->alert_file);
        ids->alert_file = NULL;
    }
}

int load_app_baselines(hpc_ids_t *ids) {
    DIR *dir;
    struct dirent *entry;
    char baseline_path[MAX_PATH_LEN];
    
    dir = opendir(ids->config.baseline_directory);
    if (!dir) {
        fprintf(stderr, "Cannot open baseline directory: %s\n", ids->config.baseline_directory);
        return -1;
    }
    
    ids->num_apps = 0;
    
    while ((entry = readdir(dir)) != NULL && ids->num_apps < MAX_APPS) {
        if (strncmp(entry->d_name, "baseline_", 9) == 0 && 
            strstr(entry->d_name, ".json")) {
            
            // Extract app name from filename
            char *app_name = entry->d_name + 9; // Skip "baseline_"
            char *dot = strrchr(app_name, '.');
            if (dot) *dot = '\0';
            
            strcpy(ids->app_baselines[ids->num_apps].name, app_name);
            
            // Load baseline
            snprintf(baseline_path, sizeof(baseline_path), "%s/%s", 
                    ids->config.baseline_directory, entry->d_name);
            
            if (load_baseline(&ids->app_baselines[ids->num_apps].baseline, baseline_path) == 0) {
                ids->app_baselines[ids->num_apps].has_baseline = true;
                printf("Loaded baseline for app: %s\n", app_name);
                ids->num_apps++;
            } else {
                fprintf(stderr, "Failed to load baseline for app: %s\n", app_name);
            }
            
            if (dot) *dot = '.'; // Restore dot
        }
    }
    
    closedir(dir);
    return ids->num_apps;
}

int get_available_apps(const char *app_dir, char apps[][128], int max_apps) {
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    
    dir = opendir(app_dir);
    if (!dir) {
        return 0;
    }
    
    while ((entry = readdir(dir)) != NULL && count < max_apps) {
        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", app_dir, entry->d_name);
        
        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0 && S_ISREG(file_stat.st_mode) && access(full_path, X_OK) == 0) {
            strcpy(apps[count], entry->d_name);
            count++;
        }
    }
    
    closedir(dir);
    return count;
}

int monitor_system(hpc_ids_t *ids, int duration_seconds) {
    char cmd[1024];
    hpc_measurement_t measurements[MAX_SAMPLES];
    int measurement_count;
    
    printf("Starting system-wide monitoring for %d seconds...\n", duration_seconds);
    
    if (build_perf_command(&ids->config, NULL, cmd, sizeof(cmd)) != 0) {
        fprintf(stderr, "Failed to build perf command\n");
        return -1;
    }
    
    // Add timeout to command
    char timed_cmd[1200];
    snprintf(timed_cmd, sizeof(timed_cmd), "timeout %d %s", duration_seconds, cmd);
    
    if (execute_perf_command(timed_cmd, measurements, &measurement_count, duration_seconds) != 0) {
        fprintf(stderr, "Failed to execute perf command\n");
        return -1;
    }
    
    printf("Collected %d measurements\n", measurement_count);
    
    if (measurement_count == 0) {
        fprintf(stderr, "No measurements collected - check perf permissions or events\n");
        return -1;
    }
    
    // Group measurements by timestamp (intervals)
    typedef struct {
        double timestamp;
        hpc_measurement_t measurements[MAX_EVENTS];
        int count;
    } interval_data_t;
    
    interval_data_t intervals[1000]; // Support up to 1000 intervals
    int num_intervals = 0;
    
    // Group measurements by perf_time
    for (int i = 0; i < measurement_count; i++) {
        double timestamp = measurements[i].perf_time;
        
        // Find or create interval for this timestamp
        int interval_idx = -1;
        for (int j = 0; j < num_intervals; j++) {
            if (fabs(intervals[j].timestamp - timestamp) < 0.001) { // 1ms tolerance
                interval_idx = j;
                break;
            }
        }
        
        if (interval_idx == -1 && num_intervals < 1000) {
            // Create new interval
            interval_idx = num_intervals++;
            intervals[interval_idx].timestamp = timestamp;
            intervals[interval_idx].count = 0;
        }
        
        if (interval_idx >= 0 && intervals[interval_idx].count < MAX_EVENTS) {
            intervals[interval_idx].measurements[intervals[interval_idx].count++] = measurements[i];
        }
    }
    
    printf("Grouped into %d intervals\n", num_intervals);
    
    // Process complete intervals
    int processed_intervals = 0;
    for (int i = 0; i < num_intervals; i++) {
        if (intervals[i].count >= 3) { // At least 3 counters for basic features
            feature_vector_t features;
            if (engineer_features(intervals[i].measurements, intervals[i].count, &features) == 0) {
                detect_anomalies(ids, &features, NULL);
                processed_intervals++;
            }
        }
    }
    
    printf("Processed %d complete intervals\n", processed_intervals);
    
    return 0;
}

int monitor_pid(hpc_ids_t *ids, pid_t pid, int duration_seconds) {
    char cmd[1024];
    char target[32];
    hpc_measurement_t measurements[MAX_SAMPLES];
    int measurement_count;
    
    char *app_name = get_app_name_from_pid(pid);
    printf("Monitoring PID %d (%s) for %d seconds...\n", pid, app_name, duration_seconds);
    
    snprintf(target, sizeof(target), "pid:%d", pid);
    
    if (build_perf_command(&ids->config, target, cmd, sizeof(cmd)) != 0) {
        fprintf(stderr, "Failed to build perf command\n");
        return -1;
    }
    
    char timed_cmd[1200];
    snprintf(timed_cmd, sizeof(timed_cmd), "timeout %d %s", duration_seconds, cmd);
    
    if (execute_perf_command(timed_cmd, measurements, &measurement_count, duration_seconds) != 0) {
        fprintf(stderr, "Failed to execute perf command\n");
        return -1;
    }
    
    printf("Collected %d measurements for %s\n", measurement_count, app_name);
    
    // Process measurements
    feature_vector_t features;
    
    for (int i = 0; i < measurement_count; i += ids->config.num_events) {
        int interval_count = 0;
        for (int j = 0; j < ids->config.num_events && (i + j) < measurement_count; j++) {
            interval_count++;
        }
        
        if (interval_count == ids->config.num_events) {
            if (engineer_features(&measurements[i], interval_count, &features) == 0) {
                detect_anomalies(ids, &features, app_name);
            }
        }
    }
    
    return 0;
}

int monitor_app(hpc_ids_t *ids, const char *app_name, int duration_seconds) {
    char cmd[1024];
    char app_path[MAX_PATH_LEN];
    hpc_measurement_t measurements[MAX_SAMPLES];
    int measurement_count;
    
    snprintf(app_path, sizeof(app_path), "%s/%s", ids->config.app_directory, app_name);
    
    if (access(app_path, X_OK) != 0) {
        fprintf(stderr, "Application not found or not executable: %s\n", app_path);
        return -1;
    }
    
    printf("Monitoring application %s for %d seconds...\n", app_name, duration_seconds);
    
    if (build_perf_command(&ids->config, app_path, cmd, sizeof(cmd)) != 0) {
        fprintf(stderr, "Failed to build perf command\n");
        return -1;
    }
    
    char timed_cmd[1200];
    snprintf(timed_cmd, sizeof(timed_cmd), "timeout %d %s", duration_seconds, cmd);
    
    if (execute_perf_command(timed_cmd, measurements, &measurement_count, duration_seconds) != 0) {
        fprintf(stderr, "Failed to execute perf command\n");
        return -1;
    }
    
    printf("Collected %d measurements for %s\n", measurement_count, app_name);
    
    // Process measurements
    feature_vector_t features;
    
    for (int i = 0; i < measurement_count; i += ids->config.num_events) {
        int interval_count = 0;
        for (int j = 0; j < ids->config.num_events && (i + j) < measurement_count; j++) {
            interval_count++;
        }
        
        if (interval_count == ids->config.num_events) {
            if (engineer_features(&measurements[i], interval_count, &features) == 0) {
                detect_anomalies(ids, &features, app_name);
            }
        }
    }
    
    return 0;
}