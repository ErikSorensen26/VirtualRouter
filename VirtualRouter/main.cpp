#include <Terminal.h>
#include <Logger.h>

int main() {
    Logger::getInstance().initialize(true);

    Terminal* terminal = new Terminal(false);
    while (true) {
        terminal->Input();
    }
    delete terminal;
    std::string bin;
    std::cin >> bin;
    return 0;
}