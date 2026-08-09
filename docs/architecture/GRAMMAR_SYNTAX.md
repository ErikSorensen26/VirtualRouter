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

There is no `retarget` key. Binding a container field (an owned-list or reference
registry) with no `mode` implicitly retargets write scope to that container for
the rest of the line, then reverts — see [Mid-command registry retarget](#mid-command-registry-retarget).

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

### Mid-command registry retarget

A command node whose `config` names an **owned-list or reference container**
field, and which carries no `mode`, implicitly retargets: the field it binds
becomes the write target for the rest of that line, then reverts when the line
ends. This is the lighter, non-persistent half of a mode change — same
underlying pointer repoint, but scoped to one line instead of the session. It
would let a hypothetical `ip dhcp pool LAN dns-server 8.8.8.8` edit the pool
without leaving the mode the operator is standing in, rather than requiring
`ip dhcp pool LAN` / `dns-server ...` / `exit` as separate lines through a mode.

**No shipped grammar exercises this yet.** Every `config` key currently under
`VirtualRouter/commands/` names a value or tuple field; the only container
bindings in the grammar (`router ospf <1-65535>` → `Global::ROUTER_OSPF`,
`router ospfv3 <1-65535>` → `Global::ROUTER_OSPFV3`) both carry `mode`, so they
stay mode changes, not retargets. The mechanism is implemented and covered by
`CliTreeTest.cpp` but has no first grammar consumer yet.

`mode` wins over inference — a container node that also carries `mode` is a
mode change, not a retarget. Every `config` beneath a retargeted node must
name the retargeted registry (or name none at all); the flattener checks this
by reachability the same way it tracks two-token keys and shared-definition
arguments. A nested retarget rebases for its own subtree and is itself checked
against the outer expectation first. There is no explicit key for this —
inference is deliberate, since a container field names a scope, not a value,
so no other reading applies. (An explicit `retarget` key existed briefly and
was removed once inference covered the same checks with nothing to disagree.)

---

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
| `recursive` | Marks a **parent**: its children form a repeatable set |
| `multi_use` | Within a `recursive` set, this child is not spent when used |
| `recurse_exclude_all` | Within a `recursive` set, using this member ends the set outright — only `<cr>` remains |
| `recurse_hide` | Within a `recursive` set, this member drops out of the re-offer once any other sibling has been used |
| `recurse_show_all` | Within a `recursive` set, using this member waives every sibling's `recurse_hide` for the rest of the line |
| `recurse_exclude` | Within a `recursive` set, using this member drops the sibling names listed in its `recurse_exclude` CSV from the re-offer (the rest of the set stays available) |
| `mode_exit` | Leaves the current mode, as `exit` does |

Any other property name is an error.

### `recursive` marks the parent; siblings are spent as used

Put `recursive` on the node whose **children** are repeatable — unlike the
per-child properties below it, `recursive` itself is the one marked on the
parent. Each child is spent once used: after `metric 100` completes, the
candidate set returns without `metric`. A child marked `multi_use` is the
exception and stays on offer for as long as the operator keeps typing.

`recurse_exclude_all`, `recurse_hide`, `recurse_show_all`, and `recurse_exclude`
refine what a repeat set re-offers after a specific member is used — for
example, a value like `receive-only` that may only open a set and never follow
another sibling (`recurse_hide`), or a threshold argument that waives another
member's `recurse_hide` once given (`recurse_show_all`). A `recurse_exclude_all`
or `recurse_show_all`-style property can sit deeper than the set's direct
child — e.g. on the `WORD` value that completes a `leak-map WORD` pair — since
what matters is which node the operator's last word actually matched, not
which node is the set's immediate child. None of the four imply `recursive` on
their own; the parent still needs the flag.

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
- `deferred`/`resolver` key ids live in their own `deferKeyId` slot, separate
  from `configExt`, so a node may defer a key **and** carry an `enum` or tuple
  binding in the same command — the three no longer contend for one byte.
  `mode` is the one binding still exclusive with `deferred`/`resolver`.
- A `resolver` binds no field of its own. What it resolves is whatever deferred
  commands named the key, wherever they live and in whatever registry. A node
  that names a field of its own resolves into it once the key resolves; one
  that names none inherits the field from whichever resolver its key pairs
  with — used when a shared deferred value has resolvers that write different
  fields (e.g. a filter-list's `WORD`, resolved by either `in` or `out`).

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

`CommandNode` is 32 bytes and that layout **is** the on-disk format. `flags` is
a `uint32_t`; bits 0-19 are allocated as of this writing (bits 16-31 were freed
by the widening, so headroom currently exists) — a new node property still
costs either a widened record plus a `CT_VERSION` bump once the remaining bits
run out, or a bit reclaimed by merging two properties.

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
