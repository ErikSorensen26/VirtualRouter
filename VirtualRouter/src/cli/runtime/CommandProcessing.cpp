// CommandProcessing.cpp

#include "CliSession.h"
#include "CliEngine.h"
#include "Token.hpp"

namespace cli
{
bool CliSession::executeModeParser(const std::span<Token> tokens)
{
    return execution.execute(tokens);
}

} // namespace cli
