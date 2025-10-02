#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main() {
    printf("Running memory test...\n");
    
    const int size = 1000000;
    int *data = malloc(size * sizeof(int));
    
    for (int i = 0; i < size; i++) {
        data[i] = rand() % 1000;
    }
    
    long sum = 0;
    for (int i = 0; i < size; i++) {
        sum += data[i];
    }
    
    printf("Memory test completed, sum: %ld\n", sum);
    free(data);
    return 0;
}