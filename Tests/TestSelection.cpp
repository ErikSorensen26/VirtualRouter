#include <gtest/gtest.h>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fstream>
#include <map>

const std::string defaultSuitePath = ".default_suite";

struct Entry 
{
    enum Type { GROUP, SUITE, TEST } type;
    std::string group;
    std::string suite;
    std::string test;
};

using TestMap = std::map<std::string, std::map<std::string, std::vector<std::string>>>;

std::vector<std::string> previousFrame;

void renderFrame(const std::vector<std::string>& nextFrame)
{
    for (size_t i = 0; i < nextFrame.size(); ++i)
    {
        if (i >= previousFrame.size() || nextFrame[i] != previousFrame[i])
        {
            std::cout << "\033[" << (i + 1) << ";1H";
            std::cout << "\033[2K" << nextFrame[i];
        }
    }
    previousFrame = nextFrame;
    std::cout << std::flush;
}

int getTerminalHeight()
{
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_row;
}

void setRawMode(bool enable)
{
    static termios oldt;
    static bool initialized = false;
    if (!initialized)
    {
        tcgetattr(STDIN_FILENO, &oldt);
        initialized = true;
    }
    termios newt = oldt;
    if (enable) newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, enable ? &newt : &oldt);
}

char readKey()
{
    char c;
    read(STDIN_FILENO, &c, 1);
    return c;
}

void clearScreen(int lines)
{
    std::cout << "\033[H";
    for (int i = 0; i < lines; ++i)
        std::cout << "\033[2K\n";
    std::cout << "\033[H";
}

std::string extractGroup(const std::string& suite)
{
    auto us = suite.find('_');

    if (us != std::string::npos) return suite.substr(0, us);
    return "Other";
}

std::string extractName(const std::string& suite)
{
    auto us = suite.find('_');

    if (us != std::string::npos) return suite.substr(us + 1);
    return suite;
}

