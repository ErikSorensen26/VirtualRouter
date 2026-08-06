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
#include "cli/modes/Context.hpp"
#include "cli/tree/CommandTree.h"
#include "configs/RegistryTable.hpp"
#include "configs/SubRegistry.hpp"

namespace cli
{
/**
 * @brief One entry in the navigation history stack.
 *
 * A mode is which grammar the session reads plus which registry its commands
 * write to, so a frame is the tree cursor and the config pointer. Field
 * appliers recover the concrete registry type from the command's configId,
 * which is why nothing here has to remember that type.
 */
struct NavFrame
{
    NavFrame(CliMode md, void* cfg, uint16_t reg, const tree::ModeEntry& dir)
        : mode(md), configPtr(cfg), configRegistry(reg), modeDir(dir)
    {}

    CliMode mode = CliMode::None;  ///< Which mode this frame represents.
    void* configPtr = nullptr;     ///< Registry instance this mode's commands write to.

    /// Which registry configPtr is, so restoring it restores its tag too.
    uint16_t configRegistry = ContextBase::NO_REGISTRY;

    const tree::ModeEntry modeDir; ///< Command-tree node for this mode.
};

class TreeNavigator
{
public:
    TreeNavigator(tree::CommandTree& tr, ContextBase& ctx)
        : tree(tr), context(ctx)
    {}

    /**
     * @brief The registry id for a registry type, or NO_REGISTRY if it has none.
     *
     * A few registry wrappers are not in REGISTRY_ID_LIST -- a templated one has
     * no single entry to hold -- and those stay untagged rather than failing to
     * compile, which is the same allowance @ref cli::execution::utils::visitOne
     * makes for them on the other side.
     */
    template <typename S>
    static constexpr uint16_t registryIdOf()
    {
        if constexpr (requires { typename S::type; })
        {
            if constexpr (config::isRegisteredV<typename S::type>)
                return config::registryIdV<typename S::type>;
        }
        return ContextBase::NO_REGISTRY;
    }

    ~TreeNavigator()
    {
        unwindNavTo(0);
    }

    /**
     * @brief Transitions to a new CLI mode and pushes the current mode onto the nav stack.
     *
     * Updates the working directory pointer and prompt string, captures the current
     * mode onto the nav stack, then points the context at the new mode's registry.
     * Use this for all normal sub-mode entries (interface, router, address-family, etc.)
     * so that `popMode()` can return here automatically.
     *
     * @tparam T  Target @ref CliMode enum value.
     * @tparam S  Registry type for the new mode.
     */
    bool changeMode(CliMode mode, void* configs,
                    uint16_t registry = ContextBase::NO_REGISTRY)
    {
        return changeMode(mode, configs, registry, context.ctx, context.ctxRegistry);
    }

    /**
     * @brief Enters a mode, returning somewhere other than where ctx.ctx points.
     *
     * The two pointers are the same for a plain mode change and differ only when
     * the line rescoped before entering: `router eigrp 1` resolves its binding
     * inside the VRF that `eigrp` selected, but `exit` has to land back on the
     * global config the line started from. Pushing ctx.ctx there would leave the
     * frame holding a VRF pointer under a mode that reads it as a Global.
     *
     * @param mode     Mode to enter.
     * @param configs  Registry the new mode's commands write to.
     * @param registry Which registry @p configs is, as a `config::registryIdV`.
     * @param retTo    Registry to restore on `exit`, in ctx.ctx's place.
     * @param retReg   Which registry @p retTo is.
     */
    bool changeMode(CliMode mode, void* configs, uint16_t registry,
                    void* retTo, uint16_t retReg)
    {
        if (hasMode() && activeMode == mode)
        {
            currentMode = tree.getMode(mode);
            return true;
        }

        if (hasMode() && navTop >= NAV_STACK_DEPTH)
            return false;

        if (hasMode())
            pushNavFrame(activeMode, retTo, retReg, currentMode);
        currentMode = tree.getMode(mode);
        bindMode(mode, configs, registry);
        return true;
    }

    /**
     * @brief Typed overload; the registry is erased once the wrapper is checked.
     *
     * The mode is a plain value -- it is compared, stored, and handed to
     * getMode(), never used as a type -- so only the registry needs a template,
     * and only to keep an arbitrary pointer from being passed as a config.
     */
    template <typename S>
    requires config::IsSubRegistryWrapper<S>
    bool changeMode(CliMode mode, S& configs)
    {
        return changeMode(mode, static_cast<void*>(&configs), registryIdOf<S>());
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
    bool saveAndChangeMode(CliMode mode, void* configs,
                           uint16_t registry = ContextBase::NO_REGISTRY)
    {
        size_t nav = navTop;
        bool chMode = changeMode(mode, configs, registry);
        if (chMode)
            savedNavTop = nav;
        return chMode;
    }

    /// @brief Typed overload; see @ref changeMode(CliMode, S&).
    template <typename S>
    requires config::IsSubRegistryWrapper<S>
    bool saveAndChangeMode(CliMode mode, S& configs)
    {
        return saveAndChangeMode(mode, static_cast<void*>(&configs), registryIdOf<S>());
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
    bool resetAndChangeMode(CliMode mode, void* configs,
                            uint16_t registry = ContextBase::NO_REGISTRY)
    {
        unwindNavTo(0);
        currentMode = tree.getMode(mode);
        bindMode(mode, configs, registry);
        return true;
    }

    /// @brief Typed overload; see @ref changeMode(CliMode, S&).
    template <typename S>
    requires config::IsSubRegistryWrapper<S>
    bool resetAndChangeMode(CliMode mode, S& configs)
    {
        return resetAndChangeMode(mode, static_cast<void*>(&configs), registryIdOf<S>());
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
        bindMode(frame.mode, frame.configPtr, frame.configRegistry);
        currentMode = frame.modeDir;
        popNavFrame();
        return true;
    }

    /// @brief The mode the session is currently in.
    CliMode getMode() const { return activeMode; }

    /// @brief False only before the first mode change, at session init.
    bool hasMode() const { return activeMode != CliMode::None; }

    /// @brief The context field appliers write through.
    ContextBase& getContext() { return context; }

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
    void pushNavFrame(CliMode md, void* cfg, uint16_t reg, const tree::ModeEntry& dir)
    {
        new (&navStack[navTop++]) NavFrame(md, cfg, reg, dir);
    }

    /**
     * @brief Points the session at a mode and the registry its commands write to.
     *
     * Entering and returning to a mode are the same operation here: both land on
     * a mode plus a config pointer, so popMode and changeMode share this rather
     * than reconstructing anything.
     */
    void bindMode(CliMode md, void* cfg, uint16_t reg)
    {
        activeMode = md;
        context.rescope(cfg, reg);
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
    CliMode activeMode = CliMode::None; ///< Mode the session is in; None before the first change.

    tree::CommandTree& tree;                 ///< Shared grammar; supplies a ModeEntry per mode.
    ContextBase& context;                    ///< Carries the config pointer appliers write through.
};
}

#endif // TREE_NAVIGATION_HPP