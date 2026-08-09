# Command Tree Grammar — JSON Syntax Reference

The CLI grammar lives in `VirtualRouter/commands/` as a tree of `.json` files.
`cli::tree::parser::flattenDir` reads every `.json` under that directory
(recursively), resolves it, and emits the flat binary `Commands.bin` that the
runtime maps read-only.

This document is the complete syntax. Every key, property, and constraint listed
here is enforced by the flattener — an unknown key or an unresolvable reference
throws and no binary is written.

**Source of truth:** `cli/tree/nodes/GrammarKeys.h` (the key vocabulary) and
`cli/tree/TreeParser.cpp` (all validation).

---

## Table of Contents

- [File Types](#file-types)
- [Mode Files](#mode-files)
- [Command Objects](#command-objects)
- [Placeholders](#placeholders)
- [Binding Configuration](#binding-configuration)
- [Properties](#properties)
- [Shared Definitions and Arguments](#shared-definitions-and-arguments)
- [Deferred Values and Resolvers](#deferred-values-and-resolvers)
- [Constraints and Common Errors](#constraints-and-common-errors)

---

## File Types

A `.json` file under `commands/` is one of three things:

| File | Identified by | Purpose |
|---|---|---|
| **Mode file** | has `prompt` **and** `commands` | Defines one CLI mode and its command list |
| **Variables file** | basename `Variables` | Shared definitions reusable across modes |
| **Ignored** | has neither `prompt` nor `commands` | Skipped silently |

A file with `prompt` but no `commands` (or the reverse) is an error — that is a
half-written mode file, not an intentionally inert one.

Directory nesting (`commands/router/Eigrp.json`) is organisational only. Modes
are keyed by their `prompt` and `variant`, never by path or filename.

---

## Mode Files

```json
{
    "prompt": "(config-router)#",
    "variant": "eigrp_classic",
    "registry": "Eigrp",
    "commands": [ /* command objects */ ]
}
```

| Key | Required | Type | Meaning |
|---|---|---|---|
| `prompt` | yes | string | The prompt text; this is the mode's identity for `getMode` lookup |
| `commands` | yes | array | The mode's top-level command objects |
| `registry` | no | string | Registry these commands configure; omit for a mode that binds no fields |
| `variant` | no | string | Distinguishes modes sharing a prompt |

### Variants

Several modes share a prompt — `(config-router)#` serves EIGRP, OSPF, and BGP.
`variant` separates them, and it is what `CliMode`'s path table matches against
(see `CLI_MODE_TABLE` in `cli/modes/Mode.hpp`).

Two files sharing a prompt where neither names a variant is an error: the second
row would be permanently unreachable, since `findMode` returns the first match.

A single file can also contribute several variants by wrapping its commands in a
dictionary keyed by variant name:

```json
{
    "prompt": "(config-router-af)#",
    "registry": "Ospf",
    "commands": {
        "ospf-ipv4": [ /* commands */ ],
        "ospf-ipv6": [ /* commands */ ]
    }
}
```

Use the top-level `variant` key **or** a variant dictionary, never both.
Variant dictionaries cannot nest.

---

## Command Objects

Every node in the tree is a command object. Only `name` and `description` are
required.

```json
{
    "description": "Enable automatic network number summarization",
    "name": "auto-summary",
    "config": "Eigrp::AUTO_SUMMARIZATION",
    "subcommands": [
        { "description": "", "name": "<cr>" }
    ]
}
```

### Every recognised key

| Key | Type | Meaning |
|---|---|---|
| `name` | string | The word the user types, or a placeholder. **Required** |
| `description` | string | Help text shown by `?`. **Required** (may be empty) |
| `subcommands` | array | Child nodes; the candidates for the next word |
| `config` | string | Registry field this command writes |
| `enum` | string | Enum member to set on the bound field |
| `mode` | string | Mode this command enters, as `"prompt"` or `"prompt/variant"` |
| `properties` | array | Behaviour flags; see [Properties](#properties) |
| `support` | bool | Marks the command as supported/implemented |
| `args` | object | Arguments passed to a shared definition |
| `deferred` | string | Hold this value under a key instead of writing now |
| `resolver` | string | This command supplies the value a deferred key waits on |

**Any other key is an error.** This is deliberate: six misspellings
(`descirption`, `subcommads`) were already in the grammar when the check went
in, and a mistyped `subcommands` silently costs an entire subtree.

### Ending a command

A command is only executable if it can terminate. Mark that with a `<cr>` child:

```json
"subcommands": [
    { "description": "", "name": "<cr>" }
]
```

`<cr>` is an ordinary child node, not a flag — the runtime asks whether it is
among a node's children (`hasCarriageReturn`) or its only child
(`hasExclusiveCR`). Without it, the command is a prefix only and the parser
treats the line as incomplete.

This is **not** checked by the flattener. A command missing its `<cr>` builds
cleanly and then refuses to run, which is the most common way a newly added
command appears broken.

---

## Placeholders

A `name` that is a placeholder accepts a user-supplied value rather than
matching literal text. Placeholders are matched by *validating the value*, never
by comparing the placeholder text — so the literal string `A.B.C.D` cannot
satisfy the address it stands for.

| Placeholder | Accepts |
|---|---|
| `WORD` | A hostname, name, or identifier |
| `LINE` | The entire remainder of the input line |
| `A.B.C.D` | An IPv4 address |
| `A.B.C.D/nn` | An IPv4 prefix |
| `X:X:X:X::X` | An IPv6 address |
| `X:X:X:X::X/<0-128>` | An IPv6 prefix |
| `H.H.H` | A MAC address |
| `<lo-hi>` | A number in the inclusive range, e.g. `<1-65535>` |

Defined in `matchVolatilePattern` (`cli/session/Token.hpp`). Anything not on
this list is a fixed keyword.

**Matching order:** keywords are tried first, by name, exact before prefix — so
`int` reaches `interface`. Placeholders are only tested when no keyword matches.

### Port placeholders

A bare `<N>` (such as `<0>`) is a *port placeholder*. The flattener cannot know
how many ports a chassis has, so it emits the placeholder unresolved and
`CommandTree::applyPortCounts` numbers it at runtime against the hardware
config — `<0>` under GigabitEthernet on a 10-port box becomes `<0-9>`.

An interface type with no configured ports keeps its bare placeholder and
therefore matches nothing, which is correct for hardware that does not exist.

---

## Binding Configuration

### `config` — naming the field

Three accepted forms:

| Form | Example | Binds |
|---|---|---|
| `Registry::field` | `"Eigrp::AUTO_SUMMARIZATION"` | A registry field |
| `Registry::field::member` | `"Eigrp::NEIGHBOR::iface"` | A tuple member of that field |
| `Schema::member` | `"DistributeList::filterList"` | A tuple member, when the schema name is unambiguous |

The short `Schema::member` form only resolves when exactly one field across all
registries stores that tuple schema. When several do, the flattener rejects it
and tells you to use the long form.

Naming a registry with no field (`"Eigrp"`) is an error.

### `enum` — setting an enum member

Written as `Type::MEMBER`, and requires a `config` on the same node:

```json
{
    "name": "route-map",
    "description": "Filter prefixes based on the route-map",
    "config": "DistributeList::filterList",
    "enum": "DistributeListType::ROUTE_MAP"
}
```

The flattener verifies the field is a registered enum type, that the named type
matches the field's actual type, and that the member exists. On a tuple member,
the member's own type is the authority — the field stores a whole tuple and is
never itself an enum.

**Bitmap fields** take `enum` keys the same way. A run of flags typed on one
line is recorded as a single command, attributed to the node that opened the run.

### `mode` — entering a mode

```json
{ "name": "eigrp", "description": "...", "mode": "(config-router)#/eigrp_classic" }
```

The value is a prompt, optionally `prompt/variant`. A mode change cannot also
carry an `enum` or tuple binding — `configExt` holds one meaning at a time.

**The value resolves against `CLI_MODE_TABLE` in `cli/modes/Mode.hpp`, not
against the mode files.** Adding a new mode therefore takes two edits: a row in
that table *and* a mode file whose `prompt`/`variant` match it. A `mode` naming
something absent from the table is an error even when a mode file for it exists.

### Two-token values

Some keys span two words (`interface Vlan 10`, an IPv4 prefix). These are
spelled as a parent node and its immediate child, **both binding the same
field**:

```json
{
    "name": "Vlan",
    "description": "Vlan interface",
    "config": "Eigrp::NEIGHBOR::iface",
    "subcommands": [
        { "name": "<0-4094>", "description": "", "config": "Eigrp::NEIGHBOR::iface" }
    ]
}
```

The pair is joined when the line commits, not when tokens are staged, so a
half-matched key is never written. This adjacency is the one case exempt from
the double-write warning.

---

## Properties

`"properties": ["recursive", "negate_hide"]`

| Property | Effect |
|---|---|
| `negate` | Command accepts a `no` prefix |
| `negate_all` | `no` on this command clears all of the bound collection |
| `negate_hide` | Hide this command from help in its negated form |
| `negate_show` | Show this command in help only in its negated form |
| `recursive` | This sibling is repeatable; spent once used |
| `subcmd_sequence` | Children may be typed in any order and repeat |
| `subcmd_single_use` | Within a `subcmd_sequence`, this child is spent once used |
| `mode_exit` | Leaves the current mode, as `exit` does |

Any other property name is an error.

### `recursive` marks siblings, not the parent

Put `recursive` on **each repeatable sibling**, never on the node that contains
them. After `metric 100` completes, the candidate set returns without `metric`.

A `subcmd_sequence` returns whole, except children marked `subcmd_single_use`.

Repeat tracking uses a bitmask, which caps a repeatable set at 64 members
(`MAX_TRACKED_SIBLINGS`).

---

## Shared Definitions and Arguments

`Variables.json` holds command fragments reusable across modes. A definition is
a name mapped to an array of command objects:

```json
{
    "GigabitEthernet": [
        {
            "description": "GigabitEthernet interface number",
            "name": "<0>",
            "config": "$CONFIG",
            "mode": "$MODE"
        }
    ]
}
```

Reference one by using `<name>` as a `name`, and pass its arguments with `args`:

```json
{
    "description": "",
    "name": "<interface>",
    "args": {
        "CONFIG": "Eigrp::NEIGHBOR::iface"
    }
}
```

### How arguments bind

- **Call sites** pass values via `args`.
- **Definitions** receive them via `$NAME` in any string value.
- A value that is itself `$NAME` **forwards** what the calling site was given,
  which is how arguments thread through nested definitions.

Passing the same argument name twice at one site is an error.

> **An unsatisfied `$VAR` is silently dropped, not an error.**
>
> If a definition references `$CONFIG` and the call site passes no `CONFIG`, the
> binding is dropped — the node is emitted with no config. It is not a build
> failure. A shared definition invoked from a mode that does not pass every
> argument it uses will therefore produce commands that parse and execute but
> write nothing.

Because a definition's `config` strings name concrete registries, a shared
definition is **registry-bound**: `distribute_list` writing `Eigrp::...` writes
into EIGRP even when OSPF calls it. Pass the target field as an argument rather
than hardcoding it when a definition is meant to serve several modes.

---

## Deferred Values and Resolvers

Some values cannot be written when they are typed, because the object they
belong to does not exist yet. `deferred` holds the value under a key; `resolver`
marks the command that supplies what the key was waiting for.

```json
{ "name": "<1-65535>", "description": "AS number", "config": "Bgp::LOCAL_AS", "deferred": "asn" }
```

Rules the flattener enforces:

- `deferred` requires a `config` — a held value still needs to know its target.
- Every `deferred` key needs a matching `resolver` somewhere in the grammar, and
  every `resolver` needs a matching `deferred`. An unpaired key on either side
  is an error.
- A node cannot be both `deferred` and a `resolver`.
- Neither can coexist with `enum` or a tuple binding: all three want `configExt`.
- A `resolver` binds no field of its own. What it resolves is whatever deferred
  commands named the key, wherever they live and in whatever registry.

---

## Constraints and Common Errors

### Hard limits

| Limit | Value | Consequence of exceeding |
|---|---|---|
| Subcommands per node | 65535 | Flattener throws |
| Repeatable set size | 64 (`MAX_TRACKED_SIBLINGS`) | Flattener throws |
| Tuple member index | 254 | Does not fit `configExt` |
| Enum-valued tuple member | 15 (4 bits) | Member and enum index share the byte |
| Registry id | `CONFIG_FIELD_MAX_REGISTRY` | Does not fit `configId` |

`CommandNode` is 16 bytes and that layout **is** the on-disk format. All 16 flag
bits are currently allocated, so a new node property costs either a widened
record plus a `CT_VERSION` bump, or a bit reclaimed by merging two properties.

### Writing one field twice on one line

Two nodes on the same root-to-leaf path binding the same field means the last
one executed wins, silently. The flattener detects this by reachability and
warns on stderr.

The adjacent parent-child pair that spells a two-token value is exempt. That
exemption is structural rather than type-aware, so a genuine double-write
spelled as a direct parent-child pair is **not** reported.

### Cache staleness

`Commands.bin` is regenerated automatically when the format version, the
registry signature, or the hash of the grammar sources changes. Editing a
`.json` file is enough to trigger a rebuild — deleting the cache by hand is no
longer necessary.

### Error message shape

Every failure names the offending command:

```
cli::grammar: 'auto-summary' names unknown field 'AUTO_SUMMARY' in registry 'Eigrp'
cli::grammar: unknown property 'recursve'
cli::grammar: unknown key 'descirption' on command object 'bfd'
```

If the flattener throws, no binary is written — an unresolvable grammar never
reaches a mapped tree.
