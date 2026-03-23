// WebConsole.hpp

#ifndef WEB_CONSOLE_HPP
#define WEB_CONSOLE_HPP

#include "cli/runtime/Console.h"
#include "cli/runtime/CliSession.h"
#include "UnixApi.h"
#include <json.hpp>
#include <Global.h>

#include <sstream>
#include <mutex>

namespace web
{

// Mock class for IConsole using Google Mock
class WebConsole : public IConsole
{
public:
    WebConsole(UnixApi& api, int clientFd, core::Global& global)
        : api(api), global(global), clientFd(clientFd) {}

    void clearScreen() override {}
    void enableLineWrapping() override {}
    void clearLineAfterCursor() override {}
    void saveCursorPosition() override {}
    void restoreCursorPosition() override {}
    void moveCursorToStart() override {}
    void moveCursorLeft(size_t count) override {}
    void moveCursorRight(size_t count) override {}
    void moveCursorUp(size_t count) override {}
    void moveCursorDown(size_t count) override {}
    size_t getTerminalWidth() override { return std::numeric_limits<size_t>::max(); }
    void beep() override {}
    void flush() override {}
    CursorPosition getCursorPosition() override { return CursorPosition{}; }

    bool getPrompt(std::string& p)
    {
        p = prompt;
        if (prompt.empty()) return false;
        prompt.clear();
        return true;
    }

    bool getPreBuffer(std::string& p)
    {
        p = preBuffer;
        if (preBuffer.empty()) return false;
        preBuffer.clear();
        return true;
    }

    void print(const std::string& str, Color color) override
    {
        if (color == Color::TERMINAL) return;
        if (color == Color::PROMPT) // Secret prompt option
        {
            prompt = str;
            preBuffer = buffer.str();
            buffer.str("");
            buffer.clear();
            return;
        }

        std::string colored = applyColor(str, color);
        {
            std::lock_guard<std::mutex> lock(mu);
            buffer << colored;
        }
    }

    void beginSession()
    {
        std::lock_guard<std::mutex> lock(mu);
        buffer.str("");
        buffer.clear();
    }

    void flushCommands(CliSession& endSequence, const std::string& id, bool scroll = false)
    {
        std::string output;
        {
            std::lock_guard<std::mutex> lock(mu);
            if (buffer.str().empty() && preBuffer.empty() && prompt.empty()) return;
            output = buffer.str();
            buffer.str("");
            buffer.clear();
        }
        auto reply = [&]() -> nlohmann::json {
            nlohmann::json j = {
                {"action", "cmd"},
                {"type", "router"},
                {"client_id", id},
                {"data", output},
                {"cursor_pos", endSequence.getInputCursorPosition() },
                {"initial_length", endSequence.getInitialLineLength() },
                {"hostname", global.getHostname() }
            };
            
            std::string str;
            if (getPreBuffer(str))
                j["pre_data"] = str;
            if (getPrompt(str))
                j["prompt"] = str;
            if (scroll)
                j["scroll"] = scroll;
            
            return j;
        };
        api.sendJson(clientFd, reply());
    }

private:
    UnixApi& api;
    core::Global& global;
    int clientFd;
    std::mutex mu;

    std::string preBuffer;
    std::string prompt;
    std::stringstream buffer;

    static std::string applyColor(const std::string& s, Color c) {
        switch (c) {
            case Color::BLACK:   return "\033[1;30m" + s + "\033[0m";
            case Color::RED:     return "\033[1;31m" + s + "\033[0m";
            case Color::GREEN:   return "\033[1;32m" + s + "\033[0m";
            case Color::YELLOW:  return "\033[1;33m" + s + "\033[0m";
            case Color::BLUE:    return "\033[1;34m" + s + "\033[0m";
            case Color::MAGENTA: return "\033[1;35m" + s + "\033[0m";
            case Color::CYAN:    return "\033[1;36m" + s + "\033[0m";
            case Color::WHITE:   return s + "\033[0m";
            case Color::NONE:    return s;
            default:             return s;
        }
    }
};

} // namespace web

#endif // MOCK_CONSOLE_HPP

