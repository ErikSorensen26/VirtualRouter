/**
 * @file WebConsole.hpp
 * @brief ConsoleController implementation that buffers CLI output for delivery over a web socket.
 */

#ifndef WEB_CONSOLE_HPP
#define WEB_CONSOLE_HPP

#include "cli/runtime/Console.h"
#include "cli/runtime/CliSession.h"
#include "UnixApi.h"
#include "cli/runtime/ConsoleController.hpp"
#include <json.hpp>
#include <Global.h>

#include <sstream>
#include <mutex>

namespace web
{

/**
 * @brief A @ref cli::ConsoleController that captures CLI output and delivers it as JSON to a web client.
 * @ingroup WEB
 *
 * WebConsole sits between the CLI engine and the web transport. It intercepts
 * every print() call issued by the CLI during command execution, converts ANSI
 * color codes to escape sequences, and accumulates the result in an in-memory
 * string stream. Callers invoke flushCommands() to package the buffer into a
 * JSON reply and send it over the associated @ref UnixApi connection.
 *
 * Two secondary buffers, @ref preBuffer and @ref prompt, allow the CLI to
 * separate output that should appear before the prompt line from the prompt
 * string itself. This lets the web client render prompt and output regions
 * independently.
 *
 * ## Architectural Role
 * Acts as the adapter between the text-oriented CLI console interface and the
 * structured JSON protocol expected by the web front-end. All terminal control
 * operations (cursor movement, screen clear, etc.) are no-ops because the web
 * client handles layout itself.
 *
 * ## Lifecycle & Ownership
 * Created by @ref WebSessionManager when a new web client subscribes. Ownership
 * is shared with the @ref cli::CliSession created at the same time — the session
 * holds a pointer to the WebConsole as its ConsoleController. Both are destroyed
 * together by WebSessionManager on unsubscribe or connection close.
 *
 * ## Concurrency Model
 * @ref mu guards @ref buffer and @ref preBuffer. print() may be called from
 * any thread; flushCommands() and beginSession() must be called from the
 * WebSessionManager loop thread. The @ref prompt field is accessed only from
 * the CLI execution thread (inside print()), then read from the loop thread
 * inside flushCommands() — callers must ensure these do not overlap.
 *
 * @see WebSessionManager
 * @see UnixApi
 */
class WebConsole : public cli::ConsoleController
{
public:
    /**
     * @brief Constructs a WebConsole bound to a specific client connection.
     *
     * @param api       Reference to the Unix API server used to send JSON replies.
     * @param clientFd  File descriptor of the client that will receive output.
     * @param global    Reference to the global system state, used to retrieve the hostname.
     */
    WebConsole(UnixApi& api, int clientFd, core::Global& global)
        : api(api), global(global), clientFd(clientFd) {}

    // Terminal control operations are no-ops; the web client manages layout.
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
    /// Reports an unbounded terminal width so the CLI never wraps output lines.
    size_t getTerminalWidth() override { return std::numeric_limits<size_t>::max(); }
    void beep() override {}
    void flush() override {}
    cli::CursorPosition getCursorPosition() override { return cli::CursorPosition{}; }

    /**
     * @brief Retrieves and clears the pending prompt string.
     *
     * @param[out] p  Populated with the pending prompt on success.
     * @return        True if a prompt was waiting; false if none had been set.
     */
    bool getPrompt(std::string& p)
    {
        p = prompt;
        if (prompt.empty()) return false;
        prompt.clear();
        return true;
    }

    /**
     * @brief Retrieves and clears any output that was buffered before the prompt was set.
     *
     * Output printed before the CLI emits a prompt is captured in @ref preBuffer
     * so the web client can display it in a separate region above the prompt line.
     *
     * @param[out] p  Populated with the pre-prompt output on success.
     * @return        True if pre-prompt output was waiting; false otherwise.
     */
    bool getPreBuffer(std::string& p)
    {
        p = preBuffer;
        if (preBuffer.empty()) return false;
        preBuffer.clear();
        return true;
    }

    /**
     * @brief Intercepts a CLI print call and routes it to the appropriate buffer.
     *
     * - @c Color::TERMINAL output is silently discarded (raw terminal escape sequences
     *   have no meaning in the web transport).
     * - @c Color::PROMPT triggers a prompt boundary: the current @ref buffer contents
     *   are moved to @ref preBuffer, the prompt string is saved, and the buffer is reset.
     * - All other colors are converted to ANSI escape sequences and appended to @ref buffer.
     *
     * @param str    Text to output.
     * @param color  Rendering hint that controls both color coding and routing logic.
     */
    void print(const std::string& str, cli::Color color) override
    {
        if (color == cli::Color::TERMINAL) return;
        if (color == cli::Color::PROMPT) // Secret prompt option
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

    /**
     * @brief Resets all output buffers in preparation for a new web session.
     *
     * Called by @ref WebSessionManager when a client re-subscribes to an
     * existing session so that stale output from a previous connection is
     * not replayed.
     */
    void beginSession()
    {
        std::lock_guard<std::mutex> lock(mu);
        buffer.str("");
        buffer.clear();
    }

    /**
     * @brief Packages all buffered output into a JSON reply and sends it to the client.
     *
     * Collects the main output buffer, pre-buffer, and prompt (if any), then
     * sends a single JSON message containing the output, cursor position,
     * initial line length, and current hostname.
     *
     * Returns immediately if all buffers are empty to avoid unnecessary
     * round-trips.
     *
     * @param endSequence  The active CLI session; used to read cursor and line-length state.
     * @param id           Client ID string that identifies the session in the web protocol.
     * @param scroll       When true, instructs the web client to scroll to the bottom.
     */
    void flushCommands(cli::CliSession& endSequence, const std::string& id, bool scroll = false)
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
    UnixApi& api;        ///< Transport used by flushCommands() to deliver replies.
    core::Global& global; ///< System-wide state; accessed for the hostname field in JSON replies.
    int clientFd;         ///< File descriptor of the web client receiving output.
    std::mutex mu;        ///< Guards @ref buffer and @ref preBuffer against concurrent access.

    std::string preBuffer;   ///< Output captured before the most recent prompt boundary.
    std::string prompt;      ///< Pending prompt string set by a Color::PROMPT print() call.
    std::stringstream buffer; ///< Accumulates colored output between flushCommands() calls.

    /**
     * @brief Wraps @p s in the ANSI escape sequence corresponding to @p c.
     *
     * Returns the string unchanged for Color::NONE and unknown color values.
     *
     * @param s  Text to colorize.
     * @param c  Desired color.
     * @return   ANSI-escaped string.
     */
    static std::string applyColor(const std::string& s, cli::Color c) {
        switch (c) {
            case cli::Color::BLACK:   return "\033[1;30m" + s + "\033[0m";
            case cli::Color::RED:     return "\033[1;31m" + s + "\033[0m";
            case cli::Color::GREEN:   return "\033[1;32m" + s + "\033[0m";
            case cli::Color::YELLOW:  return "\033[1;33m" + s + "\033[0m";
            case cli::Color::BLUE:    return "\033[1;34m" + s + "\033[0m";
            case cli::Color::MAGENTA: return "\033[1;35m" + s + "\033[0m";
            case cli::Color::CYAN:    return "\033[1;36m" + s + "\033[0m";
            case cli::Color::WHITE:   return s + "\033[0m";
            case cli::Color::NONE:    return s;
            default:             return s;
        }
    }
};

} // namespace web

#endif // MOCK_CONSOLE_HPP
