# ADR-011. Correlation rule language

<!-- language-links -->
[English](0011-correlation-rule-language.md) | [Русский](../ru/adr/0011-correlation-rule-language.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-009, ADR-010, [aggregates.md](../domain/aggregates.md#33-the-correlationrule-aggregate)

## Context

Administrators define correlation rules without programming. Rules must validate before activation, execute deterministically and efficiently at 50,000 events/s on three instances, support historical runs, and prevent arbitrary code execution in the core. Unbounded language expansion is an architectural risk.

## Considered options

| Option | Advantages | Disadvantages |
|---|---|---|
| Embedded scripting language, such as Lua or JavaScript | Maximum flexibility | Turing completeness permits infinite loops and unbounded memory; difficult validation and analysis; security risks; rules are unsuitable for structured editors and dry-run analysis |
| SQL-like event language, such as Esper EPL or Flink SQL | Expressiveness | Complex custom parser and planner; high administrator learning cost |
| Entirely declarative model without expressions | Simple validation | Attribute conditions and priority calculation require continual model expansion |
| **Declarative rule kinds with CEL conditions and calculations** | Rule structure, including kind, window, key and sequence steps, is declarative and analyzable. CEL conditions and priority are not Turing-complete, have cost estimation and compile-time typing. A C++ implementation, cel-cpp under Apache 2.0, uses the same Protobuf stack | Dependency on cel-cpp, abseil and protobuf; CEL restrictions must be documented for administrators |

## Decision

1. A rule is a document, YAML in the editor and Protobuf in storage and `psim.config.v1`, with fixed kinds `single`, `conjunction`, `sequence`, `threshold` and `absence`; see [aggregates.md](../domain/aggregates.md#33-the-correlationrule-aggregate).
2. Expression fields `condition`, `priority` and `summary` use a restricted CEL-syntax subset over typed events (`event.type`, `event.severity`, `event.attributes[...]`, `event.zone_id`) and rule variables. The implementation is a custom typed interpreter without a cel-cpp dependency. Expressions are compiled and type-checked during validation, with a bounded evaluation cost. Full CEL compatibility is not claimed.
3. The taxonomy-type filter is a separate declarative field, allowing event-type rule indexing and expression evaluation only for matching events.
4. New rule kinds require an ADR to control language expansion.

## Consequences

- Compiled expressions are immutable and reusable. The live engine manages cache versions according to the [live correlation procedure](../engineering/live-correlation.md).
- Correlation Engine load tests must establish expression-evaluation cost on the processing path. Expanding the subset or adopting cel-cpp requires a separate decision and renewed limit checks.
- Rule management and historical runs use the `management` role of the same Correlation Engine executable. Historical runs use separate read credentials, store results in their own PostgreSQL schema and do not start the live correlation stream.

## Verification

CI fuzzes the parser and validator for ten minutes without crashes. Release qualification requires 24 hours per target. Rule-evaluation benchmarks are checked separately.

### Parser and validator

The implemented interpreter supports a restricted CEL subset; full compatibility is not claimed. Supported features are `bool`, `int64`, `string`, typed event and rule fields, string attributes through `event.attributes['key']`, presence checks using `has(field)`, logical operations, comparisons, integer arithmetic, conditional `condition ? a : b`, string concatenation, and `startsWith`, `endsWith` and `contains`. Logical and conditional expressions evaluate only the required branch. Loops, comprehensions, regular expressions, dynamic calls, implicit conversions and external-system access are unsupported.

Limits are 65,536 bytes per JSON/YAML document, 4000 bytes per expression, 512 AST nodes and depth 32, and 4096 bytes per string value or result. YAML anchors, aliases and multiple documents are prohibited; YAML is limited to 4096 nodes and depth 32. Evaluation has a separate operation budget. Integer overflow, division by zero and a missing required field return errors. A priority expression is accepted only when its result range can be statically proved to remain within 1..4.

`task fuzz:rules` checks expressions, JSON/YAML document parsing and structural validation of all five rule kinds under ASan/UBSan for at least 600 seconds. The corpus, log and report, including actual duration and executable SHA-256, are retained in `build/rules/fuzz/`. The command is part of `task ci` for changes to relevant components. Early termination does not count even with exit code 0; an error or timeout leaves a failed result.

Rule semantics, historical-run limitations and verification commands are described in the [guide](../engineering/correlation-rules.md).
