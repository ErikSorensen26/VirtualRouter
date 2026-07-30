/**
 * @file TreeNavigator.hpp
 * @brief The CLI mode stack: where a session is, and how it gets back.
 *
 * A session is always inside one mode, and entering a sub-mode has to be
 * reversible — `exit` from `(config-router-af)#` must land back in
 * `(config-router)#` with the prompt, the command-tree position, and the config
 * object it was editing all restored together. Those three move as a unit, so
 * they are captured as a unit: each @ref NavFrame pairs the execution state with
 * the @ref cli::tree::ModeEntry it was reading commands from.
 *
 * Frames live in a fixed-size buffer rather than a vector. Depth is bounded by
 * the grammar (UserExec down to the deepest sub-mode), so the stack is small and
 * known, and a session should not allocate to change modes. @ref NavFrame holds
 * a @ref cli::tree::ModeEntry, which has no empty state, so the slots are raw
 * storage and frames are placement-new'd as modes are entered.
 *
 * Three ways in, deliberately distinct:
 * - @ref TreeNavigation::changeMode pushes, so `exit` returns here. The normal case.
 * - @ref TreeNavigation::resetAndChangeMode clears the stack, for `end` and
 *   Ctrl-Z, which jump to a fixed mode regardless of how deep the session went.
 * - @ref TreeNavigation::saveAndChangeMode marks the current depth so
 *   @ref TreeNavigation::restore can come back to it, for detours like
 *   `do <command>` that must leave the session where they found it.
 *
 * Restoring pops frame by frame rather than truncating: the mode, prompt, and
 * config pointer are rebuilt from each frame on the way down, and discarding
 * frames would leave all three wherever the detour left them.
 */

#ifndef TREE_NAVIGATION_HPP
#define TREE_NAVIGATION_HPP

#include <new>
#include <cstddef>
#include "cli/execution/ExecutionContext.hpp"
#include "cli/tree/CommandTree.h"

namespace cli
{
/// @brief One entry in the navigation history stack.
struct NavFrame
{
    NavFrame(cli::execution::NavEntry nEntry, const tree::ModeEntry& md)
        : executorEntry(nEntry), modeDir(md)
    {}

    cli::execution::NavEntry  executorEntry;   ///< Execution state (mode, dispatch, construct, config ptr).
    const tree::ModeEntry modeDir; ///< Command-tree node for this mode.
};

class TreeNavigation
{
public:
    TreeNavigation(tree::CommandTree& tr, execution::ExecutionManager& exc)
        : tree(tr), execution(exc)
    {}

    ~TreeNavigation()
    {
        unwindNavTo(0);
    }

    /**
     * @brief Transitions to a new CLI mode and pushes the current mode onto the nav stack.
     *
     * Updates the working directory pointer and prompt string, captures the current
     * mode into the nav stack, then delegates to @ref ExecutionManager::changeMode.
     * Use this for all normal sub-mode entries (interface, router, address-family, etc.)
     * so that `popMode()` can return here automatically.
     *
     * @tparam T  Target @ref CliMode enum value.
     * @tparam S  Registry type for the new mode.
     */
    template <CliMode T, typename S>
    requires config::IsSubRegistryWrapper<S>
    bool changeMode(S& configs)
    {
        if (execution.hasMode() && execution.getMode() == T)
        {
            currentMode = tree.getMode(T);
            return true;
        }

        if (execution.hasMode() && navTop < NAV_STACK_DEPTH)
            pushNavFrame(execution.captureCurrentMode(), currentMode);
        currentMode = tree.getMode(T);
        execution.changeMode<T, S>(configs);
        return true;
    }

    /**
     * @brief Enters a mode, remembering the depth to come back to.
     *
     * For temporary detours: `do <command>` runs one line in privileged mode and
     * has to leave the session exactly where it was. Records the depth before
     * entering so @ref restore can unwind to it, however many modes the detour
     * ends up pushing.
     *
     * The depth is only recorded if the transition succeeds, so a failed mode
     * change cannot leave a restore point that unwinds somewhere unintended.
     *
     * @tparam T  Target @ref CliMode enum value.
     * @tparam S  Registry type for the new mode.
     * @return True if the mode was entered.
     */
    template <CliMode T, typename S>
    requires config::IsSubRegistryWrapper<S>
    bool saveAndChangeMode(S& configs)
    {
        size_t nav = navTop;
        bool chMode = changeMode<T>(configs);
        if (chMode)
            savedNavTop = nav;
        return chMode;
    }

