#include "hpc_ids.h"

const char* get_severity_string(double z_score, const config_t *config) {
    if (fabs(z_score) >= config->robust_z_threshold_critical) {
        return "critical";
    } else if (fabs(z_score) >= config->robust_z_threshold_high) {
        return "high";
    } else if (fabs(z_score) >= config->robust_z_threshold_medium) {
        return "medium";
    }
    return "normal";
}

double get_threshold_for_severity(const char *severity, const config_t *config) {
    if (strcmp(severity, "critical") == 0) {
        return config->robust_z_threshold_critical;
    } else if (strcmp(severity, "high") == 0) {
        return config->robust_z_threshold_high;
    } else if (strcmp(severity, "medium") == 0) {
        return config->robust_z_threshold_medium;
    }
    return 0.0;
}

int check_feature_anomaly(const char *feature_name, double value, 
                         const baseline_stats_t *baseline, const config_t *config,
                         anomaly_alert_t *alert, const char *app_name) {
    
    double z_score = compute_robust_z_score(value, baseline->median, baseline->mad);
    const char *severity = get_severity_string(z_score, config);
    
    if (strcmp(severity, "normal") == 0) {
        return 0; // No anomaly
    }
    
    // Create alert
    strcpy(alert->application_name, app_name ? app_name : "system");
    strcpy(alert->baseline_type, app_name ? "per_app" : "global");
    strcpy(alert->feature, feature_name);
    alert->measured_value = value;
    alert->baseline_median = baseline->median;
    alert->robust_z_score = z_score;
    alert->threshold = get_threshold_for_severity(severity, config);
    strcpy(alert->severity, severity);
    alert->timestamp = time(NULL);
    
    return 1; // Anomaly detected
}

int detect_anomalies(hpc_ids_t *ids, const feature_vector_t *features, const char *app_name) {
    baseline_t *baseline = &ids->global_baseline;
    
    // Check if we have a per-app baseline for this application
    if (app_name) {
        for (int i = 0; i < ids->num_apps; i++) {
            if (strcmp(ids->app_baselines[i].name, app_name) == 0 && 
                ids->app_baselines[i].has_baseline) {
                baseline = &ids->app_baselines[i].baseline;
                break;
            }
        }
    }
    
    // Check cooldown period
    time_t current_time = time(NULL);
    if (current_time - ids->last_alert_time < ids->config.alert_cooldown_seconds) {
        return 0; // Still in cooldown
    }
    
    anomaly_alert_t alert;
    int anomaly_count = 0;
    
    // Check each feature for anomalies
    if (check_feature_anomaly("ipc", features->ipc, &baseline->ipc, 
                             &ids->config, &alert, app_name)) {
        log_alert(ids, &alert);
        anomaly_count++;
    }
    
    if (check_feature_anomaly("branch_miss_rate", features->branch_miss_rate, 
                             &baseline->branch_miss_rate, &ids->config, &alert, app_name)) {
        log_alert(ids, &alert);
        anomaly_count++;
    }
    
    if (check_feature_anomaly("cache_miss_rate", features->cache_miss_rate, 
                             &baseline->cache_miss_rate, &ids->config, &alert, app_name)) {
        log_alert(ids, &alert);
        anomaly_count++;
    }
    
    if (check_feature_anomaly("l1d_mpki", features->l1d_mpki, 
                             &baseline->l1d_mpki, &ids->config, &alert, app_name)) {
        log_alert(ids, &alert);
        anomaly_count++;
    }
    
    if (check_feature_anomaly("itlb_mpki", features->itlb_mpki, 
                             &baseline->itlb_mpki, &ids->config, &alert, app_name)) {
        log_alert(ids, &alert);
        anomaly_count++;
    }
    
    if (check_feature_anomaly("dtlb_mpki", features->dtlb_mpki, 
                             &baseline->dtlb_mpki, &ids->config, &alert, app_name)) {
        log_alert(ids, &alert);
        anomaly_count++;
    }
    
    if (anomaly_count > 0) {
        ids->last_alert_time = current_time;
    }
    
    return anomaly_count;
}

int log_alert(hpc_ids_t *ids, const anomaly_alert_t *alert) {
    if (!ids->alert_file) {
        ids->alert_file = fopen(ids->config.alert_output_file, "a");
        if (!ids->alert_file) {
            fprintf(stderr, "Failed to open alert file: %s\n", ids->config.alert_output_file);
            return -1;
        }
    }
    
    // Write alert as JSON line
    fprintf(ids->alert_file, 
        "{\"timestamp\":%.0f,\"application_name\":\"%s\",\"baseline_type\":\"%s\","
        "\"feature\":\"%s\",\"measured_value\":%.6f,\"baseline_median\":%.6f,"
        "\"robust_z_score\":%.3f,\"threshold\":%.1f,\"severity\":\"%s\"}\n",
        alert->timestamp, alert->application_name, alert->baseline_type,
        alert->feature, alert->measured_value, alert->baseline_median,
        alert->robust_z_score, alert->threshold, alert->severity);
    
    fflush(ids->alert_file);
    
    // Also log to stderr for real-time monitoring
    fprintf(stderr, "[%s] %s anomaly in %s: %s=%.6f (baseline=%.6f, z=%.3f)\n",
            alert->severity, alert->baseline_type, alert->application_name,
            alert->feature, alert->measured_value, alert->baseline_median,
            alert->robust_z_score);
    
    return 0;
}