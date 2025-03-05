#include <iostream>

class Setup {
public:
    void run() {
        std::cout << "Just setting up" << std::endl;
    }
};

int main() {
    Setup setup;
    setup.run();
    return 0;
}
