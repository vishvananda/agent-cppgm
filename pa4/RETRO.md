# PA4 Retrospective

## What Went Well
- Reused PA1/PA2 codepaths directly by embedding `posttoken.cpp`, so tokenization and final post-token emission stayed consistent with previously validated behavior.
- Implemented a full macro engine in `dev/macro.cpp` covering:
  - directive splitting (`#define`/`#undef`) with start-of-line detection,
  - object-like/function-like/variadic macro definitions,
  - redefinition-equivalence checks,
  - `#` stringizing,
  - `##` token pasting with placemarker behavior,
  - rescanning with recursion/nestedness tracking.
- Matched both assignment-local tests and the larger `course/pa4` suite, including tricky edge cases around stringized arguments, `__VA_ARGS__` restrictions, and recursive invocation semantics.

## What Went Poorly
- Nested macro availability rules required multiple iterations; small changes in blacklist/non-invokable propagation can fix one recursion case while regressing another.
- The assignment depends on subtle distinctions between raw arguments, prescanned arguments, and stringized arguments; implementing eager prescan for all parameters caused incorrect failures until usage-driven expansion was added.

## Suggestions
- Add an explicit PA4 semantic table for parameter use contexts (`#x`, `x##y`, `x` normal) and whether each uses raw/prescanned/stringized form.
- Include one short reference trace in the starter docs for the `g(f)(g)(3)`-style nestedness case to make expected non-invokable behavior concrete.
- Provide a small starter test that isolates token-origin behavior across substitution boundaries, since that is the hardest part to infer from prose alone.
