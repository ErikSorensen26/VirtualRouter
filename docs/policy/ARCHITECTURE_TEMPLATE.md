# [Project] — Architecture

This document explains *why* the system is built the way it is. It does not
describe what files exist or list class members — the code does that. Each
section answers the question: "why was this done this way, and what would
break if it were done differently?"

---

## Table of Contents

- [Design Philosophy](#design-philosophy)
1. [Section Name](#1-section-name)
2. ...

---

## Design Philosophy

State the core convictions that shaped every decision. These should be
principles strong enough to resolve ambiguous future design questions —
not a list of features.

Each principle follows this pattern:

> **Title that reads as a conviction, not a category.**
>
> One paragraph naming the problem or constraint that forced this choice.
> What breaks, slows down, or becomes incorrect if you ignore this?
>
> One paragraph explaining the mechanism chosen and why it satisfies the
> constraint. Be specific — name the data structure, the pattern, the
> tradeoff accepted.
>
> An optional closing sentence making clear this is a foundational
> assumption, not an optimization added later.

### Example principle

**The data plane must never yield to the control plane.**

Packet forwarding happens on nanosecond timescales. Route updates and
protocol convergence happen on millisecond timescales. If a forwarding
thread ever blocked waiting for a control-plane lock, jitter would be
unbounded and latency guarantees impossible.

The FIB is protected by RCU. A forwarding thread takes one memory barrier
and proceeds without lock acquisition. The control plane publishes a new FIB
entry atomically and retires the old one after all readers drain. A BGP
convergence event has zero impact on forwarding throughput.

This is the foundational constraint the entire threading model is built
around — not a performance optimization added after the fact.

---

## 1. Section Name

Each section answers:
1. What problem does this subsystem solve, and why does it need to exist as
   a distinct thing rather than being inlined elsewhere?
2. What was the key design decision, and what alternatives were rejected?
3. What invariants does the rest of the system depend on from this subsystem?

Avoid describing the class hierarchy or file layout — that belongs in code
comments. Focus on the reasoning.

### Subsection

If a specific design decision within the subsystem needs its own explanation,
give it a subsection. Lead with the constraint or goal, then the mechanism,
then the consequence.

> **Why [mechanism], not [obvious alternative].**
>
> [The constraint that ruled out the alternative.]
>
> [How the chosen mechanism satisfies it, and what it costs.]

---

## Cross-Cutting Decisions

For decisions that affected many subsystems simultaneously — threading model,
ownership model, type-safety choices, etc. These belong here rather than
scattered across protocol sections.

Each entry:
- Names the decision
- Explains what problem it solved system-wide
- Explains what it would cost to change it now
