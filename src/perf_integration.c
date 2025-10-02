#include "hpc_ids.h"

int parse_perf_line(const char *line, double wall_time, hpc_measurement_t *measurement) {
    if (!line || !measurement) return -1;
    
    char *line_copy = strdup(line);
    if (!line_copy) return -1;
    
    int field = 0;
    int result = -1;
    static int debug_count = 0;
    
    measurement->wall_time = wall_time;
    measurement->duration_ms = SAMPLING_INTERVAL_MS;
    
    // Manual CSV parsing to handle empty fields properly
    char *ptr = line_copy;
    char *field_start;
    
    while (*ptr && field < 8) {
        field_start = ptr;
        
        // Find end of current field (next comma or end of line)
        while (*ptr && *ptr != ',') ptr++;
        
        // Null-terminate the field
        if (*ptr == ',') {
            *ptr = '\0';
            ptr++; // Move past the comma
        }
        
        // Trim whitespace from field
        char *token = field_start;
        while (*token == ' ' || *token == '\t') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t' || *end == '\n')) {
            *end = '\0';
            end--;
        }
        
        // Debug first cycles line only (disabled for production)
        #ifdef DEBUG_PARSING
        if (debug_count == 0 && strstr(line, "cycles") != NULL) {
            fprintf(stderr, "DEBUG: Field %d: [%s] (len=%lu) raw=[%s]\n", field, token, strlen(token), field_start);
        }
        #endif
        
        switch (field) {
            case 0: // perf_time
                measurement->perf_time = atof(token);
                break;
            case 1: // value
                if (strstr(token, "<not supported>") || strstr(token, "<not counted>") || 
                    strstr(token, "<not available>")) {
                    goto cleanup;
                }
                measurement->value = strtoull(token, NULL, 10);
                break;
            case 2: // empty field - skip
                break;
            case 3: // counter name
                strncpy(measurement->counter, token, sizeof(measurement->counter) - 1);
                measurement->counter[sizeof(measurement->counter) - 1] = '\0';
                break;
            case 4: // running count (ignored)
            case 5: // percentage (ignored)  
            case 6: // additional info (ignored)
            case 7: // more info (ignored)
                break;
        }
        field++;
    }
    
    if (field >= 4 && strlen(measurement->counter) > 0) { 
        // We need at least timestamp, value, empty, counter
        result = 0;
        if (debug_count == 0 && strstr(line, "cycles") != NULL) {
            debug_count = 1; // Only debug first cycles line
        }
    }
    
cleanup:
    free(line_copy);
    return result;
}

int execute_perf_command(const char *cmd, hpc_measurement_t *measurements, int *count, int timeout) {
    if (!cmd || !measurements || !count) return -1;
    
    FILE *fp;
    char line[MAX_LINE_LEN];
    *count = 0;
    
    fprintf(stderr, "Executing: %s\n", cmd);
    
    fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "Failed to execute perf command: %s\n", strerror(errno));
        return -1;
    }
    
    time_t start_time = time(NULL);
    
    while (fgets(line, sizeof(line), fp) && *count < MAX_SAMPLES) {
        if (timeout > 0 && (time(NULL) - start_time) > timeout) {
            fprintf(stderr, "Command timeout after %d seconds\n", timeout);
            break;
        }
        
        // Skip comments, empty lines, and non-measurement lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == ' ') {
            continue;
        }
        
        // Skip lines that don't contain comma-separated values
        if (!strchr(line, ',')) {
            continue;
        }
        
        double wall_time = (double)time(NULL);
        hpc_measurement_t measurement;
        
        if (parse_perf_line(line, wall_time, &measurement) == 0) {
            measurements[*count] = measurement;
            (*count)++;
            
            #ifdef DEBUG_PARSING
            if (*count <= 3) { // Debug first few measurements
                fprintf(stderr, "Parsed measurement %d: counter='%s', value=%lu, time=%.3f\n", 
                        *count, measurement.counter, measurement.value, measurement.perf_time);
                fprintf(stderr, "Original line: %s", line);
            }
            #endif
            
            if (*count % 100 == 0) {
                fprintf(stderr, "Collected %d measurements so far...\n", *count);
            }
        } else {
            #ifdef DEBUG_PARSING
            if (*count <= 3) { // Debug failed parsing
                fprintf(stderr, "Failed to parse line: %s", line);
            }
            #endif
        }
    }
    
    pclose(fp);
    
    if (*count == 0) {
        fprintf(stderr, "Warning: No valid measurements collected\n");
        return -1; // Only fail if no data collected
    } else {
        fprintf(stderr, "Total measurements collected: %d\n", *count);
    }
    
    // Success if we collected data, regardless of exit status
    // (timeout command returns non-zero when it kills the process)
    return 0;
}

char* get_app_name_from_pid(pid_t pid) {
    static char app_name[128];
    char path[256];
    char link[256];
    ssize_t len;
    
    snprintf(path, sizeof(path), "/proc/%d/exe", pid);
    len = readlink(path, link, sizeof(link) - 1);
    
    if (len == -1) {
        strcpy(app_name, "unknown");
        return app_name;
    }
    
    link[len] = '\0';
    
    // Extract basename
    char *basename = strrchr(link, '/');
    if (basename) {
        strcpy(app_name, basename + 1);
    } else {
        strcpy(app_name, link);
    }
    
    return app_name;
}

int build_perf_command(const config_t *config, const char *target, char *cmd_buffer, size_t buffer_size) {
    char events_str[1024] = "";
    
    // Build events string
    for (int i = 0; i < config->num_events; i++) {
        if (i > 0) strcat(events_str, ",");
        strcat(events_str, config->perf_events[i]);
    }
    
    if (target) {
        // Monitor specific target (pid or command)
        if (strncmp(target, "pid:", 4) == 0) {
            snprintf(cmd_buffer, buffer_size,
                "perf stat --no-big-num -I %d -x , -e %s -p %s 2>&1",
                config->sampling_interval_ms, events_str, target + 4);
        } else {
            snprintf(cmd_buffer, buffer_size,
                "perf stat --no-big-num -I %d -x , -e %s %s 2>&1",
                config->sampling_interval_ms, events_str, target);
        }
    } else {
        // System-wide monitoring - redirect stderr to stdout for parsing
        snprintf(cmd_buffer, buffer_size,
            "perf stat --no-big-num -I %d -x , -e %s -a 2>&1",
            config->sampling_interval_ms, events_str);
    }
    
    return 0;
}