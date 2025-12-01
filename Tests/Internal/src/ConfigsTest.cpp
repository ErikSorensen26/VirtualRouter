// Internal_ConfigTest.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <json.hpp>
#include <string>
#include <vector>
#include <sstream>
#include <CliEngine.h>
#include <HardwareManager.h>
#include <InterfaceType.hpp>
#include <Mode.hpp>
#include <Configs.h>           // Your Configs class header
#include "MockFileSystem.hpp" // Mocked file system interface

using json = nlohmann::ordered_json;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

void validateJson(const std::string& json)
{
    try
    {
        auto parsedJson = nlohmann::ordered_json::parse(json);
    }
    catch (const nlohmann::ordered_json::parse_error& e)
    {
        // Catch and display parsing errors
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
        std::cerr << "Error occurred at byte position: " << e.byte << std::endl;

        // Lamda for printing debugging snippet
        auto printDebugSnippet = [](const std::string& jsonString, size_t errorByte, size_t contextSize = 20) {
            std::cout << "Nearby JSON snippet (hex): ";
            size_t start = (errorByte > contextSize) ? errorByte - contextSize : 0;
            size_t end = std::min(errorByte + contextSize, jsonString.size());

            for (size_t i = start; i < end; ++i)
            {
                unsigned char c = static_cast<unsigned char>(jsonString[i]);
                if (std::isprint(c))
                {
                    std::cout << c; // Printable character
                }
                else
                {
                    std::cout << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c); // Non-printable as hex
                }
            }
            std::cout << std::endl;
        };

        // Call the lamda to print debugging information
        printDebugSnippet(json, e.byte);
    }
}

// Test Fixture for Configs Function Tests
class Internal_ConfigTest : public ::testing::Test 
{
protected:
    Configs* configs = nullptr;
    MockFileSystem* mockFileSystem = nullptr;
    ModeConfig* modeConfig = nullptr;

    // Paths to configuration files
    std::string startupFilePath = ROUTER_CONFIG_FILE;
    std::string additionalConfigPath = HW_CONFIG_FILE;

    // SetUp runs before each test
    void SetUp() override 
    {
        mockFileSystem = new MockFileSystem;
        configs = new Configs(mockFileSystem);
        modeConfig = new ModeConfig;
        modeConfig->currentMode = Mode::globalConfiguration;
    }

    // TearDown runs after each test
    void TearDown() override 
    {
        delete configs;
        delete mockFileSystem;
    }

    // Helper method to capture std::cout output
    std::string capturePrintConfigOutput() 
    {
        std::stringstream buffer;
        std::streambuf* oldCout = std::cout.rdbuf(buffer.rdbuf());

        configs->printConfig();

        std::cout.rdbuf(oldCout); // Restore original buffer
        return buffer.str();
    }

    void printRoot() {std::cout << configs->root.dump(4) << std::endl;}

    // Accessor methods for internal state (if necessary)
    std::vector<std::string> getRecovery() { return configs->recoverConfigs();}
    std::vector<std::string> getVolatileInputs() {return configs->volatileInputs;}
    std::string getVolatileValue(std::string& type, std::string& value, nlohmann::ordered_json& currentJson) {return configs->getVolatileValue(type, value, currentJson);}
    std::string getVolatileValue(std::string& type, std::string& value, std::vector<std::string> volatileValues) {return configs->getVolatileValue(type, value, volatileValues);}
};

// Test Initialization with an empty startup file
TEST_F(Internal_ConfigTest, InitConfigs_EmptyStartupFile)
{
    // Set the mock to indicate that the startup file exists but is empty
    mockFileSystem->setupMockFile(startupFilePath, "{}");

    // Set up the additional Configs.json as empty
    mockFileSystem->setupMockFile(additionalConfigPath, "{}");

    // Initialize configurations
    configs->initConfigs({});

    // Verify that root is empty
    EXPECT_TRUE(configs->root.empty());

    // Verify that additional configurations are loaded as empty
    EXPECT_TRUE(configs->hwManager->getPhysicalInterfaces().empty());
}

// Test Initialization with a valid startup file and additional Configs.json.
TEST_F(Internal_ConfigTest, InitConfigs_ValidStartupAndAdditionalConfig) {
    // Define valid startup configuration
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "TestRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "192.168.1.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": "100000"
                    }
                }
            ],
            "FastEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.0.0.1",
                                "mask": "255.255.255.252"
                            }
                        }
                    }
                }
            ]
        }
    }
    )";

    // Define valid additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ],
            "FastEthernet": [
                "fe-0"
            ],
            "Ethernet": [
                "e-0"
            ]
        }
    }
    )";

    // Set up the mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify root configuration
    EXPECT_EQ(configs->root["hostname"]["word"], "TestRouter");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["id"], "0");

    // Verify interface configurations
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0][MODE_KEY]["ip"]["address"]["ip"], "192.168.1.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0][MODE_KEY]["ip"]["address"]["mask"], "255.255.255.0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0][MODE_KEY]["bandwidth"], "100000");
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0][MODE_KEY]["ip"]["address"]["ip"], "10.0.0.1");
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0][MODE_KEY]["ip"]["address"]["mask"], "255.255.255.252");

    // Verify additional configurations
    const auto& gigs = configs->hwManager->getPhysicalInterfaces(InterfaceType::GIGABIT_ETHERNET);
    const auto& fasts = configs->hwManager->getPhysicalInterfaces(InterfaceType::FAST_ETHERNET);
    const auto& eths = configs->hwManager->getPhysicalInterfaces(InterfaceType::ETHERNET);
    EXPECT_EQ(gigs.size(), 1);
    EXPECT_EQ(fasts.size(), 1);
    EXPECT_EQ(eths.size(), 1);
}

// Test Initialization with Valid Startup and Additional Configs
TEST_F(Internal_ConfigTest, InitConfigs_ValidStartupAndAdditionalConfig_ShouldInitializeCorrectly) {
    // Define valid startup configuration JSON
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "TestRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "192.168.1.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "100000"
                        }
                    }
                }
            ],
            "FastEthernet": [
                {
                    "id": "1",
                    "commands": {
                        "ip": {
                            "address": {
                                "dhcp": {}
                            }
                        },
                        "bandwidth": {
                            "id": "1000"
                        }
                    }
                }
            ]
        },
        "router": {
            "eigrp": [],
            "ospf": [],
            "bgp": []
        },
        "line": {},
        "policy-map": {}
    }
    )";

    // Define valid additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ],
            "FastEthernet": [
                "fe-0",
                "fe-1"
            ]
        }
    }
    )";

    // Set up the mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify that configurations are loaded correctly
    // Verify hostname
    EXPECT_EQ(configs->root["hostname"]["word"], "TestRouter");

    // Verify interface configurations
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["id"], "0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"], "192.168.1.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["mask"], "255.255.255.0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["bandwidth"]["id"], "100000");

    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0]["id"], "1");
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0]["commands"]["ip"]["address"]["dhcp"], json::object());
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0]["commands"]["bandwidth"]["id"], "1000");

    // Verify additional configurations
    const auto& gigs = configs->hwManager->getPhysicalInterfaces(InterfaceType::GIGABIT_ETHERNET);
    const auto& fasts = configs->hwManager->getPhysicalInterfaces(InterfaceType::FAST_ETHERNET);
    const auto& eths = configs->hwManager->getPhysicalInterfaces(InterfaceType::ETHERNET);
    EXPECT_EQ(gigs.size(), 1);
    EXPECT_EQ(fasts.size(), 2);
}

// Test Initialization with Empty Startup and Additional Configs
TEST_F(Internal_ConfigTest, InitConfigs_EmptyStartupAndAdditionalConfig_ShouldInitializeWithEmptyConfigs) {
    // Define empty startup configuration JSON
    std::string startupConfig = "{}";

    // Define empty additional Configs.json
    std::string additionalConfig = "{}";

    // Set up the mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify that configurations are loaded correctly
    // Verify hostname is not set
    EXPECT_FALSE(configs->root.contains("hostname"));

    // Verify interface configurations are empty
    EXPECT_TRUE(configs->root["interface"].empty());

    // Verify additional configurations are loaded as empty
    EXPECT_TRUE(configs->hwManager->getPhysicalInterfaces().empty());
}

// Test Initialization with Missing Startup File
TEST_F(Internal_ConfigTest, InitConfigs_MissingStartupFile_ShouldFailInitialization) {
    // Mock fileExists to return false for startup file

    // Initialize configurations
    configs->initConfigs({});

    // Verify that configurations are empty
    EXPECT_TRUE(configs->root.empty());
    EXPECT_TRUE(configs->hwManager->getPhysicalInterfaces().empty());
}

// Test Initialization with Malformed JSON in Startup File
TEST_F(Internal_ConfigTest, InitConfigs_MalformedStartupJSON_ShouldInitializeWithEmptyRoot) {
    // Define malformed startup configuration JSON
    // Trailing comma makes it invalid
    std::string malformedStartupConfig = R"(
    {
        "hostname": {
            "word": "MalformedRouter",
        }
    }
    )";

    // Define valid additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up the mock files
    mockFileSystem->setupMockFile(startupFilePath, malformedStartupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify that root is empty due to parse error
    EXPECT_TRUE(configs->root.empty());

    // Verify that additional configurations are loaded correctly
    EXPECT_TRUE(configs->hwManager->getPhysicalInterfaces().empty());
}

// Test Initialization with Missing Required Fields in Startup Config
TEST_F(Internal_ConfigTest, InitConfigs_MissingRequiredFields_ShouldInitializeWithPartialConfigs) {
    // Define startup configuration missing the 'hostname' field
    std::string startupConfig = R"(
    {
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.0.0.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "100000"
                        }
                    }
                }
            ]
        }
    }
    )";

    // Define additional Configs.json with some required fields
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up the mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify that 'hostname' is not set
    EXPECT_FALSE(configs->root.contains("hostname"));

    // Verify interface configurations
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"].size(), 1);
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["id"], "0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"], "10.0.0.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["mask"], "255.255.255.0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["bandwidth"]["id"], "100000");

    // Verify additional configurations
    const auto& gigs = configs->hwManager->getPhysicalInterfaces(InterfaceType::GIGABIT_ETHERNET);
    EXPECT_EQ(gigs.size(), 1);
}

// Test Hostname Parsing
TEST_F(Internal_ConfigTest, ParseHostname_ShouldSetHostnameCorrectly) {
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "MyRouter"
        },
        "interface": {}
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up the mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify hostname
    EXPECT_EQ(configs->root["hostname"]["word"], "MyRouter");
}

// Test Interface Parsing for GigabitEthernet and FastEthernet
TEST_F(Internal_ConfigTest, ParseInterfaces_ShouldSetInterfacesCorrectly) {
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "RouterX"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.0.0.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "1000000"
                        }
                    }
                }
            ],
            "FastEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "192.168.1.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "1000"
                        }
                    }
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ],
            "FastEthernet": [
                "fe-0"
            ]
        }
    }
    )";

    // Set up the mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify interface configurations
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"].size(), 1);
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["id"], "0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"], "10.0.0.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["mask"], "255.255.255.0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["bandwidth"]["id"], "1000000");

    EXPECT_EQ(configs->root["interface"]["FastEthernet"].size(), 1);
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0]["id"], "0");
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0]["commands"]["ip"]["address"]["ip"], "192.168.1.1");
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0]["commands"]["ip"]["address"]["mask"], "255.255.255.0");
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0]["commands"]["bandwidth"]["id"], "1000");
}

// Test Command Parsing for IP, Mask, and Bandwidth
TEST_F(Internal_ConfigTest, ParseCommands_ShouldSetCommandsCorrectly) {
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "RouterY"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "172.16.0.1",
                                "mask": "255.255.255.0"
                            },
                            "mtu": {
                                "id": "1500"
                            }
                        },
                        "bandwidth": {
                            "id": "1000000"
                        }
                    }
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up the mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify command configurations
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"], "172.16.0.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["mask"], "255.255.255.0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["mtu"]["id"], "1500");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["bandwidth"]["id"], "1000000");
}

