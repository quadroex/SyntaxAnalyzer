#include <iostream>

int main() {
    int values[] = {3, 1, 4};
    int index = 0;
    int maximum = values[0];
    do {
        maximum = values[index] > maximum ? values[index] : maximum;
        index++;
    } while (index < 3);
    std::cout << "Maximum = " << maximum << std::endl;
    return 0;
}