    /**
     * @brief Transitions to a mode without pushing or popping the nav stack.
     *
     * Used for hard resets (`end`, Ctrl-Z) that jump to a fixed mode regardless
     * of navigation depth. Clears the entire nav stack first so subsequent
     * `popMode()` calls find an empty stack.
     *
     * @tparam T  Target @ref CliMode enum value.
     * @tparam S  Registry type for the new mode.
     */
    template <CliMode T, typename S>
    requires config::IsSubRegistryWrapper<S>
    bool resetAndChangeMode(S& configs)
    {
        navTop = 0;
        currentMode = tree.getMode(T);
        execution.changeMode<T, S>(configs);
        return true;
    }

    /**
     * @brief Pops the navigation stack and restores the previous CLI mode.
     *
     * Called by `exit` handlers. Restores the mode, prompt, working directory, and
     * config node exactly as they were when `changeMode` entered the current mode.
     *
     * @return True if there was a mode to pop; false if already at the bottom.
     */
    bool popMode()
    {
        if (navTop == 0) return false;

        NavFrame& frame = frameAt(navTop - 1);
        execution.restoreFromEntry(frame.executorEntry);
        currentMode = frame.modeDir;
        popNavFrame();
        return true;
    }

    /// @brief The current mode's name, which is also the prompt text.
    const std::string_view getPrompt()
    {
        return currentMode.name();
    }

    /// @brief The command-tree entry the session is currently reading commands from.
    tree::ModeEntry& getCurrentMode()
    {
        return currentMode;
    }

    /**
     * @brief Marks the current depth as the point @ref restore returns to.
     *
     * The standalone form of @ref saveAndChangeMode, for detours that decide
     * which mode to enter after the depth has already been recorded.
     */
    void save()
    {
        savedNavTop = navTop;
    }

    /**
     * @brief Returns to the depth recorded by `save()`/`saveAndChangeMode()`.
     * 
     * Pops rather than discards: unwinding only destroys frames, which would
     * leave the mode and prompt sitting wherever the temporary switch left them.
     */
    void restore()
    {
        if (savedNavTop != 0)
            while (navTop > savedNavTop && popMode()) {}
        savedNavTop = 0;
    }

private:
    // SESSION-LEVEL STATE

    static constexpr size_t NAV_STACK_DEPTH = 10; ///< Maximum navigation depth (UserExec → deepest sub-mode).

    /**
     * @brief Constructs a frame in the next free slot.
     *
     * A tree::Command has no empty state, so slots stay raw storage until a mode
     * entry gives them a real one.
     */
    void pushNavFrame(cli::execution::NavEntry entry, const tree::ModeEntry& md)
    {
        new (&navStack[navTop++]) NavFrame(entry, md);
    }

    /// Destroys the top frame.
    void popNavFrame()
    {
        frameAt(--navTop).~NavFrame();
    }

    /// Unwinds back down to depth, destroying every frame above it.
    void unwindNavTo(size_t depth)
    {
        while (navTop > depth) popNavFrame();
    }

    NavFrame& frameAt(size_t i)
    {
        return *std::launder(reinterpret_cast<NavFrame*>(&navStack[i]));
    }

    /// Raw storage; frames are placement-new'd so none are built up front.
    alignas(NavFrame) std::byte navStack[NAV_STACK_DEPTH][sizeof(NavFrame)];
    size_t navTop = 0;      ///< Number of valid frames currently on the stack.
    size_t savedNavTop = 0; ///< Depth @ref restore unwinds to; 0 when no detour is active.

    tree::ModeEntry currentMode; ///< Command-tree entry supplying the current mode's commands.

    tree::CommandTree& tree;                 ///< Shared grammar; supplies a ModeEntry per mode.
    execution::ExecutionManager& execution;  ///< Owns the active mode object and its config pointer.
};
}

#endif // TREE_NAVIGATION_HPP