// Test Router Protocol Parsing (EIGRP, OSPF, BGP)
TEST_F(Internal_ConfigTest, ParseRouterProtocols_ShouldSetRouterProtocolsCorrectly) {
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "RouterZ"
        },
        "interface": {},
        "router": {
            "eigrp": [
                {
                    "id": "100",
                    "network": [
                        {
                            "ip": "192.168.1.0",
                            "wildcard": "0.0.0.255"
                        },
                        {
                            "ip": "10.0.0.0",
                            "wildcard": "0.0.0.255"
                        }
                    ],
                    "passive-interface": { 
                        "FastEthernet": [
                            {
                               "id": "1"
                            } 
                        ] 
                    } 
                }
            ],
            "ospf": [
                {
                    "id": "1",
                    "network": [ 
                        {
                            "ip": "172.16.0.0",
                            "wildcard": "0.0.255.255",
                            "id": "0" 
                        },
                        {
                            "ip": "192.168.0.0",
                            "wildcard": "0.0.0.255",
                            "id": "1" 
                        }
                    ]
                }
            ],
            "bgp": [
                {
                    "id": "65000",
                    "neighbor": [ 
                        {
                            "ip": "192.168.100.1",
                            "num": "65001" 
                        },
                        {
                            "ip": "192.168.200.1",
                            "num": "65002" 
                        }
                    ]
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {},
        "line": {},
        "policy-map": {}
    }
    )";

    // Set up the mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify EIGRP Configurations
    EXPECT_EQ(configs->root["router"]["eigrp"].size(), 1);
    EXPECT_EQ(configs->root["router"]["eigrp"][0]["id"], "100");
    EXPECT_EQ(configs->root["router"]["eigrp"][0]["network"].size(), 2);
    EXPECT_EQ(configs->root["router"]["eigrp"][0]["network"][0]["ip"], "192.168.1.0");
    EXPECT_EQ(configs->root["router"]["eigrp"][0]["network"][0]["wildcard"], "0.0.0.255");
    EXPECT_EQ(configs->root["router"]["eigrp"][0]["passive-interface"]["FastEthernet"].size(), 1);
    EXPECT_EQ(configs->root["router"]["eigrp"][0]["passive-interface"]["FastEthernet"][0]["id"], "1");

    // Verify OSPF Configurations
    EXPECT_EQ(configs->root["router"]["ospf"].size(), 1);
    EXPECT_EQ(configs->root["router"]["ospf"][0]["id"], "1");
    EXPECT_EQ(configs->root["router"]["ospf"][0]["network"].size(), 2);
    EXPECT_EQ(configs->root["router"]["ospf"][0]["network"][0]["ip"], "172.16.0.0");
    EXPECT_EQ(configs->root["router"]["ospf"][0]["network"][0]["wildcard"], "0.0.255.255");
    EXPECT_EQ(configs->root["router"]["ospf"][0]["network"][0]["id"], "0");

    EXPECT_EQ(configs->root["router"]["ospf"][0]["network"][1]["ip"], "192.168.0.0");
    EXPECT_EQ(configs->root["router"]["ospf"][0]["network"][1]["wildcard"], "0.0.0.255");
    EXPECT_EQ(configs->root["router"]["ospf"][0]["network"][1]["id"], "1");

    // Verify BGP Configuration
    EXPECT_EQ(configs->root["router"]["bgp"].size(), 1);
    EXPECT_EQ(configs->root["router"]["bgp"][0]["id"], "65000");
    EXPECT_EQ(configs->root["router"]["bgp"][0]["neighbor"].size(), 2);
    EXPECT_EQ(configs->root["router"]["bgp"][0]["neighbor"][0]["ip"], "192.168.100.1");
    EXPECT_EQ(configs->root["router"]["bgp"][0]["neighbor"][0]["num"], "65001");
    EXPECT_EQ(configs->root["router"]["bgp"][0]["neighbor"][1]["ip"], "192.168.200.1");
    EXPECT_EQ(configs->root["router"]["bgp"][0]["neighbor"][1]["num"], "65002");
}

// Test Policy Map Parsing
TEST_F(Internal_ConfigTest, ParsePolicyMap_ShouldSetPolicyMapCorrectly) {
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "PolicyMapRouter"
        },
        "interface": {},
        "policy-map": {
            "name": "QoS_Policy",
            "commands": {
                "class": [
                    {
                        "name": "Voice_Traffic",
                        "commands": {
                            "priority": {
                                "value": "50"
                            },
                            "police": {
                                "value": "50000"
                            }
                        }
                    },
                    {
                        "name": "Data_Traffic",
                        "commands": {
                            "bandwidth": {
                                "value": "100000"
                            },
                            "shape": {
                                "value": "1000000"
                            }
                        }
                    }
                ]
            }
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up the mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify Policy Map
    EXPECT_EQ(configs->root["policy-map"]["name"], "QoS_Policy");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"].size(), 2);

    // Verify first class
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][0]["name"], "Voice_Traffic");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][0]["commands"]["priority"]["value"], "50");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][0]["commands"]["police"]["value"], "50000");

    // Verify second class
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][1]["name"], "Data_Traffic");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][1]["commands"]["bandwidth"]["value"], "100000");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][1]["commands"]["shape"]["value"], "1000000");
}

// Test RecoverConfigs returns correct list of commands
TEST_F(Internal_ConfigTest, RecoverConfigs_ShouldReturnCorrectCommandsList) {
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "RecoverRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.0.0.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "1000000"
                        }
                    }
                },
                {
                    "id": "1",
                    "commands": {
                        "ip": {
                            "address": {
                                "dhcp": {}
                            }
                        },
                        "bandwidth": {
                            "id": "1000"
                        }
                    }
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0",
                "ge-1"
            ]
        }
    }
    )";

    // Set up the mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Recover commands
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    // Define expected commands
    std::vector<std::string> expectedCommands = {
        "hostname RecoverRouter",
        "interface GigabitEthernet 0",
        "ip address 10.0.0.1 255.255.255.0",
        "bandwidth 1000000",
        "interface GigabitEthernet 1",
        "ip address dhcp",
        "bandwidth 1000"
    };

    // Verify recovery commands match
    EXPECT_EQ(recoveryCommands.size(), expectedCommands.size());
    for (size_t i = 0; i < expectedCommands.size(); ++i) {
        EXPECT_EQ(recoveryCommands[i], expectedCommands[i]);
    }
}

// Test RecoverConfigs handles nested commands correctly
TEST_F(Internal_ConfigTest, RecoverConfigs_NestedCommands_ShouldHandleNestedCommandsCorrectly) {
    
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "NestedRecoverRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.1.1.1",
                                "mask": "255.255.255.0"
                            }
                        }
                    }
                }
            ]
        },
        "router": {
            "ospf": [
                {
                    "id": "1",
                    "commands": {
                        "network": [
                            {
                                "ip": "10.0.0.0",
                                "wildcard": "0.255.255.255",
                                "area": {
                                    "num": "0"
                                }
                            }
                        ]
                    }
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Recover commands
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    // Define expected commands
    std::vector<std::string> expectedCommands = {
        "hostname NestedRecoverRouter",
        "interface GigabitEthernet 0",
        "ip address 10.1.1.1 255.255.255.0",
        "router ospf 1",
        "network 10.0.0.0 0.255.255.255 area 0"
    };

    // Verify recovery commands match
    EXPECT_EQ(recoveryCommands.size(), expectedCommands.size());
    for (size_t i = 0; i < expectedCommands.size(); ++i) {
        EXPECT_EQ(recoveryCommands[i], expectedCommands[i]);
    }
}

// Test RecoverConfigs handles volatile and non-volatile commands correctly
TEST_F(Internal_ConfigTest, RecoverConfigs_VolatileAndNonVolatile_ShouldHandleCorrectly) {
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "VolatileNonVolatileRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.2.2.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "1000000"
                        },
                        "helper-address": {
                            "ip": "10.2.3.1"
                        }
                    }
                },
                {
                    "id": "1",
                    "commands": {
                        "ip": {
                            "address": {
                                "dhcp": {}
                            }
                        },
                        "bandwidth": {
                            "id": "1000"
                        }
                    }
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0",
                "ge-1"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Recover commands
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    // Define expected commands, assuming "helper-address" is a volatile command
    std::vector<std::string> expectedCommands = {
        "hostname VolatileNonVolatileRouter",
        "interface GigabitEthernet 0",
        "ip address 10.2.2.1 255.255.255.0",
        "bandwidth 1000000",
        "helper-address 10.2.3.1",
        "interface GigabitEthernet 1",
        "ip address dhcp",
        "bandwidth 1000"
    };

    // Verify recovery commands match
    EXPECT_EQ(recoveryCommands.size(), expectedCommands.size());
    for (size_t i = 0; i < expectedCommands.size(); ++i) {
        EXPECT_EQ(recoveryCommands[i], expectedCommands[i]);
    }
}

// Test isVolatile function
TEST_F(Internal_ConfigTest, IsVolatile_ShouldIdentifyVolatileCommandsCorrectly) {
    // Define some volatile and non-volatile commands
    std::vector<std::pair<std::string, bool>> testCases = {
        // Not Volatile
        {"ip", false},
        {"address", false},
        {"dhcp", false},
        {"bandwidth", false},
        {"<cr>", false},
        {"mtu", false},
        {"firewall", false},
        {"xyz", false},
    };

    for (const std::string& value : getVolatileInputs())
    {
        testCases.push_back(std::pair<std::string, bool>(value, true));
    }

    for (const auto& [command, expected] : testCases) {
        EXPECT_EQ(configs->isVolatile(command), expected) << "Failed for command: " << command;
    }
}

// Test getVolatileValue function (overload with json)
TEST_F(Internal_ConfigTest, GetVolatileValue_WithJson_ShouldReturnCorrectValue) {
    // Example command and com
    std::string type = "A.B.C.D";
    std::string value = "10.0.0.1";
    nlohmann::ordered_json currentJson = R"({"ip": "10.0.0.1", "mask": "255.255.255.0"})"_json;

    std::string expectedValue = "ip_2";
    std::string actualValue = getVolatileValue(type, value, currentJson);
    EXPECT_EQ(actualValue, expectedValue);

    // Another example with mask
    type = "A.B.C.D";
    value = "255.255.255.255";
    currentJson = R"({"ip": "10.0.0.1", "mask": "255.255.255.255"})"_json;

    expectedValue = "mask_2";
    actualValue = getVolatileValue(type, value, currentJson);
    EXPECT_EQ(actualValue, expectedValue);

    // IPv6 example
    type = "X:X:X:X::X";
    value = "2001:0db8::1/64";
    currentJson = R"({"ipv6": "2001:0db8::1/64"})"_json;

    expectedValue = "ipv6_2";
    actualValue = getVolatileValue(type, value, currentJson);
    EXPECT_EQ(actualValue, expectedValue);

    // MAC example
    type = "H.H.H";
    value = "001A2B";
    currentJson = R"({"mac": "001A2B"})"_json;

    expectedValue = "mac_2";
    actualValue = getVolatileValue(type, value, currentJson);
    EXPECT_EQ(actualValue, expectedValue);

    // Command starting with '<'
    type = "<command>";
    value = "execute";
    currentJson = R"({"id": "execute"})"_json;

    expectedValue = "id_2";
    actualValue = getVolatileValue(type, value, currentJson);
    EXPECT_EQ(actualValue, expectedValue);
}

// Test getVolatileValue function (overload with vector)
TEST_F(Internal_ConfigTest, GetVolatileValue_WithVector_ShouldReturnCorrectValue) {
    // Example command and com
    std::string command = "A.B.C.D";
    std::string com = "10.0.0.1";
    std::vector<std::string> volatileValues = {"ip"};

    std::string expectedValue = "ip_2";
    std::string actualValue = getVolatileValue(command, com, volatileValues);
    EXPECT_EQ(actualValue, expectedValue);

    // Duplicate value
    volatileValues.push_back("ip_2");
    expectedValue = "ip_3";
    actualValue = getVolatileValue(command, com, volatileValues);
    EXPECT_EQ(actualValue, expectedValue);

    // Another duplicate
    volatileValues.push_back("ip_3");
    expectedValue = "ip_4";
    actualValue = getVolatileValue(command, com, volatileValues);
    EXPECT_EQ(actualValue, expectedValue);

    // Mask example
    command = "A.B.C.D";
    com = "255.255.255.255";
    volatileValues.clear();
    expectedValue = "mask";
    actualValue = getVolatileValue(command, com, volatileValues);
    EXPECT_EQ(actualValue, expectedValue);

    // Another mask duplicate
    volatileValues.push_back("mask");
    expectedValue = "mask_2";
    actualValue = getVolatileValue(command, com, volatileValues);
    EXPECT_EQ(actualValue, expectedValue);

    // Try a wildcard
    com = "0.255.255.255";
    expectedValue = "wildcard";
    actualValue = getVolatileValue(command, com, volatileValues);
    EXPECT_EQ(actualValue, expectedValue);
}

// Test joinCommand function
TEST_F(Internal_ConfigTest, JoinCommand_ShouldJoinCommandPartsCorrectly) {
    std::vector<std::string> commandParts1 = {"interface", "GigabitEthernet", "1"};
    std::string expected1 = "interface GigabitEthernet 1";
    std::string actual1 = configs->joinCommand(commandParts1);
    EXPECT_EQ(actual1, expected1);

    std::vector<std::string> commandParts2 = {"ip", "address", "10.0.0.1", "255.255.255.0"};
    std::string expected2 = "ip address 10.0.0.1 255.255.255.0";
    std::string actual2 = configs->joinCommand(commandParts2);
    EXPECT_EQ(actual2, expected2);

    std::vector<std::string> commandParts3 = {"hostname", "MyRouter"};
    std::string expected3 = "hostname MyRouter";
    std::string actual3 = configs->joinCommand(commandParts3);
    EXPECT_EQ(actual3, expected3);

    std::vector<std::string> commandParts4 = {"router", "bgp", "65000", "neighbor", "192.168.100.1", "65001"};
    std::string expected4 = "router bgp 65000 neighbor 192.168.100.1 65001";
    std::string actual4 = configs->joinCommand(commandParts4);
    EXPECT_EQ(actual4, expected4);
}

// Test Initialization with Duplicate Interface IDs
TEST_F(Internal_ConfigTest, InitConfigs_DuplicateInterfaceIDs_ShouldHandleGracefully) {
    // Define startup configuration with duplicate interface IDs
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "DuplicateIDRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "1",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.3.3.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "1000000"
                        }
                    }
                },
                {
                    "id": "1",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.3.3.2",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "2000000"
                        }
                    }
                }
            ]
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0",
                "ge-1"
            ]
        }
    }
    )";

    // Set up the mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify that both interfaces with duplicate IDs are loaded
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"].size(), 2);
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["id"], "1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"], "10.3.3.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["bandwidth"]["id"], "1000000");

    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][1]["id"], "1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][1]["commands"]["ip"]["address"]["ip"], "10.3.3.2");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][1]["commands"]["bandwidth"]["id"], "2000000");

    // Verify additional configurations
    const auto& gigs = configs->hwManager->getPhysicalInterfaces(InterfaceType::GIGABIT_ETHERNET);
    EXPECT_EQ(gigs.size(), 2);
}