void runWithFilter(const std::string& filter)
{
    auto* unit = ::testing::UnitTest::GetInstance();

    // Restore terminal to clean state
    std::cout << "\033[?25h";
    std::cout << "\033[2J\033[H";
    setRawMode(false);

    ::testing::GTEST_FLAG(filter) = filter;
    (void)unit->Run();

    std::cout << "\nPress enter to return to test menu..." << std::flush;
    setRawMode(true);
    while (readKey() != '\n');
    setRawMode(false);

    previousFrame.clear();
    std::cout << "\033[2J\033[H";
    std::cout << "\033[?25l]";
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    std::ifstream defaultFile(defaultSuitePath);
    if (defaultFile)
    {
        std::string suiteFilter;
        std::getline(defaultFile, suiteFilter);
        defaultFile.close();

        std::cout << "\033[2J\033[H\033[?25l";
        runWithFilter(suiteFilter);
        return 0;
    }

    auto* unit = ::testing::UnitTest::GetInstance();

    TestMap grouped;
    std::map<std::string, bool> groupOpen;
    std::map<std::string, std::map<std::string, bool>> suiteOpen;

    for (int i = 0; i < unit->total_test_suite_count(); ++i)
    {
        const auto* suite = unit->GetTestSuite(i);
        std::string suiteName = suite->name();
        std::string group = extractGroup(suiteName);

        for (int j = 0; j < suite->total_test_count(); ++j)
        {
            grouped[group][suiteName].push_back(suite->GetTestInfo(j)->name());
        }
    }

    std::vector<Entry> flatList;
    auto rebuildList = [&]()
    {
        flatList.clear();
        for (const auto& [group, suites] : grouped) 
        {
            flatList.push_back({Entry::GROUP, group, "", ""});
            if (groupOpen[group])
            {
                for (const auto& [suite, tests] : suites)
                {
                    flatList.push_back({Entry::SUITE, group, suite, ""});
                    if (suiteOpen[group][suite])
                    {
                        for (const auto& test : tests)
                        {
                            flatList.push_back({Entry::TEST, group, suite, test});
                        }
                    }
                }
            }
        }
    };

    rebuildList();
    int cursor = 0;
    int scrollOffset = 0;
    bool firstRender = true;

    while (true)
    {
        int termHeight = getTerminalHeight();
        int visibleLines = termHeight - 4;

        std::cout << "\033[?25l";

        rebuildList();

        if (firstRender)
        {
            clearScreen(termHeight);
            firstRender = false;
        }

        // Scroll window management
        int totalLines = flatList.size();
        if (cursor < scrollOffset) scrollOffset = cursor;
        else if (cursor >= scrollOffset + visibleLines) scrollOffset = cursor - visibleLines + 1;

        std::vector<std::string> frame;
        frame.push_back("🔧 \033[1;34mRouter Test UI\033[0m  —  ↑↓ = move, ←/→ = collapse/expand, Enter = run, q = quit");
        frame.push_back("");
        
        for (int i = scrollOffset; i < std::min(scrollOffset + visibleLines, totalLines); ++i)
        {
            const auto& entry = flatList[i];
            bool selected = (i == cursor);
            std::stringstream line;

            if (entry.type == Entry::GROUP) {
                std::string icon = groupOpen[entry.group] ? "▼" : "▶";
                line << (selected ? "\033[7m" : "") << icon << " [" << entry.group << "]" << "\033[0m";
            } else if (entry.type == Entry::SUITE) {
                std::string icon = suiteOpen[entry.group][entry.suite] ? "▼" : "▶";
                line << (selected ? "\033[7m" : "") << "  " << icon << " " << extractName(entry.suite) << "\033[0m";
            } else if (entry.type == Entry::TEST) {
                line << (selected ? "\033[7m" : "") << "     • " << entry.test << "\033[0m";
            }

            frame.push_back(line.str());
        }

        while ((int)frame.size() < termHeight)
            frame.push_back("");

        renderFrame(frame);

        setRawMode(true);
        char key = readKey();
        setRawMode(false);

        if (key == '\033')
        {
            readKey(); // skip [
            char arrow = readKey();
            if (arrow == 'A') cursor = (cursor - 1 + flatList.size()) % flatList.size(); // up
            if (arrow == 'B') cursor = (cursor + 1) % flatList.size(); // down
            if (arrow == 'C')
            {
                const auto& e = flatList[cursor];
                if (e.type == Entry::GROUP) groupOpen[e.group] = true;
                else if (e.type == Entry::SUITE) suiteOpen[e.group][e.suite] = true;
            }
            if (arrow == 'D')
            {
                const auto& e = flatList[cursor];
                if (e.type == Entry::GROUP) groupOpen[e.group] = false;
                else if (e.type == Entry::SUITE) suiteOpen[e.group][e.suite] = false;
            }
        }
        else if (key == '\n')
        {
            const auto& e = flatList[cursor];
            if (e.type == Entry::GROUP)
            {
                std::string filter;
                for (const auto& [suite, _] : grouped[e.group])
                    filter += suite + ".*:";
                if (!filter.empty()) filter.pop_back();
                runWithFilter(filter);
            }
            else if (e.type == Entry::SUITE)
            {
                runWithFilter(e.suite + ".*");
            }
            else if (e.type == Entry::TEST)
            {
                runWithFilter(e.suite + "." + e.test);
            }
        }
        else if (key == 'q')
        {
            std::cout << "\033[?25h";
            setRawMode(false);
            break;
        }
        else if (key == 'd')
        {
            const auto& e = flatList[cursor];
            std::string filter;

            if (e.type == Entry::GROUP)
            {
                for (const auto& [suite, _] : grouped[e.group])
                    filter += suite + ".*:";
                if (!filter.empty()) filter.pop_back();
            }
            else if (e.type == Entry::SUITE)
            {
                filter = e.suite + ".*";
            }
            else if (e.type == Entry::TEST)
            {
                filter = e.suite + "." + e.test;
            }

            std::ofstream defaultOut(defaultSuitePath);
            if (defaultOut)
            {
                defaultOut << filter << "\n";
                defaultOut.close();
            }

            runWithFilter(filter);
        }
        else
        {
            if (key == 'j') cursor = (cursor + 1) % flatList.size();
            if (key == 'k') cursor = (cursor - 1 + flatList.size()) % flatList.size();
            if (key == 'l')
            {
                const auto& e = flatList[cursor];
                if (e.type == Entry::GROUP) groupOpen[e.group] = true;
                else if (e.type == Entry::SUITE) suiteOpen[e.group][e.suite] = true;
            }
            if (key == 'h')
            {
                const auto& e = flatList[cursor];
                if (e.type == Entry::GROUP) groupOpen[e.group] = false;
                else if (e.type == Entry::SUITE) suiteOpen[e.group][e.suite] = false;
            }
        }
    }
    clearScreen(getTerminalHeight());
    return 0;
}
