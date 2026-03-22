# PA2 Retrospective

## What Went Well
- Reused the PA1 tokenizer implementation directly inside `posttoken` so phase 1-3 behavior stayed consistent with PA1 test-validated behavior.
- Implemented full PA2 post-token analysis for:
  - keyword/operator/simple mapping
  - integer and floating literal classification + type selection
  - character/string literal escape decoding and ABI-form hexdumps
  - user-defined literal validation with underscore-prefixed suffix rules
  - phase-6 string literal concatenation (including encoding and UD-suffix conflict handling)
- Passed both assignment-local and `tests/course/pa2` suites, including the large string-concatenation stress test.

## What Went Poorly
- The final hard concatenation case exposed a subtle behavior mismatch: invalid runs must still report the full maximal string-literal sequence source, not just the prefix processed before detection.
- Keeping tokenizer reuse via direct include is pragmatic but increases coupling and compile-unit size.

## Suggestions
- Provide a compact PA2 grammar appendix showing exactly which suffix forms are valid for built-in literals vs user-defined literals.
- Add one focused starter note for phase-6 concatenation explaining that error reporting still uses the entire maximal run source.
- Consider extracting the PA1 tokenizer into shared `dev/src` library code in starter scaffolding to encourage cleaner reuse across assignments.