// Test Initialization with Invalid Interface Types
TEST_F(Internal_ConfigTest, InitConfigs_InvalidInterfaceTypes_ShouldHandleGracefully) 
{
    // Define startup configuration with invalid interface type
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "InvalidInterfaceRouter"
        },
        "interface": {
            "Ethernet": [ // Unsupported interface type
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.4.4.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "1000000"
                        }
                    }
                }
            ]
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "Ethernet": [
                "e-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify that unsupported interface types are ignored or handled
    // Assuming that 'Ethernet' is not a supported interface type, expect it to be loaded or not
    // Depending on implementation, adjust the expectations

    // For this example, let's assume 'Ethernet' is ignored
    EXPECT_FALSE(configs->root["interface"].contains("Ethernet"));

    // Verify other configurations
    const auto& eths = configs->hwManager->getPhysicalInterfaces(InterfaceType::ETHERNET);
    EXPECT_EQ(eths.size(), 1);
}

// Test Initialization with Empty Interface Lists
TEST_F(Internal_ConfigTest, InitConfigs_EmptyInterfaceList_ShouldHandleGracefully) 
{
    // Define startup configuration with empty interface lists
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "EmptyInterfaceRouter"
        },
        "interface": {
            "GigabitEthernet": [],
            "FastEthernet": []
        }
    }
    )";

    // Define additional Configs.json with empty interface mappings
    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify that interface lists are empty
    EXPECT_TRUE(configs->root["interface"]["GigabitEthernet"].empty());
    EXPECT_TRUE(configs->root["interface"]["FastEthernet"].empty());

    // Verify that physicalInterfaces is empty
    EXPECT_TRUE(configs->hwManager->getPhysicalInterfaces().empty());
}

// Test Initialization with Interface Missing Commands
TEST_F(Internal_ConfigTest, InitConfigs_InterfaceMissingCommands_ShouldHandleGracefully) 
{
    // Define startup configuration with interface missing 'commands'
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "NoCommandsRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0"
                }
            ]
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify that interface exists without commands
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"].size(), 1);
    EXPECT_FALSE(configs->root["interface"]["GigabitEthernet"][0].contains("commands"));

    // Recover commands should skip this interface or handle accordingly
    configs->recoverConfigs();
    std::vector<std::string> recoveryCommands = getRecovery();

    // Define expected commands (assuming commands are skipped)
    std::vector<std::string> expectedCommands = {
        "hostname NoCommandsRouter",
        "interface GigabitEthernet 0"
    };

    // Verify recovery commands match
    EXPECT_EQ(recoveryCommands.size(), expectedCommands.size());
    EXPECT_EQ(recoveryCommands[0], expectedCommands[0]);
}

// Test Correct Loading of All Configurations
TEST_F(Internal_ConfigTest, LoadAllConfigurations_ShouldLoadCorrectly) 
{
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "CompleteRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "1",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.4.4.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "1000000"
                        }
                    }
                },
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.4.4.2",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "2000000"
                        }
                    }
                }
            ],
            "FastEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "192.168.4.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "speed": {
                            "value": "100"
                        },
                        "duplex": {
                            "value": "full"
                        }
                    }
                }
            ]
        },
        "router": {},
        "line": {},
        "policy-map": {}
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {
            "FastEthernet": [
                "fe-0"
            ],
            "GigabitEthernet": [
                "ge-0",
                "ge-1"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify hostname
    EXPECT_EQ(configs->root["hostname"]["word"], "CompleteRouter");

    // Verify GigabitEthernet Interfaces
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"].size(), 2);
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["id"], "1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"], "10.4.4.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["bandwidth"]["id"], "1000000");

    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][1]["id"], "0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][1]["commands"]["ip"]["address"]["ip"], "10.4.4.2");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][1]["commands"]["bandwidth"]["id"], "2000000");

    // Verify FastEthernet Interfaces
    EXPECT_EQ(configs->root["interface"]["FastEthernet"].size(), 1);
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0]["id"], "0");
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0]["commands"]["ip"]["address"]["ip"], "192.168.4.1");
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0]["commands"]["ip"]["address"]["mask"], "255.255.255.0");
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0]["commands"]["speed"]["value"], "100");
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0]["commands"]["duplex"]["value"], "full");

    // Verify additional configurations
    const auto& fasts = configs->hwManager->getPhysicalInterfaces(InterfaceType::FAST_ETHERNET);
    const auto& gigs = configs->hwManager->getPhysicalInterfaces(InterfaceType::GIGABIT_ETHERNET);
    EXPECT_EQ(configs->hwManager->getPhysicalInterfaces().size(), 2);
    EXPECT_EQ(fasts.size(), 1);
    EXPECT_EQ(gigs.size(), 2);
}

// Test RecoverConfigs with Empty Configurations
TEST_F(Internal_ConfigTest, RecoverConfigs_EmptyConfig_ShouldReturnEmptyList) {
    // Define empty startup configuration
    std::string startupConfig = "{}";

    // Define empty additional Configs.json
    std::string additionalConfig = "{}";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Recover commands
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    // Define expected commands as empty
    std::vector<std::string> expectedCommands = {};

    // Verify recovery commands match
    EXPECT_EQ(recoveryCommands.size(), expectedCommands.size());
}

// Test Re-initializing Configurations After Changes
TEST_F(Internal_ConfigTest, ReInitConfigs_ShouldResetAndLoadNewConfigurations) {
    // Define initial startup configuration
    std::string startupConfig1 = R"(
    {
        "hostname": {
            "word": "InitialRouter"
        },
        "interface": {}
    }
    )";

    // Define initial additional Configs.json
    std::string additionalConfig1 = R"(
    {
        "Interface": {}
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig1);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig1);

    // Initialize configurations
    configs->initConfigs({});

    // Verify initial configurations
    EXPECT_EQ(configs->root["hostname"]["word"], "InitialRouter");

    // Define new startup configuration
    std::string startupConfig2 = R"(
    {
        "hostname": {
            "word": "UpdatedRouter"
        },
        "interface": {}
    }
    )";

    // Define new additional Configs.json
    std::string additionalConfig2 = R"(
    {
        "Interface": {}
    }
    )";

    // Update mock files for re-initialization
    mockFileSystem->setupMockFile(startupFilePath, startupConfig2);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig2);

    // Re-initialize configurations
    configs->initConfigs({});

    // Verify that old configurations are reset and new configurations are loaded
    EXPECT_EQ(configs->root["hostname"]["word"], "UpdatedRouter");
}

// Test No State Leakage Between Tests
TEST_F(Internal_ConfigTest, Test_NoStateLeakage_BetweenTests_ShouldBeIsolated) {
    // Assuming that each test runs independently
    // No actual state leakage needs to be tested since each test uses a fresh fixture
    // But to confirm, run a test that verifies default state

    // Define empty startup configuration
    std::string startupConfig = "{}";

    // Define empty additional Configs.json
    std::string additionalConfig = "{}";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify that all configurations are empty
    EXPECT_TRUE(configs->root.empty());
    EXPECT_TRUE(configs->hwManager->getPhysicalInterfaces().empty());
}

// Test saveConfig writes the correct JSON to file
TEST_F(Internal_ConfigTest, SaveConfig_ShouldWriteCorrectJSONToFile) {
    // Define startup configuration
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "SaveRouter"
        },
        "interface": {}
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Modify some configuration
    configs->root["hostname"]["word"] = "ModifiedRouter";

    // Mock writeFile behavior
    EXPECT_CALL(*mockFileSystem, writeFile(startupFilePath, _))
        .Times(1)
        .WillOnce(Invoke([](const std::string&, const std::string& content) -> bool {
            // For testing, we can check if content contains "ModifiedRouter"
            return content.find("ModifiedRouter") != std::string::npos;
        }));

    // Save configuration
    configs->saveConfig();
}


// Test saveConfig when root is empty
TEST_F(Internal_ConfigTest, SaveConfig_EmptyRoot_ShouldWriteEmptyJSON) {
    // Define empty startup configuration
    std::string startupConfig = "{}";

    // Define empty additional Configs.json
    std::string additionalConfig = "{}";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Mock writeFile behavior
    EXPECT_CALL(*mockFileSystem, writeFile(startupFilePath, _))
        .Times(1)
        .WillOnce(Invoke([](const std::string&, const std::string& content) -> bool {
            return content == "{}";
        }));

    // Save configuration
    configs->saveConfig();
}


// Test Parsing Nested Commands within Interfaces
TEST_F(Internal_ConfigTest, ParseNestedCommands_ShouldLoadNestedCommandsCorrectly) {
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "NestedCommandsRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.10.10.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "routing": {
                            "protocol": "OSPF",
                            "area": "0"
                        }
                    }
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify nested commands
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["routing"]["protocol"], "OSPF");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["routing"]["area"], "0");
}

// Test Parsing Policy Map with Multiple Classes
TEST_F(Internal_ConfigTest, ParsePolicyMapMultipleClasses_ShouldLoadAllClassesCorrectly) {
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "PolicyMapMultipleClassesRouter"
        },
        "interface": {},
        "policy-map": {
            "name": "QoS_Policy1",
            "commands": {
                "class": [
                    {
                        "name": "Voice_Traffic",
                        "commands": {
                            "priority": {
                                "value": "50"
                            },
                            "police": {
                                "value": "50000"
                            }
                        }
                    },
                    {
                        "name": "Data_Traffic",
                        "commands": {
                            "bandwidth": {
                                "value": "100000"
                            },
                            "shape": {
                                "value": "1000000"
                            }
                        }
                    },
                    {
                        "name": "Video_Traffic",
                        "commands": {
                            "bandwidth": {
                                "value": "200000"
                            },
                            "shape": {
                                "value": "2000000"
                            }
                        }
                    }
                ]
            }
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify Policy Map
    EXPECT_EQ(configs->root["policy-map"]["name"], "QoS_Policy1");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"].size(), 3);

    // Verify first class
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][0]["name"], "Voice_Traffic");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][0]["commands"]["priority"]["value"], "50");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][0]["commands"]["police"]["value"], "50000");

    // Verify second class
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][1]["name"], "Data_Traffic");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][1]["commands"]["bandwidth"]["value"], "100000");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][1]["commands"]["shape"]["value"], "1000000");

    // Verify third class
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][2]["name"], "Video_Traffic");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][2]["commands"]["bandwidth"]["value"], "200000");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][2]["commands"]["shape"]["value"], "2000000");
}

// Test Parsing Router Protocol Neighbors
TEST_F(Internal_ConfigTest, ParseRouterProtocolNeighbors_ShouldLoadAllNeighborsCorrectly) {
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "NeighborRouter"
        },
        "interface": {},
        "router": {
            "bgp": [
                {
                    "id": "65000",
                    "neighbor": [ 
                        {
                            "ip": "192.168.100.1",
                            "remote-as": {
                                "num": "65001" 
                            }
                        },
                        {
                            "ip": "192.168.200.1",
                            "remote-as": {
                                "num": "65002" 
                            }
                        }
                    ]
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify BGP Neighbors
    EXPECT_EQ(configs->root["router"]["bgp"].size(), 1);
    EXPECT_EQ(configs->root["router"]["bgp"][0]["id"], "65000");
    EXPECT_EQ(configs->root["router"]["bgp"][0]["neighbor"].size(), 2);
    EXPECT_EQ(configs->root["router"]["bgp"][0]["neighbor"][0]["ip"], "192.168.100.1");
    EXPECT_EQ(configs->root["router"]["bgp"][0]["neighbor"][0]["remote-as"]["num"], "65001");
    EXPECT_EQ(configs->root["router"]["bgp"][0]["neighbor"][1]["ip"], "192.168.200.1");
    EXPECT_EQ(configs->root["router"]["bgp"][0]["neighbor"][1]["remote-as"]["num"], "65002");
}

// Test Parsing Configurations with Nested Security Commands
TEST_F(Internal_ConfigTest, ParseNestedSecurityCommands_ShouldLoadCorrectly) 
{
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "SecurityRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.16.16.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "1000000"
                        },
                        "advanced": {
                            "security": {
                                "firewall": "enabled",
                                "antivirus": "active"
                            }
                        }
                    }
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify nested security commands
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["advanced"]["security"]["firewall"], "enabled");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["advanced"]["security"]["antivirus"], "active");
}

// Test Parsing Configurations with Modes and Exits
TEST_F(Internal_ConfigTest, RecoverConfigs_WithModeAndExits_ShouldRecoverCommandsCorrectly) 
{
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "ModeExitRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "configure": {
                            "commands": {
                                "hostname": {
                                    "value": "ModeExitRouter"
                                },
                                "exit": {}
                            }
                        }
                    }
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Recover commands
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    // Define expected commands
    std::vector<std::string> expectedCommands = {
        "hostname ModeExitRouter",
        "interface GigabitEthernet 0",
        "configure",
        "hostname ModeExitRouter",
        "exit"
    };

    // Verify recovery commands match
    EXPECT_EQ(recoveryCommands.size(), expectedCommands.size());
    for (size_t i = 0; i < expectedCommands.size(); ++i) {
        EXPECT_EQ(recoveryCommands[i], expectedCommands[i]);
    }
}

// Test Initialization with Recovery Commands
TEST_F(Internal_ConfigTest, InitConfigs_WithRecoveryCommands_ShouldLoadRecoveryCommandsCorrectly) 
{
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "RecoveryCommandsRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.8.8.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "100000"
                        }
                    }
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify recovery data
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    EXPECT_EQ(recoveryCommands.size(), 4);
    EXPECT_EQ(recoveryCommands[0], "hostname RecoveryCommandsRouter");
    EXPECT_EQ(recoveryCommands[1], "interface GigabitEthernet 0");
    EXPECT_EQ(recoveryCommands[2], "ip address 10.8.8.1 255.255.255.0");
    EXPECT_EQ(recoveryCommands[3], "bandwidth 100000");
}

