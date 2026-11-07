#include <iostream>

int main() {
    int sum = 0;
    for (int i = 1; i <= 10; i++) {
        if (i == 3) {
            continue;
        }
        if (i == 6) {
            break;
        }
        sum += i;
    }
    int count = 3;
    while (count > 0) {
        std::cout << count << " ";
        count--;
    }
    std::cout << std::endl << sum << std::endl;
    return 0;
}
