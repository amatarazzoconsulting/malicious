#include <iostream>
using namespace std;

int main() {
    int total = 0;
    int i = 1;
    while (i <= 5) {
        total += i;
        i += 1;
    }
    std::cout << "sum=" << total << std::endl;
}