// Test RecoverConfigs handles special characters in recovery commands
TEST_F(Internal_ConfigTest, RecoverConfigs_WithSpecialCharacters_ShouldHandleCorrectly) 
{
    std::string startupConfig = R"(
    {
        "recovery": {
            "Reboot#1": {},
            "Shutdown@2": {},
            "FactoryReset$3": {}
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Recover commands
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    // Define expected commands with special characters
    std::vector<std::string> expectedCommands = {
        "recovery Reboot#1",
        "recovery Shutdown@2",
        "recovery FactoryReset$3"
    };

    // Verify recovery commands match
    EXPECT_EQ(recoveryCommands.size(), expectedCommands.size());
    for (size_t i = 0; i < expectedCommands.size(); ++i) {
        EXPECT_EQ(recoveryCommands[i], expectedCommands[i]);
    }
}

// Test RecoverConfigs with Empty Commands
TEST_F(Internal_ConfigTest, RecoverConfigs_WithEmptyCommands_ShouldHandleCorrectly) 
{
    // Define startup configuration with empty commands
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "EmptyCommandsRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {}
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Recover commands
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    // Define expected commands
    std::vector<std::string> expectedCommands = {
        "hostname EmptyCommandsRouter",
        "interface GigabitEthernet 0"
        // No commands inside
    };

    // Verify recovery commands match
    EXPECT_EQ(recoveryCommands.size(), expectedCommands.size());
    for (size_t i = 0; i < expectedCommands.size(); ++i) {
        EXPECT_EQ(recoveryCommands[i], expectedCommands[i]);
    }
}

// Test RecoverConfigs with Multiple Lines and VTY Configurations
TEST_F(Internal_ConfigTest, RecoverConfigs_MultipleLinesAndVTY_ShouldRecoverCorrectly) 
{
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "MultipleLinesRouter"
        },
        "interface": {
            "FastEthernet": [
                {
                    "id": "1",
                    "commands": {
                        "speed": {
                            "value": "100"
                        },
                        "duplex": {
                            "value": "full"
                        }
                    }
                }
            ]
        },
        "line": {
            "console": {
                "number": "0",
                "commands": {
                    "password": {
                        "value": "consolepass"
                    },
                    "login": {}
                }
            },
            "vty": [
                {
                    "id": "0",
                    "commands": {
                        "password": {
                            "value": "vtypassword0"
                        },
                        "login": {},
                        "transport": {
                            "input": {
                                "ssh": {}
                            }
                        }
                    }
                },
                {
                    "id": "1",
                    "commands": {
                        "password": {
                            "value": "vtypassword1"
                        },
                        "login": {},
                        "transport": {
                            "input": {
                                "ssh": {
                                    "telnet": {}
                                }
                            }
                        }
                    }
                },
                {
                    "id": "2",
                    "commands": {
                        "password": {
                            "value": "vtypassword2"
                        },
                        "login": {},
                        "transport": {
                            "input": {
                                "ssh": {
                                    "telnet": {
                                        "https": {}
                                    }
                                }
                            }
                        }
                    }
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Recover commands
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    // Define expected commands
    std::vector<std::string> expectedCommands = {
        "hostname MultipleLinesRouter",
        "interface FastEthernet 1",
        "speed 100",
        "duplex full",
        "line console 0",
        "password consolepass",
        "login",
        "line vty 0",
        "password vtypassword0",
        "login",
        "transport input ssh",
        "line vty 1",
        "password vtypassword1",
        "login",
        "transport input ssh telnet",
        "line vty 2",
        "password vtypassword2",
        "login",
        "transport input ssh telnet https"
    };

    // Verify recovery commands match
    EXPECT_EQ(recoveryCommands.size(), expectedCommands.size());
    for (size_t i = 0; i < expectedCommands.size(); ++i) {
        EXPECT_EQ(recoveryCommands[i], expectedCommands[i]);
    }
}

// Test Initialization with Completely Empty JSON
TEST_F(Internal_ConfigTest, InitConfigs_CompletelyEmptyJSON_ShouldInitializeWithEmptyRoot) 
{
    // Define completely empty startup configuration
    std::string startupConfig = "{}";

    // Define completely empty additional Configs.json
    std::string additionalConfig = "{}";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify that root is empty
    EXPECT_TRUE(configs->root.empty());

    // Verify additional configurations are loaded as empty
    EXPECT_TRUE(configs->hwManager->getPhysicalInterfaces().empty());
}

// Test Parsing Configurations with VLAN and Security Commands
TEST_F(Internal_ConfigTest, RecoverConfigs_WithVLANAndSecurity_ShouldRecoverCorrectly) 
{
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "VLANSecurityRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "1",
                    "commands": {
                        "vlan": [
                            {
                                "id": "100",
                                "name": "Marketing"
                            },
                            {
                                "id": "200",
                                "name": "Engineering"
                            }
                        ],
                        "security": {
                            "firewall": {},
                            "antivirus": {}
                        }
                    }
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Recover commands
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    // Define expected commands
    std::vector<std::string> expectedCommands = {
        "hostname VLANSecurityRouter",
        "interface GigabitEthernet 1",
        "vlan 100 Marketing",
        "vlan 200 Engineering",
        "security firewall",
        "security antivirus"
    };

    // Verify recovery commands match
    EXPECT_EQ(expectedCommands, recoveryCommands);
}

TEST_F(Internal_ConfigTest, InitConfigs_PartialAdditionalConfig_ShouldInitializePartialConfigs) 
{
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "PartialConfigRouter"
        },
        "interface": {}
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    configs->initConfigs({});

    // Verify hostname
    EXPECT_EQ(configs->root["hostname"]["word"], "PartialConfigRouter");

    // Verify partial additional configurations
    const auto& gigs = configs->hwManager->getPhysicalInterfaces(InterfaceType::GIGABIT_ETHERNET);
    EXPECT_EQ(gigs.size(), 1);
}

// Parsing multiple objects at once
TEST_F(Internal_ConfigTest, ParseMultipleVLANs_ShouldLoadAllVLANsCorrectly) 
{
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "VLANRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "vlan": [
                            {
                                "id": "10",
                                "name": "Sales"
                            },
                            {
                                "id": "20",
                                "name": "Engineering"
                            }
                        ]
                    }
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    configs->initConfigs({});

    // Verify VLAN configurations
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"].size(), 1);
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["vlan"].size(), 2);
    
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["vlan"][0]["id"], "10");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["vlan"][0]["name"], "Sales");
    
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["vlan"][1]["id"], "20");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["vlan"][1]["name"], "Engineering");
}

// Expect empty lists to return no commands
TEST_F(Internal_ConfigTest, RecoverConfigs_NoRecoverySection_ShouldReturnEmptyList) 
{
    std::string startupConfig = R"(
    {
        "interface": {
            "GigabitEthernet": []
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    configs->initConfigs({});

    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    EXPECT_TRUE(recoveryCommands.empty());
}

// Test empty commands in configuration
TEST_F(Internal_ConfigTest, RecoverConfigs_PartialRecoveryCommands_ShouldHandleGracefully) 
{
    std::string startupConfig = R"(
    {
        "recovery": {
            "Reboot": {},
            "": {},
            "Shutdown@": {},
            "FactoryReset#1": {}
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    configs->initConfigs({});

    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    std::vector<std::string> expectedCommands = {
        "recovery Reboot",
        "recovery ", // Depending on implementation, this might be skipped or included
        "recovery Shutdown@",
        "recovery FactoryReset#1"
    };

    EXPECT_EQ(recoveryCommands.size(), expectedCommands.size());
    for (size_t i = 0; i < expectedCommands.size(); ++i) {
        EXPECT_EQ(recoveryCommands[i], expectedCommands[i]);
    }
}

// Test complex JSONs with volitile values
TEST_F(Internal_ConfigTest, GetVolatileValue_ComplexJSON_ShouldReturnCorrectValue) 
{
    std::string command = "A.B.C.D";
    std::string com = "10.0.0.1";
    nlohmann::ordered_json currentJson = R"({
        "ip": "10.0.0.1",
        "mask": "255.255.255.0",
        "additional": {
            "details": "value"
        }
    })"_json;

    std::string expectedValue = "ip_2";
    std::string actualValue = getVolatileValue(command, com, currentJson);
    EXPECT_EQ(actualValue, expectedValue);

    // Another example with different value
    com = "255.255.255.0";
    expectedValue = "mask_2";
    actualValue = getVolatileValue(command, com, currentJson);
    EXPECT_EQ(actualValue, expectedValue);
}

// Test saving configuration
TEST_F(Internal_ConfigTest, SaveConfig_ModifiedNestedCommands_ShouldWriteCorrectly) 
{
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "NestedSaveRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.0.0.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "1000000"
                        }
                    }
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    configs->initConfigs({});

    // Modify nested commands
    configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"] = "10.0.0.2";
    configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["mask"] = "255.255.254.0";

    // Mock writeFile behavior
    EXPECT_CALL(*mockFileSystem, writeFile(startupFilePath, _))
        .Times(1)
        .WillOnce(Invoke([&](const std::string&, const std::string& content) -> bool {
            // Parse the content to verify the changes
            json savedJson;
            try {
                savedJson = json::parse(content);
            } catch (...) {
                return false;
            }
            return savedJson["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"] == "10.0.0.2" &&
                   savedJson["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["mask"] == "255.255.254.0";
        }));

    // Save configuration
    configs->saveConfig();
}

// Test resetting the internal state of the router
TEST_F(Internal_ConfigTest, ReInitConfigs_ShouldResetInternalState) 
{
    std::string startupConfig1 = R"(
    {
        "hostname": {
            "value": "FirstRouter"
        },
        "interface": {}
    }
    )";

    std::string additionalConfig1 = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    mockFileSystem->setupMockFile(startupFilePath, startupConfig1);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig1);

    configs->initConfigs({});

    // Verify initial state
    EXPECT_EQ(configs->root["hostname"]["value"], "FirstRouter");
    const auto& gigs = configs->hwManager->getPhysicalInterfaces(InterfaceType::GIGABIT_ETHERNET);
    EXPECT_EQ(gigs.size(), 1);

    // Define new startup configuration
    std::string startupConfig2 = R"(
    {
        "hostname": {
            "value": "SecondRouter"
        },
        "interface": {}
    }
    )";

    std::string additionalConfig2 = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-1"
            ]
        }
    }
    )";

    mockFileSystem->setupMockFile(startupFilePath, startupConfig2);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig2);

    // Re-initialize configurations
    configs->initConfigs({});

    // Verify that old configurations are reset
    EXPECT_EQ(configs->root["hostname"]["value"], "SecondRouter");
    const auto& gigs2 = configs->hwManager->getPhysicalInterfaces(InterfaceType::GIGABIT_ETHERNET);
    EXPECT_EQ(gigs2.size(), 1);
}

// Test command order, make sure order is correct
TEST_F(Internal_ConfigTest, RecoverConfigs_CommandOrder_ShouldRespectDependencies) 
{
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "OrderRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.10.10.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "description": {
                            "value": "Uplink Interface"
                        }
                    }
                }
            ]
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    configs->initConfigs({});

    // Recover commands
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    // Expected order: hostname, interface, ip address, description
    std::vector<std::string> expectedCommands = {
        "hostname OrderRouter",
        "interface GigabitEthernet 0",
        "ip address 10.10.10.1 255.255.255.0",
        "description Uplink Interface"
    };

    EXPECT_EQ(recoveryCommands, expectedCommands);
}

// Test parsing policy maps correctly
TEST_F(Internal_ConfigTest, ParsePolicyMapMultipleClassesNestedCommands_ShouldLoadCorrectly) 
{
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "ComplexPolicyMapRouter"
        },
        "interface": {},
        "policy-map": {
            "name": "Advanced_QoS",
            "commands": {
                "class": [
                    {
                        "name": "Critical_Traffic",
                        "commands": {
                            "priority": {
                                "value": "70"
                            },
                            "police": {
                                "value": "70000"
                            }
                        }
                    },
                    {
                        "name": "Normal_Traffic",
                        "commands": {
                            "bandwidth": {
                                "value": "150000"
                            },
                            "shape": {
                                "value": "1500000"
                            },
                            "queue-limit": {
                                "value": "100"
                            }
                        }
                    },
                    {
                        "name": "Bulk_Traffic",
                        "commands": {
                            "bandwidth": {
                                "value": "300000"
                            },
                            "shape": {
                                "value": "3000000"
                            }
                        }
                    }
                ]
            }
        }
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {}
    )";

    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    configs->initConfigs({});

    // Verify Policy Map
    EXPECT_EQ(configs->root["policy-map"]["name"], "Advanced_QoS");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"].size(), 3);

    // Verify Critical_Traffic Class
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][0]["name"], "Critical_Traffic");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][0]["commands"]["priority"]["value"], "70");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][0]["commands"]["police"]["value"], "70000");

    // Verify Normal_Traffic Class
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][1]["name"], "Normal_Traffic");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][1]["commands"]["bandwidth"]["value"], "150000");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][1]["commands"]["shape"]["value"], "1500000");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][1]["commands"]["queue-limit"]["value"], "100");

    // Verify Bulk_Traffic Class
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][2]["name"], "Bulk_Traffic");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][2]["commands"]["bandwidth"]["value"], "300000");
    EXPECT_EQ(configs->root["policy-map"]["commands"]["class"][2]["commands"]["shape"]["value"], "3000000");
}

