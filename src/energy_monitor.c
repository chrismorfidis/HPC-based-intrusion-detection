#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

#define MAX_PATH 256
#define MAX_LINE 1024

typedef struct {
    double package_energy_uj;
    double dram_energy_uj;
    double core_energy_uj;
    double uncore_energy_uj;
    time_t timestamp;
} energy_reading_t;

int read_energy_counter(const char *path, double *value) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    
    unsigned long long uj_value;
    if (fscanf(fp, "%llu", &uj_value) != 1) {
        fclose(fp);
        return -1;
    }
    
    *value = (double)uj_value;
    fclose(fp);
    return 0;
}

int get_energy_reading(energy_reading_t *reading) {
    reading->timestamp = time(NULL);
    reading->package_energy_uj = 0;
    reading->dram_energy_uj = 0;
    reading->core_energy_uj = 0;
    reading->uncore_energy_uj = 0;
    
    // Try different RAPL interfaces
    const char *rapl_paths[] = {
        "/sys/class/powercap/intel-rapl:0/energy_uj",           // Package
        "/sys/class/powercap/intel-rapl:0:0/energy_uj",        // Core
        "/sys/class/powercap/intel-rapl:0:1/energy_uj",        // Uncore
        "/sys/class/powercap/intel-rapl:0:2/energy_uj",        // DRAM
        NULL
    };
    
    // Try package energy
    if (read_energy_counter(rapl_paths[0], &reading->package_energy_uj) != 0) {
        // If RAPL not available, estimate using CPU frequency and utilization
        FILE *fp = fopen("/proc/stat", "r");
        if (fp) {
            char line[MAX_LINE];
            if (fgets(line, sizeof(line), fp)) {
                unsigned long user, nice, system, idle;
                if (sscanf(line, "cpu %lu %lu %lu %lu", &user, &nice, &system, &idle) == 4) {
                    unsigned long total = user + nice + system + idle;
                    double cpu_usage = (double)(user + nice + system) / total;
                    // Rough estimate: 15W TDP * usage percentage
                    reading->package_energy_uj = cpu_usage * 15.0 * 1000000.0; // Convert to microjoules/second
                }
            }
            fclose(fp);
        }
    }
    
    // Try core energy
    read_energy_counter(rapl_paths[1], &reading->core_energy_uj);
    
    // Try uncore energy  
    read_energy_counter(rapl_paths[2], &reading->uncore_energy_uj);
    
    // Try DRAM energy
    read_energy_counter(rapl_paths[3], &reading->dram_energy_uj);
    
    return 0;
}

double calculate_power_watts(double energy_uj_start, double energy_uj_end, double time_seconds) {
    if (time_seconds <= 0) return 0.0;
    
    double energy_joules = (energy_uj_end - energy_uj_start) / 1000000.0;
    return energy_joules / time_seconds;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <output_file> <duration_seconds>\n", argv[0]);
        return 1;
    }
    
    const char *output_file = argv[1];
    int duration = atoi(argv[2]);
    
    if (duration <= 0) {
        fprintf(stderr, "Error: Duration must be positive\n");
        return 1;
    }
    
    FILE *fp = fopen(output_file, "w");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open output file %s: %s\n", output_file, strerror(errno));
        return 1;
    }
    
    // Write CSV header
    fprintf(fp, "timestamp,package_power_watts,core_power_watts,dram_power_watts,uncore_power_watts\n");
    
    energy_reading_t prev_reading, curr_reading;
    
    // Get initial reading
    if (get_energy_reading(&prev_reading) != 0) {
        fprintf(stderr, "Error: Cannot read energy counters\n");
        fclose(fp);
        return 1;
    }
    
    printf("Starting energy monitoring for %d seconds...\\n", duration);
    
    time_t start_time = time(NULL);
    
    while ((time(NULL) - start_time) < duration) {
        sleep(1);
        
        if (get_energy_reading(&curr_reading) != 0) {
            fprintf(stderr, "Warning: Failed to read energy counters\\n");
            continue;
        }
        
        double time_diff = difftime(curr_reading.timestamp, prev_reading.timestamp);
        
        if (time_diff > 0) {
            double package_power = calculate_power_watts(
                prev_reading.package_energy_uj, 
                curr_reading.package_energy_uj, 
                time_diff
            );
            
            double core_power = calculate_power_watts(
                prev_reading.core_energy_uj, 
                curr_reading.core_energy_uj, 
                time_diff
            );
            
            double dram_power = calculate_power_watts(
                prev_reading.dram_energy_uj, 
                curr_reading.dram_energy_uj, 
                time_diff
            );
            
            double uncore_power = calculate_power_watts(
                prev_reading.uncore_energy_uj, 
                curr_reading.uncore_energy_uj, 
                time_diff
            );
            
            fprintf(fp, "%ld,%.3f,%.3f,%.3f,%.3f\n", 
                    curr_reading.timestamp, package_power, core_power, dram_power, uncore_power);
            
            fflush(fp);
        }
        
        prev_reading = curr_reading;
    }
    
    fclose(fp);
    printf("Energy monitoring completed. Results saved to %s\\n", output_file);
    
    return 0;
}
