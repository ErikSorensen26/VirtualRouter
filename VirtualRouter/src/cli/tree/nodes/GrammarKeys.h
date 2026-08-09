/**
 * @file GrammarKeys.h
 * @brief The vocabulary of a grammar file: every key the flattener recognises.
 *
 * These name things in the JSON source, not in the tree built from it. Once
 * @ref cli::tree::parser::flattenDir has run, nothing here is meaningful --
 * a @ref cli::tree::CommandNode holds offsets and flags, not key names -- so
 * they live apart from the node layout that outlives them.
 *
 * The split matters for more than tidiness: every runtime consumer of the
 * command tree includes @c Command.h, and none of them parse JSON.
 */

#ifndef GRAMMAR_KEYS_H
#define GRAMMAR_KEYS_H

#include <string_view>

namespace cli::tree
{
// Keys as they appear on a command object in a grammar file.
/// @brief "prompt" or "prompt/variant" on a command that enters a mode.
constexpr std::string_view KEY_MODE        = "mode";
constexpr std::string_view KEY_NAME        = "name";
constexpr std::string_view KEY_DESCRIPTION = "description";
constexpr std::string_view KEY_SUBCOMMANDS = "subcommands";
constexpr std::string_view KEY_PROPERTIES  = "properties";
constexpr std::string_view KEY_SUPPORT     = "support";
constexpr std::string_view KEY_CONFIG      = "config";
constexpr std::string_view KEY_ENUM        = "enum";
constexpr std::string_view KEY_ARGS        = "args";
constexpr std::string_view KEY_DEFERRED    = "deferred";
constexpr std::string_view KEY_RESOLVER    = "resolver";
/// @brief CSV of sibling names this `recurse_exclude` member drops from the re-offer.
constexpr std::string_view KEY_RECURSE_EXCLUDE = "recurse_exclude";

/// @brief Marks a value that names an argument rather than being one.
constexpr char ARG_SIGIL = '$';

/// @brief Key of the shared definition block inside the variables file.
constexpr std::string_view KEY_VARIABLES   = "VARIABLES";

/**
 * @brief Every key a command object may carry.
 *
 * Checked against, so a key the flattener does not know is an error rather than
 * something silently dropped. Six misspellings were already in the grammar when
 * this went in -- "descirption", "subcommads" -- and a mistyped "subcommands"
 * costs a whole subtree with nothing to show that it went missing.
 */
constexpr std::string_view COMMAND_KEYS[] = {
    KEY_NAME, KEY_DESCRIPTION, KEY_SUBCOMMANDS, KEY_PROPERTIES,
    KEY_SUPPORT, KEY_CONFIG, KEY_ENUM, KEY_MODE, KEY_ARGS,
    KEY_DEFERRED, KEY_RESOLVER, KEY_RECURSE_EXCLUDE,
};

// Keys of a per-mode grammar file. Each file is one mode: its prompt, the
// registry it configures, and its command list.
constexpr std::string_view KEY_PROMPT      = "prompt";
constexpr std::string_view KEY_REGISTRY    = "registry";
constexpr std::string_view KEY_COMMANDS    = "commands";
constexpr std::string_view KEY_VARIANT     = "variant";

/// @brief Basename, without extension, of the shared variable file.
constexpr std::string_view VARIABLES_STEM  = "Variables";
}

#endif // GRAMMAR_KEYS_H
