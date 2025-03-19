#include <Terminal.h>
#include <Logger.h>

int main() 
{
    Logger::getInstance().initialize(true, /*isolateMode*/true);

    Terminal* terminal = new Terminal(false);
    while (true) {
        terminal->handleInput();
    }
    delete terminal;
    std::string bin;
    std::cin >> bin;
    return 0;
}