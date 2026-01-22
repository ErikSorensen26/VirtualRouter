// Executioner.hpp

#ifndef EXECUTOR_HPP
#define EXECUTOR_HPP

#include <CliModeParser.hpp>
#include <ContextBase.hpp>

template <typename... Parsers>
class Executor
{
    static_assert(
        (Cli::is_cli_mode_v<Parsers> && ...),
        "All entries must be CliModeParser types"
    );

    // Validate Parsers
    template <CliMode...>
    struct UniqueModes : std::true_type{};

    template <CliMode M, CliMode... Rest>
    struct UniqueModes<M, Rest...>
        : std::bool_constant<
            ((M != Rest) && ...) &&
            UniqueModes<Rest...>::value
        > {};

    static_assert(
        UniqueModes<Parsers::mode...>::value,
        "Duplicate CliMode detected"
    );

    // Type finder
    template <typename>
    static constexpr bool dependent_false_v = false;

    template <CliMode M, typename... Ts>
    struct FindParserImpl;

    template <CliMode M, typename First, typename... Rest>
        requires(First::mode == M)
    struct FindParserImpl<M, First, Rest...>
    {
        using Type = First;
    };

    template <CliMode M, typename First, typename... Rest>
        requires (First::mode != M)
    struct FindParserImpl<M, First, Rest...> : FindParserImpl<M, Rest...> {};

    template <CliMode M>
    struct FindParserImpl<M>
    {
        static_assert(dependent_false_v<std::integral_constant<CliMode, M>>,
                      "No CliModeParser found for the requested CliMode");
        using Type = void;
    };

    // Alias to extract the parser type corresponding to the requested CliMode
    template <CliMode M>
    using FindParser = typename FindParserImpl<M, Parsers...>::Type;

public:

    explicit Executor(CliSession& sess)
        : session(sess)
    {}

    template <typename Parser>
    static bool executeThunk(
        Cli::ContextBase& ctx,
        std::vector<std::string>::const_iterator b,
        std::vector<std::string>::const_iterator e)
    {
        using Ctx = typename Parser::ContextType;
        static_assert(std::is_base_of_v<Cli::ContextBase, Ctx>,
                      "Parser::ContextType must derive from Cli::ContextBase");
        return Parser::execute(static_cast<Ctx&>(ctx), b, e);
    }
    
    template <CliMode M, typename... Args>
    void changeMode(Args&&... args)
    {
        swap();

        using Parser = FindParser<M>;
        using Ctx = typename Parser::ContextType;

        executeFn[head] = executeThunk<Parser>;

        if (modeConfig[head ^ 1])
        {
            modeConfig[head] =
                std::make_unique<Ctx>(*modeConfig[head ^ 1], std::forward<Args>(args)...);
        }
        else
        {
            modeConfig[head] =
                std::make_unique<Ctx>(session, std::forward<Args>(args)...);
        }

        currentMode[head] = M;
    }

    CliMode getMode()
    {
        return currentMode[head];
    }

    Cli::ContextBase& getContext()
    {
        return *modeConfig[head];
    }

    bool execute(const std::vector<std::string>& tokens)
    {
        return executeFn[head](
            *modeConfig[head],
            tokens.begin(),
            tokens.end()
        );
    }

    void revert()
    {
        swap();
    }

private:

    using ExecuteFn = bool (*)(
        Cli::ContextBase&,
        std::vector<std::string>::const_iterator,
        std::vector<std::string>::const_iterator
    );

    void swap() { head = (head == 0) ? 1 : 0; }
    size_t head = 0;

    CliSession& session;
    CliMode currentMode[2];
    std::unique_ptr<Cli::ContextBase> modeConfig[2];
    ExecuteFn executeFn[2] = {};
};

#endif // EXECUTOR_HPP
