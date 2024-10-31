#include <Interface.h>
#include <Terminal.h>
#include <Configs.h>

#include <Eigrp.h>

int main() {
    Functions* function = Functions::getInstance();

    Terminal* terminal = new Terminal();
    while (true) {
        terminal->Input();
    }
    delete terminal;
    std::string bin;
    std::cin >> bin;
    return 0;
}