// Test loading multiple policy maps correctly
TEST_F(Internal_ConfigTest, ParseMultiplePolicyMaps_ShouldLoadAllPolicyMapsCorrectly) 
{
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "MultiPolicyMapRouter"
        },
        "interface": {},
        "policy-map": [
            {
                "name": "QoS_Policy1",
                "commands": {
                    "class": [
                        {
                            "name": "Voice_Traffic",
                            "commands": {
                                "priority": {
                                    "value": "60"
                                }
                            }
                        }
                    ]
                }
            },
            {
                "name": "QoS_Policy2",
                "commands": {
                    "class": [
                        {
                            "name": "Data_Traffic",
                            "commands": {
                                "bandwidth": {
                                    "value": "200000"
                                }
                            }
                        }
                    ]
                }
            }
        ]
    }
    )";

    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    configs->initConfigs({});

    // Verify both Policy Maps
    EXPECT_EQ(configs->root["policy-map"].size(), 2);

    // Verify first Policy Map
    EXPECT_EQ(configs->root["policy-map"][0]["name"], "QoS_Policy1");
    EXPECT_EQ(configs->root["policy-map"][0]["commands"]["class"].size(), 1);
    EXPECT_EQ(configs->root["policy-map"][0]["commands"]["class"][0]["name"], "Voice_Traffic");
    EXPECT_EQ(configs->root["policy-map"][0]["commands"]["class"][0]["commands"]["priority"]["value"], "60");

    // Verify second Policy Map
    EXPECT_EQ(configs->root["policy-map"][1]["name"], "QoS_Policy2");
    EXPECT_EQ(configs->root["policy-map"][1]["commands"]["class"].size(), 1);
    EXPECT_EQ(configs->root["policy-map"][1]["commands"]["class"][0]["name"], "Data_Traffic");
    EXPECT_EQ(configs->root["policy-map"][1]["commands"]["class"][0]["commands"]["bandwidth"]["value"], "200000");
}

// Test Initialization with Multiple Sub-Commands Sharing the Same Parent Key
TEST_F(Internal_ConfigTest, InitConfigs_MultipleSubCommands_SameParent_ShouldLoadCorrectly) 
{
    // Define startup configuration with 'ip' having both 'address' and 'mtu'
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "DualIPRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.10.10.1",
                                "mask": "255.255.255.0"
                            },
                            "mtu": {
                                "value": "1400"
                            }
                        },
                        "bandwidth": {
                            "id": "1000000"
                        }
                    }
                }
            ]
        }
    }
    )";

    // Define additional Configs.json with matching interface mappings
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify hostname
    EXPECT_EQ(configs->root["hostname"]["word"], "DualIPRouter");

    // Verify GigabitEthernet Interface
    ASSERT_EQ(configs->root["interface"]["GigabitEthernet"].size(), 1);
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["id"], "0");

    // Verify IP Address
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"], "10.10.10.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["mask"], "255.255.255.0");

    // Verify MTU
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["mtu"]["value"], "1400");

    // Verify Bandwidth
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["bandwidth"]["id"], "1000000");

    // Verify additional configurations
    const auto& gigs = configs->hwManager->getPhysicalInterfaces(InterfaceType::GIGABIT_ETHERNET);
    EXPECT_EQ(gigs.size(), 1);
}

// Test Recovery with Multiple Sub-Commands Sharing the Same Parent Key
TEST_F(Internal_ConfigTest, RecoverConfigs_MultipleSubCommands_SameParent_ShouldRecoverAllCommands) 
{
    // Define startup configuration with 'ip' having both 'address' and 'mtu'
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "DualIPRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.10.10.1",
                                "mask": "255.255.255.0"
                            },
                            "mtu": {
                                "value": "1400"
                            }
                        },
                        "bandwidth": {
                            "id": "1000000"
                        }
                    }
                }
            ]
        },
        "recovery": [
            "hostname DualIPRouter",
            "interface GigabitEthernet 1",
            "ip address 10.10.10.1 255.255.255.0",
            "ip mtu 1400",
            "bandwidth 1000000"
        ]
    }
    )";

    // Define additional Configs.json with matching interface mappings
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Recover commands
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    // Define expected recovery commands
    std::vector<std::string> expectedCommands = {
        "hostname DualIPRouter",
        "interface GigabitEthernet 0",
        "ip address 10.10.10.1 255.255.255.0",
        "ip mtu 1400",
        "bandwidth 1000000"
    };

    // Verify recovery commands match expected
    EXPECT_EQ(recoveryCommands.size(), expectedCommands.size());
    for (size_t i = 0; i < expectedCommands.size(); ++i) {
        EXPECT_EQ(recoveryCommands[i], expectedCommands[i]) << "Mismatch at command index " << i;
    }
}

// Test adding duplicate interfaces
TEST_F(Internal_ConfigTest, InitConfigs_AddDuplicateInterfaces_ShouldNestCorrectlyAndOverwriteValues) {
    // Define initial startup configuration with one GigabitEthernet interface
    std::string startupConfig = R"(
    {
        "hostname": {
            "value": "DuplicateInterfaceRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.0.0.1",
                                "mask": "255.255.255.0"
                            },
                            "mtu": {
                                "id": "1500"
                            }
                        }
                    }
                }
            ]
        }
    }
    )";

    // Define additional Configs.json with duplicate interface and additional commands
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations with the initial setup
    configs->initConfigs({});

    // Create new commands to overwrite with
    std::vector<std::vector<std::string>> duplicatePathVectors = 
    {
        {"hostname", "WORD"},
        {"interface", "GigabitEthernet", "<0-9>"},
        {"ip", "address", "A.B.C.D", "A.B.C.D"},
        {"ip", "mtu", "<900-1500>"}
    };

    std::vector<std::vector<std::string>> duplicateCommandVectors =
    {
        {"hostname", "DuplicateInterfaceRouterTest"},
        {"interface", "GigabitEthernet", "0"},
        {"ip", "address", "10.0.0.2", "255.255.254.0"},
        {"ip", "mtu", "1400"}
    };

    // Reset config directory
    modeConfig->configNode = &configs->root;

    // Re-initialize to process the duplicate
    for (size_t i = 0; i < duplicateCommandVectors.size(); ++i)
    {
        bool list = false;
        if (duplicateCommandVectors[i][0] == "interface")
        {
            list = true;
            configs->saveCommand(duplicatePathVectors[i], duplicateCommandVectors[i], *modeConfig, true, false, list);
        }
        else
        {
            configs->saveCommand(duplicatePathVectors[i], duplicateCommandVectors[i], *modeConfig, false, false, list);
        }
    }

    // Verify hostname remains the same
    EXPECT_EQ(configs->root["hostname"]["value"], "DuplicateInterfaceRouterTest");

    // Verify that the existing interface has been updated (nested) correctly
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["id"], "0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"], "10.0.0.2");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["mask"], "255.255.254.0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["mtu"]["id"], "1400");

    // Verify that additional configurations are intact
    const auto& gigs = configs->hwManager->getPhysicalInterfaces(InterfaceType::GIGABIT_ETHERNET);
    EXPECT_EQ(gigs.size(), 1);
}

// Test adding multiple duplicates across different interfaces
TEST_F(Internal_ConfigTest, InitConfigs_MultipleDuplicateInterfaces_ShouldHandleEachCorrectly) 
{
    // Define initial startup configuration with two GigabitEthernet interfaces
    std::string startupConfig = R"(
    {
        "hostname": {
            "value": "MultiDuplicateRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.1.1.1",
                                "mask": "255.255.255.0"
                            },
                            "mtu": {
                                "id": "1500"
                            }
                        },
                        "bandwidth": {
                            "id": "10000"
                        }
                    }
                },
                {
                    "id": "1",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.1.2.1",
                                "mask": "255.255.255.0"
                            },
                            "mtu": {
                                "id": "1500"
                            }
                        }
                    }
                }
            ]
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0",
                "ge-1"
            ]
        }
    }
    )";

    // Set up initial mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations with initial setup
    configs->initConfigs({});

    // Create new commands to overwrite with
    std::vector<std::vector<std::string>> duplicatePathVectors = 
    {
        {"hostname", "WORD"},
        {"interface", "GigabitEthernet", "<0-9>"},
        {"ip", "address", "A.B.C.D", "A.B.C.D"},
        {"ip", "mtu", "<900-1500>"},
        {"bandwidth", "<10000-1000000>"},
        {"exit"},
        {"interface", "GigabitEthernet", "<0-9>"},
        {"ip", "address", "A.B.C.D", "A.B.C.D"},
        {"ip", "mtu", "<900-1500>"}
    };

    std::vector<std::vector<std::string>> duplicateCommandVectors =
    {
        {"hostname", "MultiDuplicateRouter"},
        {"interface", "GigabitEthernet", "0"},
        {"ip", "address", "10.1.1.2", "255.255.254.0"},
        {"ip", "mtu", "1400"},
        {"bandwidth", "100000"},
        {"exit"},
        {"interface", "GigabitEthernet", "1"},
        {"ip", "address", "10.1.3.1", "255.255.255.0"},
        {"ip", "mtu", "1500"}
    };

    // Reset config directory to root
    modeConfig->configNode = &configs->root;

    // Re-initialize to process the duplicate
    for (size_t i = 0; i < duplicateCommandVectors.size(); ++i)
    {
        bool list = false;
        if (duplicateCommandVectors[i][0] == "interface")
        {
            list = true;
            configs->saveCommand(duplicatePathVectors[i], duplicateCommandVectors[i], *modeConfig, true, false, list);
        }
        else if (duplicateCommandVectors[i][0] == "exit")
        {
            configs->saveCommand(duplicatePathVectors[i], duplicateCommandVectors[i], *modeConfig, true, true, list);
        }
        else
        {
            configs->saveCommand(duplicatePathVectors[i], duplicateCommandVectors[i], *modeConfig, false, false, list);
        }
    }

    // Verify hostname remains the same
    EXPECT_EQ(configs->root["hostname"]["value"], "MultiDuplicateRouter");

    // Verify three GigabitEthernet interfaces exist
    ASSERT_EQ(configs->root["interface"]["GigabitEthernet"].size(), 2);

    // Verify interface 1 has been updated
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["id"], "0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"], "10.1.1.2");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["mask"], "255.255.254.0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["mtu"]["id"], "1400");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["bandwidth"]["id"], "100000");

    // Verify interface 2 remains unchanged
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][1]["id"], "1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][1]["commands"]["ip"]["address"]["ip"], "10.1.3.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][1]["commands"]["ip"]["address"]["mask"], "255.255.255.0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][1]["commands"]["ip"]["mtu"]["id"], "1500");
}

// Test saving configuration after modifications using processConfigs
TEST_F(Internal_ConfigTest, ProcessConfigs_SaveAfterModification_ShouldPersistChanges) 
{
    // Define initial startup configuration
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "SaveRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.3.3.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "10000"
                        }
                    }
                }
            ]
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Modify the configuration: Update IP address and MTU
    configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"] = "10.3.3.2";
    configs->root["interface"]["GigabitEthernet"][0]["commands"]["bandwidth"]["id"] = "100000";

    // Mock the writeFile function to verify that the updated configuration is written correctly
    EXPECT_CALL(*mockFileSystem, writeFile(startupFilePath, _))
        .Times(1)
        .WillOnce(Invoke([&](const std::string& /*path*/, const std::string& content) -> bool {
            // Parse the content to verify updates
            json savedJson;
            try {
                savedJson = json::parse(content);
            } catch (...) {
                return false;
            }

            // Verify updates
            return savedJson["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"] == "10.3.3.2" &&
                   savedJson["interface"]["GigabitEthernet"][0]["commands"]["bandwidth"]["id"] == "100000";
        }));

    // Call processConfigs to save the configuration
    bool saveResult = configs->saveConfig();

    // Verify that save was successful
    EXPECT_TRUE(saveResult);
}

// Test saving multiple configuration modifications using processConfigs
TEST_F(Internal_ConfigTest, ProcessConfigs_SaveMultipleModifications_ShouldPersistAllChanges) 
{
    // Define initial startup configuration
    std::string startupConfig = R"(
    {
        "hostname": {
            "value": "MultiModRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.4.4.1",
                                "mask": "255.255.255.0"
                            }
                        },
                        "bandwidth": {
                            "id": "10000"
                        }
                    }
                }
            ]
        },
        "line": {
            "console": {
                "number": "0",
                "commands": {
                    "password": {
                        "value": "initialPass"
                    },
                    "login": {}
                }
            }
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Modify the configuration: Update IP address, MTU, and console password
    configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"] = "10.4.4.2";
    configs->root["interface"]["GigabitEthernet"][0]["commands"]["bandwidth"]["id"] = "100000";
    configs->root["line"]["console"]["commands"]["password"]["value"] = "newSecurePass";

    // Mock the writeFile function to verify that the updated configuration is written correctly
    EXPECT_CALL(*mockFileSystem, writeFile(startupFilePath, _))
        .Times(1)
        .WillOnce(Invoke([&](const std::string& /*path*/, const std::string& content) -> bool {
            // Parse the content to verify updates
            json savedJson;
            try {
                savedJson = json::parse(content);
            } catch (...) {
                return false;
            }

            // Verify interface updates
            bool interfaceUpdated = savedJson["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"] == "10.4.4.2" &&
                                     savedJson["interface"]["GigabitEthernet"][0]["commands"]["bandwidth"]["id"] == "100000";

            // Verify line console password update
            bool lineUpdated = savedJson["line"]["console"]["commands"]["password"]["value"] == "newSecurePass";

            return interfaceUpdated && lineUpdated;
        }));

    // Call processConfigs to save the configuration
    bool saveResult = configs->saveConfig();

    // Verify that save was successful
    EXPECT_TRUE(saveResult);
}

