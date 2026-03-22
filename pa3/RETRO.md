# PA3 Retrospective

## What Went Well
- Implemented a full controlling-expression parser and evaluator with correct precedence/associativity, including ternary `?:` and both symbolic/alternative operators (`and`, `or`, `not`, etc.).
- Reused PA2 literal parsing to classify integral literals and character literals, while enforcing PA3 line-level `error` behavior for non-integral/invalid tokens.
- Added proper short-circuit evaluation and static result-type propagation for conditional expressions so non-evaluated branches do not trigger runtime errors.

## What Went Poorly
- Including earlier assignment code directly increased coupling and required a small compile-time guard (`CPPGM_POSTTOKEN_NO_MAIN`) to avoid `main` conflicts.
- A signed division overflow edge case (`INT64_MIN / -1`) initially caused `SIGFPE`; this required explicit runtime guarding to match expected `error` output.

## Suggestions
- Add a small PA3 starter utility library for reusable literal-to-intmax/uintmax promotion logic so this does not need to be rebuilt per assignment.
- Include one explicit note in PA3 docs about signed division overflow handling (`INT64_MIN / -1`) alongside divide-by-zero guidance.
- Consider providing a minimal parser skeleton with precedence-function stubs to reduce boilerplate and keep focus on semantics.
