#include <iostream>

int main() {
    int a = 5 / 2;
    double b = 5.0 / 2;
    bool greater = b > a;
    int converted = (int)b;
    std::cout << "Integer division = " << a << std::endl;
    std::cout << "Real division = " << b << std::endl;
    std::cout << "Comparison = " << greater << std::endl;
    std::cout << "Cast = " << converted << std::endl;
    return 0;
}
