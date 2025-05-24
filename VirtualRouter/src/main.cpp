#include <CliEngine.h>
#include <CliSession.h>
#include <Logger.h>
#include <Global.h>

int main() 
{
    Logger::getInstance().initialize(true, /*isolateMode*/false);
    Global global;
    CliEngine& engine = global.engine;
    auto session = engine.createSession(false);
    while (true) {
        session->handleInput();
    }
    std::string bin;
    std::cin >> bin;
    return 0;
}
