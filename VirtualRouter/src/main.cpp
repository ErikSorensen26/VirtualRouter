#include <CliEngine.h>
#include <CliSession.h>
#include <Logger.h>

int main() 
{
    Logger::getInstance().initialize(true, /*isolateMode*/false);

    CliEngine* engine = new CliEngine();
    auto session = engine->createSession(false);
    while (true) {
        session->handleInput();
    }
    delete engine;
    std::string bin;
    std::cin >> bin;
    return 0;
}
