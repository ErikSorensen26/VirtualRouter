// MockFileSystem.hpp

#ifndef MOCK_FILE_SYSTEM_HPP
#define MOCK_FILE_SYSTEM_HPP

#include <gmock/gmock.h>
#include <string>
#include <cli/runtime/Configs.h>

namespace cli
{
class MockFileSystem : public FileSystem {
public:
    MOCK_METHOD(bool, writeFile, (const std::string& path, const std::string& content), (override));
    MOCK_METHOD(void, removeFile, (const std::string& path), (override));

    // Helper to set file content
    void setFileContent(const std::string& path, const std::string& content) {
        fileContents[path] = content;
    }

    // Override readFile to return content from fileContents map
    bool readFile(const std::string& path, std::string& content) override 
    {
        auto it = fileContents.find(path);
        if (it != fileContents.end()) {
            content = it->second;
            return true;
        }
        return false;
    }

    bool fileExists(const std::string& path) override
    {
        if (fileContents.contains(path))
        {
            return true;
        }
        return false;
    }

    // Helper method to set up mock file content
    void setupMockFile(const std::string& path, const std::string& content = "")
    {
        setFileContent(path, content);
    }

private:
    std::map<std::string, std::string> fileContents;
};
}

#endif // MOCK_FILE_SYSTEM_HPP
