# Project Architecture Specification

This document defines the required structure and reasoning format for describing system architecture. It is a strict schema: every section must be completed when used for a real system. No section may be omitted or left partially defined in production use.

This document does not describe a specific system. It defines the required format for describing any system.

---

## 1. Required Completeness Rules

- Every section labeled “required” must be filled when applied to a real system.
- No placeholder text (e.g. “TBD”, “...”, “Section Name”) is permitted in finalized instances.
- Every subsystem must define:
  - Responsibility
  - Key design decision
  - Rejected alternatives
  - System invariants
- Every cross-cutting decision must define:
  - Scope of impact
  - System-wide problem solved
  - Cost of reversal

If any of these are missing, the document is considered incomplete.

---

## 2. Table of Contents (Fixed Structure)

- Design Philosophy
- Subsystem Architecture

No additional top-level sections may be introduced without updating this specification.

---

## 3. Design Philosophy (System-Wide Invariants)

This section defines non-negotiable system constraints.

### Required Format per Principle

Each principle must follow this structure exactly:

> **Invariant Statement**
>
> Constraint context: The system limitation, requirement, or failure condition that forces this rule.
>
> Mechanism: The architectural or algorithmic design used to enforce the constraint. Must include explicit trade-offs.
>
> Status: This is a foundational invariant and not a tunable optimization.

### Rule

- Each principle must map to a real system constraint.
- Vague or decorative principles are not allowed.

---

## 4. Subsystem Architecture

Each subsystem must be defined as a logically independent boundary.

### Required Subsystem Format

For every subsystem:

1. **Purpose**
   - What problem it solves
   - Why it cannot be merged into another subsystem

2. **Design Decision**
   - Primary architectural choice
   - Explicit rejected alternatives and reasoning

3. **Invariants**
   - What guarantees it provides to the rest of the system

### Subsystem Detail Block (Optional Depth)

Where a decision affects multiple subsystems, name the affected subsystems
explicitly in the constraint or trade-off line rather than separating them
into a standalone section.

> **Decision: [Name]**
>
> Constraint: What requirement forces this decision. If the decision affects
> more than one subsystem, name all affected subsystems here.
>
> Mechanism: How the design satisfies the constraint.
>
> Trade-offs: What is made more complex, slower, or more constrained. Include
> the cost of reversing this decision — what would need to change and where.

---

## 5. Validation Rules

A completed instance of this document is only valid if:

- All required fields are filled
- No placeholders exist
- Each subsystem has at minimum:
  - one design decision
  - one invariant
- Any decision that affects more than one subsystem names all affected
  subsystems in its constraint or trade-off line
- All language is deterministic (no ambiguity such as “may”, “could”, unless explicitly justified)

---

## 6. Intent

This specification exists to enforce:

- architectural clarity
- reproducibility of reasoning
- elimination of implicit design assumptions
- consistent documentation across systems