#include <gtest/gtest.h>
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <sstream>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fstream>

const std::string stateDir = ".";
const std::string defaultSuitePath = stateDir + "/.default_suite";
const std::string testModePath = stateDir + "/.test_mode_state";

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

void saveTestModeState(const std::string& scope, const std::set<std::string>& disabled)
{
    std::ofstream out(testModePath);
    if (!out) return;
    out << scope << "\n";
    for (const auto& key : disabled)
        out << key << "\n";
}

void loadTestModeState(std::string& scope, std::set<std::string>& disabled)
{
    std::ifstream in(testModePath);
    if (!in) return;
    std::getline(in, scope);
    std::string key;
    while (std::getline(in, key))
        if (!key.empty()) disabled.insert(key);
}

void clearTestModeState()
{
    std::remove(testModePath.c_str());
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
    std::cout << "\033[?25l";
}

void toggleRangeDisabled(const std::vector<Entry>& flatList, int a, int b, std::set<std::string>& disabledTests)
{
    if (a > b) std::swap(a, b);
    int disabledCount = 0, testCount = 0;
    for (int i = a; i <= b; ++i)
    {
        if (flatList[i].type == Entry::TEST)
        {
            testCount++;
            if (disabledTests.count(flatList[i].suite + "." + flatList[i].test))
                disabledCount++;
        }
    }
    bool shouldDisable = (disabledCount < testCount);
    for (int i = a; i <= b; ++i)
    {
        if (flatList[i].type != Entry::TEST) continue;
        std::string key = flatList[i].suite + "." + flatList[i].test;
        if (shouldDisable) disabledTests.insert(key);
        else disabledTests.erase(key);
    }
}

