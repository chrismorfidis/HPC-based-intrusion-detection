#include "hpc_ids.h"
#include <getopt.h>

// Forward declarations
int compute_baseline_from_features(baseline_t *baseline, feature_vector_t *features, int count);
int save_baseline(const baseline_t *baseline, const char *filename, const char *app_name,
                 const config_t *config, int sample_count);

void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("Per-Application HPC Baseline Collector\n\n");
    printf("Options:\n");
    printf("  -a, --app NAME         Collect baseline for specific application\n");
    printf("  -r, --runs NUMBER      Number of runs per application (default: 10)\n");
    printf("  -c, --config FILE      Configuration file path (default: config/rigorous_hpc_config.json)\n");
    printf("  -h, --help             Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s                           # Collect baselines for all applications\n", program_name);
    printf("  %s --app matmul              # Collect baseline for matmul only\n", program_name);
    printf("  %s --app crypto --runs 15    # Collect baseline for crypto with 15 runs\n", program_name);
}

int main(int argc, char *argv[]) {
    hpc_ids_t ids;
    int opt;
    char *app_name = NULL;
    char *config_file = "config/rigorous_hpc_config.json";
    int runs = 0; // 0 means use config default
    
    static struct option long_options[] = {
        {"app",    required_argument, 0, 'a'},
        {"runs",   required_argument, 0, 'r'},
        {"config", required_argument, 0, 'c'},
        {"help",   no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "a:r:c:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'a':
                app_name = optarg;
                break;
            case 'r':
                runs = atoi(optarg);
                if (runs <= 0) {
                    fprintf(stderr, "Number of runs must be positive\n");
                    return 1;
                }
                break;
            case 'c':
                config_file = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Initialize the IDS system
    if (hpc_ids_init(&ids, config_file) != 0) {
        fprintf(stderr, "Failed to initialize HPC-IDS\n");
        return 1;
    }
    
    // Override runs if specified
    if (runs > 0) {
        ids.config.runs_per_app = runs;
    }
    
    // Signal handler for graceful shutdown
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    
    int result = 0;
    
    if (app_name) {
        printf("=== COLLECTING BASELINE FOR %s ===\n", app_name);
        result = collect_baseline(&ids, app_name);
        
        if (result == 0) {
            printf("Baseline collection completed successfully\n");
        } else {
            printf("Baseline collection failed\n");
            result = 1;
        }
    } else {
        printf("=== COLLECTING BASELINES FOR ALL APPLICATIONS ===\n");
        result = collect_all_baselines(&ids);
        
        if (result > 0) {
            printf("Baseline collection completed\n");
            result = 0; // convert success count to success status
        } else {
            printf("Baseline collection failed\n");
            result = 1;
        }
    }
    
    hpc_ids_cleanup(&ids);
    return result;
}