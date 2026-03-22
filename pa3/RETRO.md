## What went well

- Reused the existing phase-1 through phase-3 and literal-decoding machinery so `ctrlexpr` could focus on the PA3 parser and evaluator instead of reimplementing the earlier pipeline again.
- Covered the full checked-in controlling-expression grammar, including lazy evaluation for `?:`, `&&`, and `||`, which was necessary for the repository’s error-handling tests.
- Matched both the assignment-local and course PA3 suites, including the overflow and shift edge cases.

## What went poorly

- The current implementation pulls `posttoken.cpp` into `ctrlexpr.cpp` with a macro rename to reuse helpers, which is expedient but not a clean long-term shared-code boundary.
- Several PA3 edge cases depend on course-defined behavior around runtime errors and integer semantics, so the evaluator needed repository-specific guards instead of just straightforward operator execution.

## Suggestions

- Extract the translation and literal helper code into shared `dev/src/` modules before PA4+ build on the same functionality again.
- Add a short PA3 note documenting the required lazy-evaluation behavior for `?:`, `&&`, and `||`.
- Document the course-defined overflow and shift-error cases directly in the handout examples.
