#include "hpc_ids.h"
#include <getopt.h>

// Forward declarations for missing functions
int build_perf_command(const config_t *config, const char *target, char *cmd_buffer, size_t buffer_size);
int compute_baseline_from_features(baseline_t *baseline, feature_vector_t *features, int count);
int save_baseline(const baseline_t *baseline, const char *filename, const char *app_name,
                 const config_t *config, int sample_count);

void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("Hardware Performance Counter Intrusion Detection System\n\n");
    printf("Options:\n");
    printf("  -m, --monitor           Start monitoring mode\n");
    printf("  -p, --pid PID          Monitor specific process ID\n");
    printf("  -a, --app-name NAME    Monitor specific application by name\n");
    printf("  -d, --duration SECS    Monitoring duration in seconds (default: 60)\n");
    printf("  -c, --config FILE      Configuration file path (default: config/rigorous_hpc_config.json)\n");
    printf("  -b, --collect-baseline Collect baseline for all applications\n");
    printf("  --collect-app APP      Collect baseline for specific application\n");
    printf("  -h, --help             Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s --monitor --duration 30                 # System-wide monitoring for 30 seconds\n", program_name);
    printf("  %s --monitor --pid 1234 --duration 60      # Monitor process 1234 for 60 seconds\n", program_name);
    printf("  %s --monitor --app-name matmul             # Monitor matmul application\n", program_name);
    printf("  %s --collect-baseline                      # Collect baselines for all apps\n", program_name);
    printf("  %s --collect-app crypto                    # Collect baseline for crypto app\n", program_name);
}

int main(int argc, char *argv[]) {
    hpc_ids_t ids;
    int opt;
    bool monitor_mode = false;
    bool collect_mode = false;
    pid_t target_pid = 0;
    char *app_name = NULL;
    char *collect_app = NULL;
    char *config_file = "config/rigorous_hpc_config.json";
    int duration = 60;
    
    static struct option long_options[] = {
        {"monitor",          no_argument,       0, 'm'},
        {"pid",              required_argument, 0, 'p'},
        {"app-name",         required_argument, 0, 'a'},
        {"duration",         required_argument, 0, 'd'},
        {"config",           required_argument, 0, 'c'},
        {"collect-baseline", no_argument,       0, 'b'},
        {"collect-app",      required_argument, 0, 1000},
        {"help",             no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "mp:a:d:c:bh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'm':
                monitor_mode = true;
                break;
            case 'p':
                target_pid = atoi(optarg);
                break;
            case 'a':
                app_name = optarg;
                break;
            case 'd':
                duration = atoi(optarg);
                if (duration <= 0) {
                    fprintf(stderr, "Duration must be positive\n");
                    return 1;
                }
                break;
            case 'c':
                config_file = optarg;
                break;
            case 'b':
                collect_mode = true;
                break;
            case 1000: // --collect-app
                collect_app = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Check for conflicting options
    if (monitor_mode && collect_mode) {
        fprintf(stderr, "Cannot specify both monitor and collect modes\n");
        return 1;
    }
    
    if (monitor_mode && collect_app) {
        fprintf(stderr, "Cannot specify both monitor mode and collect-app\n");
        return 1;
    }
    
    if (collect_mode && collect_app) {
        fprintf(stderr, "Cannot specify both collect-baseline and collect-app\n");
        return 1;
    }
    
    if (target_pid > 0 && app_name) {
        fprintf(stderr, "Cannot specify both PID and application name\n");
        return 1;
    }
    
    // Initialize the IDS system
    if (hpc_ids_init(&ids, config_file) != 0) {
        fprintf(stderr, "Failed to initialize HPC-IDS\n");
        return 1;
    }
    
    // Signal handler for graceful shutdown
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    
    int result = 0;
    
    if (monitor_mode) {
        printf("=== HPC-IDS MONITORING MODE ===\n");
        
        if (target_pid > 0) {
            result = monitor_pid(&ids, target_pid, duration);
        } else if (app_name) {
            result = monitor_app(&ids, app_name, duration);
        } else {
            result = monitor_system(&ids, duration);
        }
        
        if (result == 0) {
            printf("Monitoring completed successfully\n");
        } else {
            printf("Monitoring failed\n");
        }
        
    } else if (collect_mode) {
        printf("=== COLLECTING BASELINES FOR ALL APPLICATIONS ===\n");
        
        result = collect_all_baselines(&ids);
        
        if (result > 0) {
            printf("Baseline collection completed\n");
        } else {
            printf("Baseline collection failed\n");
            result = 1;
        }
        
    } else if (collect_app) {
        printf("=== COLLECTING BASELINE FOR %s ===\n", collect_app);
        
        result = collect_baseline(&ids, collect_app);
        
        if (result == 0) {
            printf("Baseline collection completed successfully\n");
        } else {
            printf("Baseline collection failed\n");
            result = 1;
        }
        
    } else {
        fprintf(stderr, "No operation specified. Use --help for usage information.\n");
        result = 1;
    }
    
    hpc_ids_cleanup(&ids);
    return result;
}