#include <Terminal.h>
#include <Functions.h>

int main() {

    Terminal* terminal = new Terminal();
    while (true) {
        terminal->Input();
    }
    delete terminal;
    std::string bin;
    std::cin >> bin;
    return 0;
}