// Test handling of save failures in processConfigs
TEST_F(Internal_ConfigTest, ProcessConfigs_SaveFailure_ShouldReturnFalse) 
{
    // Define startup configuration
    std::string startupConfig = R"(
    {
        "hostname": {
            "value": "FailureRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.5.5.1",
                                "mask": "255.255.255.0"
                            }
                            "mtu": {
                                "value": "1500"
                            }
                        }
                    }
                }
            ]
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Modify the configuration: Update IP address
    configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"] = "10.5.5.2";

    // Mock the writeFile function to simulate a failure
    EXPECT_CALL(*mockFileSystem, writeFile(startupFilePath, _))
        .Times(1)
        .WillOnce(Return(false)); // Simulate write failure

    // Call processConfigs to attempt saving the configuration
    bool saveResult = configs->saveConfig();

    // Verify that save failed
    EXPECT_FALSE(saveResult);
}

// Test idempotency of processConfigs when no changes are made
TEST_F(Internal_ConfigTest, ProcessConfigs_Idempotent_WhenNoChanges_ShouldSaveConsistently) 
{
    // Define startup configuration
    std::string startupConfig = R"(
    {
        "hostname": {
            "value": "IdempotentRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.6.6.1",
                                "mask": "255.255.255.0"
                            },
                            "mtu": {
                                "id": "1500"
                            }
                        }
                    }
                }
            ]
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Mock the writeFile function to verify it's called correctly each time
    EXPECT_CALL(*mockFileSystem, writeFile(startupFilePath, _))
        .Times(2) // Expect two save operations
        .WillRepeatedly(Invoke([&](const std::string& /*path*/, const std::string& content) -> bool {
            // Parse the content to verify it matches the initial configuration
            json savedJson;
            try {
                savedJson = json::parse(content);
            } catch (...) {
                return false;
            }

            return savedJson["hostname"]["value"] == "IdempotentRouter" &&
                   savedJson["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"] == "10.6.6.1" &&
                   savedJson["interface"]["GigabitEthernet"][0]["commands"]["ip"]["mtu"]["id"] == "1500";
        }));

    // Call processConfigs twice without making any changes
    bool firstSave = configs->saveConfig();
    bool secondSave = configs->saveConfig();

    // Verify both saves were successful
    EXPECT_TRUE(firstSave);
    EXPECT_TRUE(secondSave);
}

// Test handling of completely empty configuration
TEST_F(Internal_ConfigTest, InitConfigs_EmptyConfiguration_ShouldInitializeWithDefaults) 
{
    // Define an empty startup configuration
    std::string startupConfig = "{}";

    // Define an empty additional Configs.json
    std::string additionalConfig = "{}";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify that root is empty or contains default values
    EXPECT_TRUE(configs->root.empty());

    // Verify that additional configurations are empty
    EXPECT_TRUE(configs->hwManager->getPhysicalInterfaces().empty());
}

// Test deleting an interface and saving the configuration
TEST_F(Internal_ConfigTest, ProcessConfigs_DeleteInterface_ShouldRemoveInterfaceAndSaveCorrectly) 
{
    // Define initial startup configuration with two interfaces
//     std::string startupConfig = R"(
//     {
//         "hostname": {
//             "word": "DeleteInterfaceRouter"
//         },
//         "interface": {
//             "GigabitEthernet": [
//                 {
//                     "id": "1",
//                     "commands": {
//                         "ip": {
//                             "address": {
//                                 "ip": "10.8.8.1",
//                                 "mask": "255.255.255.0"
//                             }
//                         },
//                         "mtu": {
//                             "value": "1500"
//                         }
//                     }
//                 },
//                 {
//                     "id": "2",
//                     "commands": {
//                         "ip": {
//                             "address": {
//                                 "ip": "10.8.8.2",
//                                 "mask": "255.255.255.0"
//                             }
//                         },
//                         "mtu": {
//                             "value": "1500"
//                         }
//                     }
//                 }
//             ]
//         }
//     }
//     )";

    // Define additional Configs.json
//     std::string additionalConfig = R"(
//     {
//         "Interface": {
//             "PORT_0": "ge-0/0/1",
//             "PORT_1": "ge-0/0/2"
//         },
//         "Mac": {
//             "OUI": "AA1122",
//             "GigabitEthernet": {
//                 "1": "AAA111",
//                 "2": "BBB222"
//             }
//         }
//     }
//     )";

    // Define additional Configs.json after deletion
//     std::string additionalConfigAfterDeletion = R"(
//     {
//         "Interface": {
//             "PORT_0": "ge-0/0/1"
//         },
//         "Mac": {
//             "OUI": "AA1122",
//             "GigabitEthernet": {
//                 "1": "AAA111"
//             }
//         }
//     }
//     )";

    // Set up initial mock files
//     mockFileSystem->setupMockFile(startupFilePath, startupConfig);
//     mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
//     configs->initConfigs(startupFilePath);

    // Delete interface with id "2"
    // Assuming there's a method to delete interfaces, e.g., deleteInterface("2")
    // If not, you can manipulate the JSON directly for the purpose of the test
//     configs->root["interface"]["GigabitEthernet"].erase(
//         std::remove_if(
//             configs->root["interface"]["GigabitEthernet"].begin(),
//             configs->root["interface"]["GigabitEthernet"].end(),
//             [](const json& iface) { return iface["id"] == "2"; }
//         ),
//         configs->root["interface"]["GigabitEthernet"].end()
//     );

    // Update additional configurations accordingly
//     mockFileSystem->setupMockFile(additionalConfigPath, additionalConfigAfterDeletion);

    // Mock the writeFile function to verify that the interface has been deleted
//     EXPECT_CALL(*mockFileSystem, writeFile(startupFilePath, _))
//         .Times(1)
//         .WillOnce(Invoke([&](const std::string& /*path*/, const std::string& content) -> bool {
            // Parse the content to verify that interface "2" is removed
//             json savedJson;
//             try {
//                 savedJson = json::parse(content);
//             } catch (...) {
//                 return false;
//             }

            // Verify hostname
//             bool hostnameCorrect = savedJson["hostname"]["word"] == "DeleteInterfaceRouter";

            // Verify only one interface remains
//             bool singleInterface = savedJson["interface"]["GigabitEthernet"].size() == 1 &&
//                                     savedJson["interface"]["GigabitEthernet"][0]["id"] == "1";

//             return hostnameCorrect && singleInterface;
//         }));

    // Call processConfigs to save the configuration after deletion
//     bool saveResult = configs->saveConfig();

    // Verify that save was successful
//     EXPECT_TRUE(saveResult);
}

// Test adding a new GigabitEthernet interface
TEST_F(Internal_ConfigTest, SaveCommand_AddNewGigabitEthernetInterface_ShouldCreateInterface) 
{
    // Define initial startup configuration without any interfaces
    std::string startupConfig = R"(
    {
        "hostname": {
            "value": "NewInterfaceRouter"
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations with the initial setup
    configs->initConfigs({});

    // Define the path and command to add a new GigabitEthernet interface with ID 1
    std::vector<std::vector<std::string>> path = {
        {"interface", "GigabitEthernet", "<0-9>"},
        {"ip", "address", "A.B.C.D", "A.B.C.D"},
        {"ip", "mtu", "<900-1500>"}
    };

    std::vector<std::vector<std::string>> command = {
        {"interface", "GigabitEthernet", "1"},
        {"ip", "address", "10.0.0.1", "255.255.255.0"},
        {"ip", "mtu", "1500"}
    };

    // Reset config directory
    modeConfig->configNode = &configs->root;

    // Re-initialize to process the new interface
    for (size_t i = 0; i < command.size(); ++i)
    {
        if (command[i][0] == "interface")
        {
            configs->saveCommand(path[i], command[i], *modeConfig, true, false, true);
        }
        else
        {
            configs->saveCommand(path[i], command[i], *modeConfig, false, false, false);
        }
    }

    // Verify that the new GigabitEthernet interface has been added correctly
    ASSERT_EQ(configs->root["interface"]["GigabitEthernet"].size(), 1);
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["id"], "1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"], "10.0.0.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["mask"], "255.255.255.0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["mtu"]["id"], "1500");

    // Verify that additional configurations are intact
    const auto& gigs = configs->hwManager->getPhysicalInterfaces(InterfaceType::GIGABIT_ETHERNET);
    EXPECT_EQ(gigs.size(), 1);
}

// Test updating an existing GigabitEthernet interface's IP address
TEST_F(Internal_ConfigTest, SaveCommand_UpdateGigabitEthernetIP_ShouldModifyIPAddressOnly) {
    // Define initial startup configuration with one GigabitEthernet interface
    std::string startupConfig = R"(
    {
        "hostname": {
            "value": "UpdateIPRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "10.0.0.1",
                                "mask": "255.255.255.0"
                            },
                            "mtu": {
                                "id": "1500"
                            }
                        }
                    }
                }
            ]
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations with the initial setup
    configs->initConfigs({});

    // Define the path and command to update the IP address of GigabitEthernet interface 1
    std::vector<std::vector<std::string>> path = {
        {"interface", "GigabitEthernet", "<0-9>"},
        {"ip", "address", "A.B.C.D", "A.B.C.D"},
        {"ip", "mtu", "<900-1500>"}
    };

    std::vector<std::vector<std::string>> command = {
        {"interface", "GigabitEthernet", "0"},
        {"ip", "address", "10.0.0.2", "255.255.254.0"}, // Updated IP
        {"ip", "mtu", "1400"} // Updated MTU
    };

    // Reset config directory
    modeConfig->configNode = &configs->root;

    // Re-initialize to process the updates
    for (size_t i = 0; i < command.size(); ++i)
    {
        if (command[i][0] == "interface")
        {
            configs->saveCommand(path[i], command[i], *modeConfig, true, false, true);
        }
        else
        {
            configs->saveCommand(path[i], command[i], *modeConfig, false, false, false);
        }
    }

    // Verify that the IP address has been updated correctly
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"], "10.0.0.2");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["mask"], "255.255.254.0");

    // Verify that the MTU has been updated correctly
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["mtu"]["id"], "1400");

    // Verify that other configurations remain intact
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["id"], "0");
    const auto& gigs = configs->hwManager->getPhysicalInterfaces(InterfaceType::GIGABIT_ETHERNET);
    EXPECT_EQ(gigs.size(), 1);
}

