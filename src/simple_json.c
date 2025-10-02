#include "hpc_ids.h"

// Simple JSON value extractor - finds "key": value pairs
char* extract_json_string(const char *json, const char *key) {
    static char result[256];
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);
    
    char *start = strstr(json, search_pattern);
    if (!start) return NULL;
    
    start += strlen(search_pattern);
    // Skip whitespace
    while (*start == ' ' || *start == '\t' || *start == '\n') start++;
    
    if (*start == '"') {
        start++; // Skip opening quote
        char *end = strchr(start, '"');
        if (!end) return NULL;
        
        size_t len = end - start;
        if (len >= sizeof(result)) len = sizeof(result) - 1;
        strncpy(result, start, len);
        result[len] = '\0';
        return result;
    }
    
    return NULL;
}

double extract_json_double(const char *json, const char *key) {
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);
    
    char *start = strstr(json, search_pattern);
    if (!start) return -1.0;
    
    start += strlen(search_pattern);
    // Skip whitespace
    while (*start == ' ' || *start == '\t' || *start == '\n') start++;
    
    return atof(start);
}

int extract_json_int(const char *json, const char *key) {
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);
    
    char *start = strstr(json, search_pattern);
    if (!start) return -1;
    
    start += strlen(search_pattern);
    // Skip whitespace
    while (*start == ' ' || *start == '\t' || *start == '\n') start++;
    
    return atoi(start);
}

bool extract_json_bool(const char *json, const char *key) {
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);
    
    char *start = strstr(json, search_pattern);
    if (!start) return false;
    
    start += strlen(search_pattern);
    // Skip whitespace
    while (*start == ' ' || *start == '\t' || *start == '\n') start++;
    
    return (strncmp(start, "true", 4) == 0);
}

// Extract array of strings from JSON
int extract_json_string_array(const char *json, const char *key, char results[][64], int max_items) {
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);
    
    char *start = strstr(json, search_pattern);
    if (!start) return 0;
    
    start += strlen(search_pattern);
    // Skip whitespace
    while (*start == ' ' || *start == '\t' || *start == '\n') start++;
    
    if (*start != '[') return 0;
    start++; // Skip opening bracket
    
    int count = 0;
    while (*start && *start != ']' && count < max_items) {
        // Skip whitespace and commas
        while (*start == ' ' || *start == '\t' || *start == '\n' || *start == ',') start++;
        
        if (*start == '"') {
            start++; // Skip opening quote
            char *end = strchr(start, '"');
            if (!end) break;
            
            int len = end - start;
            if (len >= 64) len = 63;
            strncpy(results[count], start, len);
            results[count][len] = '\0';
            count++;
            
            start = end + 1;
        } else {
            break;
        }
    }
    
    return count;
}