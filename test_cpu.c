#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    printf("Running CPU intensive test...\n");
    
    volatile long sum = 0;
    for (long i = 0; i < 100000000; i++) {
        sum += i * i;
    }
    
    printf("Test completed, sum: %ld\n", sum);
    return 0;
}