// Test adding a nested command (e.g., enabling DHCP) under an existing parent
TEST_F(Internal_ConfigTest, SaveCommand_AddNestedCommand_ShouldAddDHCPUnderIP) {
    // Define initial startup configuration with one GigabitEthernet interface
    std::string startupConfig = R"(
    {
        "hostname": {
            "value": "NestedCommandRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "ip": {
                            "address": {
                                "ip": "192.168.1.1",
                                "mask": "255.255.255.0"
                            }
                        }
                    }
                }
            ]
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations with the initial setup
    configs->initConfigs({});

    // Define the path and command to add DHCP under the existing IP configuration
    std::vector<std::vector<std::string>> path = {
        {"interface", "GigabitEthernet", "<0-9>"},
        {"ip", "address", "dhcp"}
    };

    std::vector<std::vector<std::string>> command = {
        {"interface", "GigabitEthernet", "0"},
        {"ip", "address", "dhcp"} // Enable DHCP
    };

    // Reset config directory
    modeConfig->configNode = &configs->root;

    // Re-initialize to process the nested command
    for (size_t i = 0; i < command.size(); ++i)
    {
        if (command[i][0] == "interface")
        {
            configs->saveCommand(path[i], command[i], *modeConfig, true, false, true);
        }
        else
        {
            configs->saveCommand(path[i], command[i], *modeConfig, false, false, false);
        }
    }

    // Verify that DHCP has been added under the IP configuration
    EXPECT_TRUE(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"].contains("dhcp"));

    // Verify that existing IP address remains unchanged
    EXPECT_FALSE(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"].contains("ip"));
    EXPECT_FALSE(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"].contains("mask"));
}

// Test handling of volatile commands (e.g., adding multiple VLANs)
TEST_F(Internal_ConfigTest, SaveCommand_HandleVolatileCommands_ShouldAddMultipleVLANs) {
    // Define initial startup configuration without any VLANs
    std::string startupConfig = R"(
    {
        "hostname": {
            "value": "VLANRouter"
        },
        "interface": {
            "GigabitEthernet": []
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations with the initial setup
    configs->initConfigs({});

    // Define the paths and commands to add VLANs 10 and 20
    std::vector<std::vector<std::string>> path = {
        {"vlan", "<0-4094>"},
        {"vlan", "<0-4094>"}
    };

    std::vector<std::vector<std::string>> command = {
        {"vlan", "10"},
        {"vlan", "20"}
    };

    // Reset config directory
    modeConfig->configNode = &configs->root;

    // Re-initialize to process the VLAN additions
    for (size_t i = 0; i < command.size(); ++i)
    {
        if (command[i][0] == "vlan")
        {
            configs->saveCommand(path[i], command[i], *modeConfig, false, false, true);
        }
        else
        {
            configs->saveCommand(path[i], command[i], *modeConfig, false, false, false);
        }
    }

    // Verify that both VLANs have been added
    ASSERT_EQ(configs->root["vlan"].size(), 2);
    EXPECT_EQ(configs->root["vlan"][0]["id"], "10");
    EXPECT_EQ(configs->root["vlan"][1]["id"], "20");
}

// Test adding multiple commands in a single call (e.g., setting hostname and adding an interface)
TEST_F(Internal_ConfigTest, SaveCommand_AddMultipleCommandsInSingleCall_ShouldProcessAllCommands) {
    // Define initial startup configuration without hostname and interfaces
    std::string startupConfig = R"(
    {
        "interface": {}
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations with the initial setup
    configs->initConfigs({});

    // Define the paths and commands to set hostname and add a new interface
    std::vector<std::vector<std::string>> path = {
        {"hostname", "WORD"},
        {"interface", "GigabitEthernet", "<0-9>"},
        {"ip", "address", "A.B.C.D", "A.B.C.D"},
        {"ip", "mtu", "<900-1500>"}
    };

    std::vector<std::vector<std::string>> command = {
        {"hostname", "MultiCommandRouter"},
        {"interface", "GigabitEthernet", "2"},
        {"ip", "address", "192.168.2.1", "255.255.255.0"},
        {"ip", "mtu", "1400"}
    };

    // Reset config directory
    modeConfig->configNode = &configs->root;

    // Re-initialize to process the commands
    for (size_t i = 0; i < command.size(); ++i)
    {
        if (command[i][0] == "interface")
        {
            configs->saveCommand(path[i], command[i], *modeConfig, true, false, true);
        }
        else
        {
            configs->saveCommand(path[i], command[i], *modeConfig, false, false, false);
        }
    }

    // Verify that the hostname has been set correctly
    EXPECT_EQ(configs->root["hostname"]["value"], "MultiCommandRouter");

    // Verify that the new GigabitEthernet interface has been added correctly
    ASSERT_EQ(configs->root["interface"]["GigabitEthernet"].size(), 1);
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["id"], "2");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"], "192.168.2.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["mask"], "255.255.255.0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["mtu"]["id"], "1400");
}

// Test adding commands to multiple GigabitEthernet interfaces and testing exit
TEST_F(Internal_ConfigTest, SaveCommand_AddCommandsToMultipleInterfaces_ShouldHandleEachCorrectly) {
    // Define initial startup configuration with two GigabitEthernet interfaces
    std::string startupConfig = R"(
    {
        "hostname": {
            "value": "MultiInterfaceRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {}
                },
                {
                    "id": "1",
                    "commands": {}
                }
            ]
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0",
                "ge-1"
            ]
        }
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations with the initial setup
    configs->initConfigs({});

    // Define the paths and commands for both interfaces
    std::vector<std::vector<std::string>> path = {
        {"interface", "GigabitEthernet", "<0-9>"},
        {"ip", "address", "A.B.C.D", "A.B.C.D"},
        {"ip", "mtu", "<900-1500>"},
        {"exit"},
        {"interface", "GigabitEthernet", "<0-9>"},
        {"ip", "address", "A.B.C.D", "A.B.C.D"},
        {"ip", "mtu", "<900-1500>"},
    };

    std::vector<std::vector<std::string>> command = {
        {"interface", "GigabitEthernet", "0"},
        {"ip", "address", "10.0.1.1", "255.255.255.0"},
        {"ip", "mtu", "1400"},
        {"exit"},
        {"interface", "GigabitEthernet", "1"},
        {"ip", "address", "10.0.2.1", "255.255.255.0"},
        {"ip", "mtu", "1500"}
    };

    // Reset config directory
    modeConfig->configNode = &configs->root;

    // Re-initialize to process the commands for both interfaces
    for (size_t i = 0; i < command.size(); ++i)
    {
        if (command[i][0] == "interface")
        {
            configs->saveCommand(path[i], command[i], *modeConfig, true, false, true);
        }
        else if (command[i][0] == "exit")
        {
            configs->saveCommand(path[i], command[i], *modeConfig, true, true, false);
        }
        else
        {
            configs->saveCommand(path[i], command[i], *modeConfig, false, false, false);
        }
    }

    // Verify interface 1 updates
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"], "10.0.1.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["mask"], "255.255.255.0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["mtu"]["id"], "1400");

    // Verify interface 2 updates
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][1]["commands"]["ip"]["address"]["ip"], "10.0.2.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][1]["commands"]["ip"]["address"]["mask"], "255.255.255.0");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][1]["commands"]["ip"]["mtu"]["id"], "1500");
}

// Test adding commands with special characters and spaces in command names and values
TEST_F(Internal_ConfigTest, SaveCommand_AddCommandsWithSpecialCharsAndSpaces_ShouldHandleCorrectly) 
{
    // Define initial startup configuration with one GigabitEthernet interface
    std::string startupConfig = R"(
    {
        "hostname": {
            "value": "SpecialCharRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "1",
                    "commands": {}
                }
            ]
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {}
    }
    )";

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Define the path and commands with special characters and spaces
    std::vector<std::vector<std::string>> path = {
        {"interface", "GigabitEthernet", "<0-9>"},
        {"description", "LINE"},
        {"shutdown"}
    };

    std::vector<std::vector<std::string>> command = {
        {"interface", "GigabitEthernet", "1"},
        {"description", "Uplink @ Main Office!"},
        {"shutdown"}
    };

    // Reset config directory
    modeConfig->configNode = &configs->root;

    // Re-initialize to process the commands
    for (size_t i = 0; i < command.size(); ++i)
    {
        if (command[i][0] == "interface")
        {
            configs->saveCommand(path[i], command[i], *modeConfig, true, false, true);
        }
        else
        {
            configs->saveCommand(path[i], command[i], *modeConfig, false, false, false);
        }
    }

    // Verify that the 'description' command has been added correctly
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["description"]["value"], "Uplink @ Main Office!");

    // Verify that the 'shutdown-status' command has been added correctly
    EXPECT_TRUE(configs->root["interface"]["GigabitEthernet"][0]["commands"].contains("shutdown"));
}

// Test outputting saving very large amounts of data
TEST_F(Internal_ConfigTest, InitConfigs_ExtremelyLargeNumberOfInterfaces_ShouldLoadAllInterfaces) 
{
    const int numGigabitInterfaces = 1000; // Adjust as needed for "extremely large"
    const int numFastEthernetInterfaces = 1000;

    // Generate a large number of GigabitEthernet interfaces
    nlohmann::ordered_json startupConfig = {
        {"hostname", {{"value", "LargeInterfaceRouter"}}},
        {"interface", {
            {"GigabitEthernet", nlohmann::ordered_json::array()},
            {"FastEthernet", nlohmann::ordered_json::array()}
        }}
    };

    // Generate a large number of GigabitEthernet interfaces
    for (int i = 1; i <= numGigabitInterfaces; ++ i)
    {
        startupConfig["interface"]["GigabitEthernet"].push_back({
            {"id", std::to_string(i)},
            {"commands", {
                {"ip", {{"address", {{"ip", "10.0." + std::to_string(i) + ".1"}, {"mask", "255.255.255.0"}}}}},
                {"bandwidth", {{"id", std::to_string(100000 + i)}}}
            }}
        });
    }

    // Generate a large number of FastEthernet interfaces
    for (int i = 1; i <= numGigabitInterfaces; ++ i)
    {
        startupConfig["interface"]["FastEthernet"].push_back({
            {"id", std::to_string(i)},
            {"commands", {
                {"ip", {{"address", {{"ip", "192.168." + std::to_string(i) + ".1"}, {"mask", "255.255.255.0"}}}}},
                {"bandwidth", {{"id", std::to_string(1000 + i)}}}
            }}
        });
    }

    mockFileSystem->setupMockFile(startupFilePath, startupConfig.dump(4));
    mockFileSystem->setupMockFile(additionalConfigPath, "{}");

    configs->initConfigs({});

    // Verify interface counts
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"].size(), numGigabitInterfaces);
    EXPECT_EQ(configs->root["interface"]["FastEthernet"].size(), numFastEthernetInterfaces);

    // Verify specific interface details
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["id"], "1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["ip"]["address"]["ip"], "10.0.1.1");
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0]["id"], "1");
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][0]["commands"]["ip"]["address"]["ip"], "192.168.1.1");

    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][numGigabitInterfaces - 1]["id"], std::to_string(numGigabitInterfaces));
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][numGigabitInterfaces - 1]["commands"]["ip"]["address"]["ip"], "10.0." + std::to_string(numGigabitInterfaces) + ".1");
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][numFastEthernetInterfaces - 1]["id"], std::to_string(numGigabitInterfaces));
    EXPECT_EQ(configs->root["interface"]["FastEthernet"][numFastEthernetInterfaces - 1]["commands"]["ip"]["address"]["ip"], "192.168." + std::to_string(numFastEthernetInterfaces) +".1");
}

// Test Extremely large number of interfaces
TEST_F(Internal_ConfigTest, RecoverConfigs_ExtremelyLargeNumberOfInterfaces_ShouldRecoverAllCommands) 
{
    const int numInterfaces = 1000; // Adjust as needed for "extremely large"

    // Combine interfaces into the startup configuration
    nlohmann::ordered_json startupConfig = {
        {"hostname", {{"word", "RecoverLargeRouter"}}},
        {"interface", {{"GigabitEthernet", nlohmann::ordered_json::array()}}}
    };

    // Generate a large number of GigabitEthernet interfaces
    for (size_t i = 1; i <= numInterfaces; ++i)
    {
        startupConfig["interface"]["GigabitEthernet"].push_back({
            {"id", std::to_string(i)},
            {"commands", {
                {"ip", {{"address", {{"ip", "10.0." + std::to_string(i) + ".1"}, {"mask", "255.255.255.0"}}}}},
                {"bandwidth", {{"id", std::to_string(100000 + i)}}}
            }}
        });
    }

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig.dump(4));
    mockFileSystem->setupMockFile(additionalConfigPath, {});

    // Initialize configurations
    configs->initConfigs({});

    // Recover commands
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    // Expected number of commands:
    // 1 hostname command
    // For each interface:
    //   1 interface command
    //   1 ip address command
    //   1 bandwidth command
    // Total = 1 + 3 * numInterfaces

    EXPECT_EQ(recoveryCommands.size(), 1 + 3 * numInterfaces);

    // Verify the first command is the hostname
    EXPECT_EQ(recoveryCommands[0], "hostname RecoverLargeRouter");

    // Verify the first interface commands
    EXPECT_EQ(recoveryCommands[1], "interface GigabitEthernet 1");
    EXPECT_EQ(recoveryCommands[2], "ip address 10.0.1.1 255.255.255.0");
    EXPECT_EQ(recoveryCommands[3], "bandwidth 100001");

    // Verify the last interface commands
    size_t lastInterfaceIndex = 1 + 3 * numInterfaces - 3;
    EXPECT_EQ(recoveryCommands[lastInterfaceIndex], "interface GigabitEthernet " + std::to_string(numInterfaces));
    EXPECT_EQ(recoveryCommands[lastInterfaceIndex + 1], "ip address 10.0." + std::to_string(numInterfaces) + ".1 255.255.255.0");
    EXPECT_EQ(recoveryCommands[lastInterfaceIndex + 2], "bandwidth " + std::to_string(100000 + numInterfaces));
}

// Test saving large ammounts of policy maps
TEST_F(Internal_ConfigTest, InitConfigs_ExtremelyLargePolicyMap_ShouldLoadAllClasses) 
{
    const int numClasses = 1000; // Adjust as needed for "extremely large"

    // Generate a large number of classes within a policy map
    nlohmann::ordered_json policyMap = {
        {"name", "Advanced_QoS"},
        {"commands", {{"class", nlohmann::ordered_json::array()}}}
    };

    for (size_t i = 1; i <= numClasses; ++i)
    {
        policyMap["commands"]["class"].push_back({
            {"name", "Class_" + std::to_string(i)},
            {"commands", {
                {"bandwidth", {{"value", std::to_string(100000 + i)}}},
                {"shape", {{"value", std::to_string(1000000 + i * 1000)}}}
            }}
        });
    }

    // Combine into startup configuration
    nlohmann::ordered_json startupConfig = {
        {"hostname", {{"value", "LargePolicyMapRouter"}}},
        {"policy-map", nlohmann::ordered_json::array()}
    };
    startupConfig["policy-map"].push_back(policyMap);

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig.dump(4));
    mockFileSystem->setupMockFile(additionalConfigPath, "{}");

    // Initialize configurations
    configs->initConfigs({});

    // Verify the number of classes loaded
    EXPECT_EQ(configs->root["policy-map"].size(), 1);
    EXPECT_EQ(configs->root["policy-map"][0]["commands"]["class"].size(), numClasses);

    // Optionally, verify a few random classes for correctness
    EXPECT_EQ(configs->root["policy-map"][0]["commands"]["class"][0]["name"], "Class_1");
    EXPECT_EQ(configs->root["policy-map"][0]["commands"]["class"][0]["commands"]["bandwidth"]["value"], "100001");
    EXPECT_EQ(configs->root["policy-map"][0]["commands"]["class"][999]["name"], "Class_1000");
    EXPECT_EQ(configs->root["policy-map"][0]["commands"]["class"][999]["commands"]["shape"]["value"], "2000000");
}

