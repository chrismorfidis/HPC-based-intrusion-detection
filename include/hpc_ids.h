#ifndef HPC_IDS_H
#define HPC_IDS_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>

#define MAX_EVENTS 16
#define MAX_APPS 64
#define MAX_SAMPLES 10000
#define MAX_PATH_LEN 256
#define MAX_LINE_LEN 1024
#define SAMPLING_INTERVAL_MS 200

typedef struct {
    double wall_time;
    double perf_time;
    char counter[64];
    uint64_t value;
    int duration_ms;
} hpc_measurement_t;

typedef struct {
    double wall_time;
    double ipc;
    double branch_miss_rate;
    double cache_miss_rate;
    double l1d_mpki;
    double itlb_mpki;
    double dtlb_mpki;
} feature_vector_t;

typedef struct {
    double median;
    double mad;
    double min;
    double max;
    int samples;
} baseline_stats_t;

typedef struct {
    char application_name[128];
    char baseline_type[32];
    char feature[64];
    double measured_value;
    double baseline_median;
    double robust_z_score;
    double threshold;
    char severity[16];
    double timestamp;
} anomaly_alert_t;

typedef struct {
    char app_directory[MAX_PATH_LEN];
    char baseline_directory[MAX_PATH_LEN];
    char alert_output_file[MAX_PATH_LEN];
    int sampling_interval_ms;
    int runs_per_app;
    int min_samples_per_app;
    int max_runtime_seconds;
    int core_affinity;
    double robust_z_threshold_medium;
    double robust_z_threshold_high;
    double robust_z_threshold_critical;
    int alert_cooldown_seconds;
    bool use_robust_statistics;
    char perf_events[MAX_EVENTS][64];
    int num_events;
} config_t;

typedef struct {
    baseline_stats_t ipc;
    baseline_stats_t branch_miss_rate;
    baseline_stats_t cache_miss_rate;
    baseline_stats_t l1d_mpki;
    baseline_stats_t itlb_mpki;
    baseline_stats_t dtlb_mpki;
} baseline_t;

typedef struct {
    char name[128];
    baseline_t baseline;
    bool has_baseline;
} app_baseline_t;

typedef struct {
    config_t config;
    baseline_t global_baseline;
    app_baseline_t app_baselines[MAX_APPS];
    int num_apps;
    feature_vector_t feature_samples[MAX_SAMPLES];
    int num_samples;
    FILE *alert_file;
    time_t last_alert_time;
} hpc_ids_t;

// Core functions
int hpc_ids_init(hpc_ids_t *ids, const char *config_file);
void hpc_ids_cleanup(hpc_ids_t *ids);
int load_config(config_t *config, const char *config_file);
int load_baseline(baseline_t *baseline, const char *baseline_file);
int load_app_baselines(hpc_ids_t *ids);

// Monitoring functions
int monitor_system(hpc_ids_t *ids, int duration_seconds);
int monitor_pid(hpc_ids_t *ids, pid_t pid, int duration_seconds);
int monitor_app(hpc_ids_t *ids, const char *app_name, int duration_seconds);

// Statistical functions
int compute_baseline_stats(baseline_stats_t *stats, double *values, int count);
double compute_median(double *values, int count);
double compute_mad(double *values, int count, double median);
double compute_robust_z_score(double value, double median, double mad);

// Detection functions
int detect_anomalies(hpc_ids_t *ids, const feature_vector_t *features, const char *app_name);
int log_alert(hpc_ids_t *ids, const anomaly_alert_t *alert);

// Baseline collection functions
int collect_baseline(hpc_ids_t *ids, const char *app_name);
int collect_all_baselines(hpc_ids_t *ids);

// Utility functions
int execute_perf_command(const char *cmd, hpc_measurement_t *measurements, int *count, int timeout);
int parse_perf_line(const char *line, double wall_time, hpc_measurement_t *measurement);
int engineer_features(hpc_measurement_t *measurements, int count, feature_vector_t *features);
char* get_app_name_from_pid(pid_t pid);
int get_available_apps(const char *app_dir, char apps[][128], int max_apps);

#endif