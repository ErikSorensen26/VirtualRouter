#include <gtest/gtest.h>
#include <RestClient.hpp>
#include <chrono>
#include <cstdlib>
#include <thread>

class Gns3Environment : public ::testing::Environment
{
    void SetUp() override
    {
        auto isServerUp = []
        {
            try 
            {
                RestClient api{3000};
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
            ASSERT_TRUE(isServerUp()) << "gns3server failed to start";
        }
    }
};

