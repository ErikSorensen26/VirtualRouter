#include <gtest/gtest.h>
#include <RestClient.hpp>
#include <cstdlib>
#include <CliEngine.h>
#include <CliSession.h>
#include <Gns3Harness.hpp>

class Environment
{
public:
    Environment()
    {
        engine = new CliEngine();
        gns3 = new Gns3Harness();
        session = engine->createSession();
    }

    ~Environment()
    {
        delete engine;
        delete gns3;
    }

    CliSession* resetSession()
    {
        engine->clearSessions();
        return engine->createSession();
    }

    bool start()
    {
        auto isServerUp = []
        {
            try 
            {
                RestClient api{3080};
                api.get("/v2/version");
                return true;
            }
            catch (...)
            {
                return false;
            }
        };

        if (!isServerUp())
        {
            std::system("gns3server &"); // background the server
            std::this_thread::sleep_for(std::chrono::seconds(2));
            return isServerUp();
        }
        return true;
    }

    CliEngine* engine = nullptr;
    CliSession* session = nullptr;
    Gns3Harness* gns3 = nullptr;
};

