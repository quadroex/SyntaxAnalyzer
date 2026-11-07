#include <iostream>

int sum(int values[], int count) {
    int total = 0;
    for (int i = 0; i < count; i++) {
        total += values[i];
    }
    return total;
}

void sort(int values[], int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (values[j] > values[j + 1]) {
                int temporary = values[j];
                values[j] = values[j + 1];
                values[j + 1] = temporary;
            }
        }
    }
}

int main() {
    int count;
    std::cin >> count;
    if (count < 1 || count > 10) {
        std::cout << "Count must be between 1 and 10" << std::endl;
        return 1;
    }
    int values[10];
    for (int i = 0; i < count; i++) {
        std::cin >> values[i];
    }
    std::cout << "Sum = " << sum(values, count) << std::endl;
    sort(values, count);
    std::cout << "Sorted = ";
    for (int i = 0; i < count; i++) {
        std::cout << values[i] << " ";
    }
    std::cout << std::endl;
    return 0;
}
