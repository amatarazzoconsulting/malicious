#include <stdio.h>

int main() {
    int total = 0;
    int i = 1;
    while (i <= 5) {
        total += i;
        i += 1;
    }
    printf("sum=%d\n", total);
}