std::string buildFilterWithDisabled(const std::string& baseFilter, const TestMap& grouped, const std::set<std::string>& disabledTests)
{
    std::vector<std::string> enabled;
    for (const auto& [group, suites] : grouped)
    {
        for (const auto& [suite, tests] : suites)
        {
            bool suiteInScope = (baseFilter == "*" || baseFilter == suite + ".*" ||
                baseFilter.find(suite + ".*") != std::string::npos);
            if (!suiteInScope) continue;

            for (const auto& test : tests)
            {
                std::string key = suite + "." + test;
                if (disabledTests.count(key) == 0)
                    enabled.push_back(key);
            }
        }
    }

    if (enabled.empty()) return "NOTHING_TO_RUN";

    std::string filter;
    for (const auto& s : enabled) filter += s + ":";
    filter.pop_back();
    return filter;
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

    TestMap allGrouped;
    for (int i = 0; i < unit->total_test_suite_count(); ++i)
    {
        const auto* suite = unit->GetTestSuite(i);
        std::string suiteName = suite->name();
        std::string group = extractGroup(suiteName);

        for (int j = 0; j < suite->total_test_count(); ++j)
            allGrouped[group][suiteName].push_back(suite->GetTestInfo(j)->name());
    }

    bool testMode = false;
    std::string testModeScope;
    std::string testModeSuite;
    std::set<std::string> disabledTests;
    int rangeStart = -1;

    loadTestModeState(testModeScope, disabledTests);
    if (!testModeScope.empty() && testModeScope.size() > 2 &&
        testModeScope.substr(testModeScope.size() - 2) == ".*")
    {
        testModeSuite = testModeScope.substr(0, testModeScope.size() - 2);
        std::string group = extractGroup(testModeSuite);
        if (allGrouped.count(group) && allGrouped[group].count(testModeSuite))
            testMode = true;
        else
        {
            testModeScope.clear();
            testModeSuite.clear();
            disabledTests.clear();
        }
    }
    else
    {
        testModeScope.clear();
        disabledTests.clear();
    }

    TestMap grouped;
    std::map<std::string, bool> groupOpen;
    std::map<std::string, std::map<std::string, bool>> suiteOpen;

    auto rebuildGrouped = [&]()
    {
        grouped.clear();
        groupOpen.clear();
        suiteOpen.clear();
        if (testMode)
        {
            std::string group = extractGroup(testModeSuite);
            grouped[group][testModeSuite] = allGrouped[group][testModeSuite];
            groupOpen[group] = true;
            suiteOpen[group][testModeSuite] = true;
        }
        else
        {
            grouped = allGrouped;
        }
    };

    rebuildGrouped();

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
        frame.push_back(testMode
            ? "🔬 \033[1;33mTEST MODE\033[0m  —  x=disable, m=mark range, r=run, R=run range, e=exit | scope: " + testModeScope
            : "🔧 \033[1;34mRouter Test UI\033[0m  —  ↑↓ = move, ←/→ = collapse/expand, Enter = run, t=test mode, q = quit");
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
                std::string key = entry.suite + "." + entry.test;
                bool disabled = disabledTests.count(key);
                bool inRange = testMode && rangeStart >= 0 &&
                    i >= std::min(rangeStart, cursor) && i <= std::max(rangeStart, cursor);

                line << (selected ? "\033[7m" : "");
                if (disabled) line << "\033[9;31m";
                if (inRange && !selected) line << "\033[43m";
                line << "     • " << entry.test;
                if (disabled) line << " [DISABLED]";
                line << "\033[0m";
            }

            frame.push_back(line.str());
        }

        while ((int)frame.size() < termHeight)
            frame.push_back("");

        renderFrame(frame);

        setRawMode(true);
        char key = readKey();

        if (key == '\033')
        {
            readKey(); // skip [
            char arrow = readKey();
            setRawMode(false);
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
            setRawMode(false);
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
            setRawMode(false);
            std::cout << "\033[?25h";
            break;
        }
        else if (key == 'd')
        {
            setRawMode(false);
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
            setRawMode(false);
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
            if (key == 't')
            {
                const auto& e = flatList[cursor];
                if (e.type != Entry::SUITE) continue;

                testMode = true;
                testModeSuite = e.suite;
                testModeScope = e.suite + ".*";
                rangeStart = -1;
                disabledTests.clear();
                cursor = 0;
                scrollOffset = 0;
                rebuildGrouped();

                saveTestModeState(testModeScope, disabledTests);
            }
            if (key == 'e')
            {
                testMode = false;
                rangeStart = -1;
                disabledTests.clear();
                testModeScope.clear();
                testModeSuite.clear();
                cursor = 0;
                scrollOffset = 0;
                rebuildGrouped();
                clearTestModeState();
            }
            if (key == 'x')
            {
                if (rangeStart >= 0)
                {
                    toggleRangeDisabled(flatList, rangeStart, cursor, disabledTests);
                    rangeStart = -1;
                }
                else
                {
                    const auto& e = flatList[cursor];
                    if (e.type == Entry::TEST)
                    {
                        std::string k = e.suite + "." + e.test;
                        if (disabledTests.count(k)) disabledTests.erase(k);
                        else disabledTests.insert(k);
                    }
                    else if (e.type == Entry::SUITE)
                    {
                        for (const auto& test : grouped[e.group][e.suite])
                            disabledTests.insert(e.suite + "." + test);
                    }
                }
                saveTestModeState(testModeScope, disabledTests);
            }
            if (key == 'm')
            {
                rangeStart = cursor;
            }
            if (key == 'r')
            {
                const auto& e = flatList[cursor];
                std::string base;
                if (e.type == Entry::TEST) base = e.suite + "." + e.test;
                else if (e.type == Entry::SUITE) base = e.suite + ".*";
                else if (e.type == Entry::GROUP)
                {
                    for (const auto& [suite, _] : grouped[e.group])
                        base += suite + ".*:";
                    if (!base.empty()) base.pop_back();
                }
                std::string filter = testMode
                    ? buildFilterWithDisabled(testModeScope, grouped, disabledTests)
                    : base;
                runWithFilter(filter);
            }
            if (key == 'R')
            {
                if (rangeStart >= 0)
                {
                    int lo = std::min(rangeStart, cursor), hi = std::max(rangeStart, cursor);
                    std::string filter;
                    for (int i = lo; i <= hi; ++i)
                    {
                        if (flatList[i].type == Entry::TEST)
                        {
                            std::string k = flatList[i].suite + "." + flatList[i].test;
                            if (!disabledTests.count(k))
                                filter += k + ":";
                        }
                    }
                    if (!filter.empty())
                    {
                        filter.pop_back();
                        runWithFilter(filter);
                    }
                    rangeStart = -1;
                }
            }
        }
    }
    clearScreen(getTerminalHeight());
    return 0;
}