// Test Recovering Large Policy Maps
TEST_F(Internal_ConfigTest, RecoverConfigs_ExtremelyLargePolicyMap_ShouldRecoverAllClasses) 
{
    const int numClasses = 1000; // Adjust as needed for "extremely large"

    // Generate a large number of classes within a policy map
    nlohmann::ordered_json policyMap = {
        {"name", "Advanced_QoS"},
        {"commands", {{"class", nlohmann::ordered_json::array()}}}
    };

    for (size_t i = 1; i <= numClasses; ++i)
    {
        policyMap["commands"]["class"].push_back({
            {"name", "Class_" + std::to_string(i)},
            {"commands", {
                {"bandwidth", {{"value", std::to_string(100000 + i)}}},
                {"shape", {{"value", std::to_string(1000000 + i * 1000)}}}
            }}
        });
    }

    // Combine into startup configuration
    nlohmann::ordered_json startupConfig = {
        {"hostname", {{"value", "RecoverLargePolicyMapRouter"}}},
        {"policy-map", nlohmann::ordered_json::array()}
    };
    startupConfig["policy-map"].push_back(policyMap);

    // Set up mock files
    mockFileSystem->setupMockFile(startupFilePath, startupConfig.dump(4));
    mockFileSystem->setupMockFile(additionalConfigPath, "{}");

    // Initialize configurations
    configs->initConfigs({});

    // Recover commands
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    EXPECT_EQ(recoveryCommands.size(), 2 + 3 * numClasses);

    // Verify the first two commands
    EXPECT_EQ(recoveryCommands[0], "hostname RecoverLargePolicyMapRouter");
    EXPECT_EQ(recoveryCommands[1], "policy-map Advanced_QoS");
    EXPECT_EQ(recoveryCommands[2], "class Class_1");
    EXPECT_EQ(recoveryCommands[3], "bandwidth 100001");
    EXPECT_EQ(recoveryCommands[4], "shape 1001000");

    // Verify the last class commands
    size_t lastClassStart = 2 + 3 * numClasses - 3;
    EXPECT_EQ(recoveryCommands[lastClassStart], "class Class_" + std::to_string(numClasses));
    EXPECT_EQ(recoveryCommands[lastClassStart + 1], "bandwidth " + std::to_string(100000 + numClasses));
    EXPECT_EQ(recoveryCommands[lastClassStart + 2], "shape " + std::to_string(1000000 + numClasses * 1000));
}

// Test Deeply nested commands
TEST_F(Internal_ConfigTest, InitConfigs_DeeplyNestedCommands_ShouldParseCorrectly) 
{
    // Define a deeply nested commands structure
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "DeepNestedRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "level1": {
                            "level2": {
                                "level3": {
                                    "level4": {
                                        "level5": {
                                            "ip": {
                                                "address": {
                                                    "ip": "10.20.30.40",
                                                    "mask": "255.255.0.0"
                                                }
                                            },
                                            "bandwidth": {
                                                "id": "5000000"
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            ]
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Verify nested commands
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"].size(), 1);
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["level1"]["level2"]["level3"]["level4"]["level5"]["ip"]["address"]["ip"], "10.20.30.40");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["level1"]["level2"]["level3"]["level4"]["level5"]["bandwidth"]["id"], "5000000");
}

// Test Recovering Deeply Nested Configurations
TEST_F(Internal_ConfigTest, RecoverConfigs_DeeplyNestedCommands_ShouldRecoverCorrectly) 
{
    // Define a deeply nested commands structure
    std::string startupConfig = R"(
    {
        "hostname": {
            "word": "DeepNestedRecoverRouter"
        },
        "interface": {
            "GigabitEthernet": [
                {
                    "id": "0",
                    "commands": {
                        "level1": {
                            "level2": {
                                "level3": {
                                    "level4": {
                                        "level5": {
                                            "ip": {
                                                "address": {
                                                    "ip": "10.20.30.40",
                                                    "mask": "255.255.0.0"
                                                }
                                            },
                                            "bandwidth": {
                                                "id": "5000000"
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            ]
        }
    }
    )";

    // Define additional Configs.json
    std::string additionalConfig = R"(
    {
        "Interface": {
            "GigabitEthernet": [
                "ge-0"
            ]
        }
    }
    )";

    mockFileSystem->setupMockFile(startupFilePath, startupConfig);
    mockFileSystem->setupMockFile(additionalConfigPath, additionalConfig);

    // Initialize configurations
    configs->initConfigs({});

    // Recover commands
    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    // Define expected commands
    std::vector<std::string> expectedCommands = {
        "hostname DeepNestedRecoverRouter",
        "interface GigabitEthernet 0",
        "level1 level2 level3 level4 level5 ip address 10.20.30.40 255.255.0.0",
        "level1 level2 level3 level4 level5 bandwidth 5000000"
    };

    // Verify recovery commands match
    EXPECT_EQ(recoveryCommands.size(), expectedCommands.size());
    for (size_t i = 0; i < expectedCommands.size(); ++i) {
        EXPECT_EQ(recoveryCommands[i], expectedCommands[i]);
    }
}

// Test Testing large and extremely nested configurations
TEST_F(Internal_ConfigTest, InitConfigs_ExtremelyLargeAndDeeplyNested_ShouldLoadAllCorrectly) 
{
    const int numInterfaces = 1000; // Adjust as needed

    // Generate a large number of GigabitEthernet interfaces with deeply nested commands
    nlohmann::ordered_json startupConfig = {
        {"hostname", {{"word", "ComplexLargeRouter"}}},
        {"interface", {{"GigabitEthernet", nlohmann::ordered_json::array()}}}
    };

    for (int i = 1; i <= numInterfaces; ++i) {
        startupConfig["interface"]["GigabitEthernet"].push_back({
            {"id", std::to_string(i)},
            {"commands", {
                {"level1", {
                    {"level2", {
                        {"level3", {
                            {"level4", {
                                {"level5", {
                                    {"ip", {
                                        {"address", {
                                            {"ip", "10." + std::to_string(i) + "." + std::to_string(i % 256) + ".1"},
                                            {"mask", "255.255.255.0"}
                                        }}
                                    }},
                                    {"bandwidth", {{"id", std::to_string(100000 + i)}}}
                                }}
                            }}
                        }}
                    }}
                }}
            }}
        });
    }

    mockFileSystem->setupMockFile(startupFilePath, startupConfig.dump());
    mockFileSystem->setupMockFile(additionalConfigPath, "{}");

    configs->initConfigs({});

    // Verify total number of interfaces
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"].size(), numInterfaces);

    // Verify first interface details
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["level1"]["level2"]["level3"]["level4"]["level5"]["ip"]["address"]["ip"], "10.1.1.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["level1"]["level2"]["level3"]["level4"]["level5"]["bandwidth"]["id"], "100001");

    // Verify last interface details
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][numInterfaces - 1]["commands"]["level1"]["level2"]["level3"]["level4"]["level5"]["ip"]["address"]["ip"], "10." + std::to_string(numInterfaces) + "." + std::to_string(numInterfaces % 256) + ".1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][numInterfaces - 1]["commands"]["level1"]["level2"]["level3"]["level4"]["level5"]["bandwidth"]["id"], std::to_string(100000 + numInterfaces));
}

// Test Recovery of Extremely Large and Deeply Nested Configurations
TEST_F(Internal_ConfigTest, RecoverConfigs_ExtremelyLargeAndDeeplyNested_ShouldRecoverAllCommands) 
{
    const int numInterfaces = 1000; // Adjust as needed

    // Generate the same startup configuration as above
    nlohmann::ordered_json startupConfig = {
        {"hostname", {{"word", "RecoverComplexLargeRouter"}}},
        {"interface", {{"GigabitEthernet", nlohmann::ordered_json::array()}}}
    };

    for (int i = 1; i <= numInterfaces; ++i) {
        startupConfig["interface"]["GigabitEthernet"].push_back({
            {"id", std::to_string(i)},
            {"commands", {
                {"level1", {
                    {"level2", {
                        {"level3", {
                            {"level4", {
                                {"level5", {
                                    {"ip", {
                                        {"address", {
                                            {"ip", "10." + std::to_string(i) + "." + std::to_string(i % 256) + ".1"},
                                            {"mask", "255.255.255.0"}
                                        }}
                                    }},
                                    {"bandwidth", {{"id", std::to_string(100000 + i)}}}
                                }}
                            }}
                        }}
                    }}
                }}
            }}
        });
    }

    mockFileSystem->setupMockFile(startupFilePath, startupConfig.dump());
    mockFileSystem->setupMockFile(additionalConfigPath, "{}");

    configs->initConfigs({});

    std::vector<std::string> recoveryCommands = configs->recoverConfigs();

    // Expected number of commands:
    // 1 hostname command + 3 commands per interface (interface, ip address, bandwidth)
    EXPECT_EQ(recoveryCommands.size(), 1 + 3 * numInterfaces);

    // Verify hostname command
    EXPECT_EQ(recoveryCommands[0], "hostname RecoverComplexLargeRouter");

    // Verify first interface commands
    EXPECT_EQ(recoveryCommands[1], "interface GigabitEthernet 1");
    EXPECT_EQ(recoveryCommands[2], "level1 level2 level3 level4 level5 ip address 10.1.1.1 255.255.255.0");
    EXPECT_EQ(recoveryCommands[3], "level1 level2 level3 level4 level5 bandwidth 100001");

    // Verify last interface commands
    size_t lastIndex = 1 + 3 * numInterfaces - 3;
    EXPECT_EQ(recoveryCommands[lastIndex], "interface GigabitEthernet " + std::to_string(numInterfaces));
    EXPECT_EQ(recoveryCommands[lastIndex + 1], "level1 level2 level3 level4 level5 ip address 10." + std::to_string(numInterfaces) + "." + std::to_string(numInterfaces % 256) + ".1 255.255.255.0");
    EXPECT_EQ(recoveryCommands[lastIndex + 2], "level1 level2 level3 level4 level5 bandwidth " + std::to_string(100000 + numInterfaces));
}

// Test Extremely large number of vlans
TEST_F(Internal_ConfigTest, InitConfigs_ExtremelyLargeNumberOfVLANs_ShouldLoadAllVLANs) 
{
    const int numVLANs = 1000; // Adjust as needed

    // Generate VLAN configurations
    nlohmann::ordered_json startupConfig = {
        {"hostname", {{"word", "VLANLargeRouter"}}},
        {"interface", {
            {"GigabitEthernet", {
                {
                    {"id", "1"},
                    {"commands", {
                        {"vlan", nlohmann::ordered_json::array()}
                    }}
                }
            }}
        }}
    };

    for (int i = 1; i <= numVLANs; ++i) {
        startupConfig["interface"]["GigabitEthernet"][0]["commands"]["vlan"].push_back({
            {"id", std::to_string(100 + i)},
            {"name", "VLAN_" + std::to_string(i)}
        });
    }

    mockFileSystem->setupMockFile(startupFilePath, startupConfig.dump());
    mockFileSystem->setupMockFile(additionalConfigPath, "{}");

    configs->initConfigs({});

    // Verify VLANs loaded correctly
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["vlan"].size(), numVLANs);
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["vlan"][0]["id"], "101");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["vlan"][0]["name"], "VLAN_1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["vlan"][numVLANs - 1]["id"], std::to_string(100 + numVLANs));
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["vlan"][numVLANs - 1]["name"], "VLAN_" + std::to_string(numVLANs));
}

// Test Extremely Large and Deeply Nested Configuration
TEST_F(Internal_ConfigTest, InitConfigs_ExtremelyLargeAndDeeplyNestedCombined_ShouldLoadAllCorrectly) 
{
    const int numInterfaces = 500; // Adjust as needed
    const int vlansPerInterface = 10;

    // Generate startup configuration with deeply nested commands and VLANs
    nlohmann::ordered_json startupConfig = {
        {"hostname", {{"word", "ComplexDeepLargeRouter"}}},
        {"interface", {{"GigabitEthernet", nlohmann::ordered_json::array()}}}
    };

    for (int i = 1; i <= numInterfaces; ++i) {
        nlohmann::ordered_json interfaceEntry = {
            {"id", std::to_string(i)},
            {"commands", {
                {"level1", {
                    {"level2", {
                        {"level3", {
                            {"level4", {
                                {"level5", {
                                    {"ip", {
                                        {"address", {
                                            {"ip", "10." + std::to_string(i) + "." + std::to_string(i % 256) + ".1"},
                                            {"mask", "255.255.255.0"}
                                        }}
                                    }},
                                    {"bandwidth", {{"id", std::to_string(100000 + i)}}},
                                    {"vlan", nlohmann::ordered_json::array()}
                                }}
                            }}
                        }}
                    }}
                }}
            }}
        };

        // Add VLANs to the interface
        for (int j = 1; j <= vlansPerInterface; ++j) {
            interfaceEntry["commands"]["level1"]["level2"]["level3"]["level4"]["level5"]["vlan"].push_back({
                {"id", std::to_string(1000 + j)},
                {"name", "VLAN_" + std::to_string(j) + "_Interface_" + std::to_string(i)}
            });
        }

        startupConfig["interface"]["GigabitEthernet"].push_back(interfaceEntry);
    }

    mockFileSystem->setupMockFile(startupFilePath, startupConfig.dump());
    mockFileSystem->setupMockFile(additionalConfigPath, "{}");

    configs->initConfigs({});

    // Verify interface count
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"].size(), numInterfaces);

    // Verify nested IP and bandwidth in the first interface
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["level1"]["level2"]["level3"]["level4"]["level5"]["ip"]["address"]["ip"], "10.1.1.1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["level1"]["level2"]["level3"]["level4"]["level5"]["bandwidth"]["id"], "100001");

    // Verify VLANs in the first interface
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["level1"]["level2"]["level3"]["level4"]["level5"]["vlan"].size(), vlansPerInterface);
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["level1"]["level2"]["level3"]["level4"]["level5"]["vlan"][0]["id"], "1001");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][0]["commands"]["level1"]["level2"]["level3"]["level4"]["level5"]["vlan"][0]["name"], "VLAN_1_Interface_1");

    // Verify the last interface details
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][numInterfaces - 1]["commands"]["level1"]["level2"]["level3"]["level4"]["level5"]["ip"]["address"]["ip"], "10." + std::to_string(numInterfaces) + "." + std::to_string(numInterfaces % 256) + ".1");
    EXPECT_EQ(configs->root["interface"]["GigabitEthernet"][numInterfaces - 1]["commands"]["level1"]["level2"]["level3"]["level4"]["level5"]["bandwidth"]["id"], std::to_string(100000 + numInterfaces));
}
