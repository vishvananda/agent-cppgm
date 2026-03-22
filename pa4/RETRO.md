What went well

- Reusing the PA2 tokenizer and literal parsing code kept PA4 output aligned with the checked-in token expectations.
- Moving macro expansion onto shared token state made the later recursion and rescan fixes possible without restarting the implementation.
- The checked-in local and course tests were strong enough to pin down several non-obvious behaviors, especially around `##`, `#`, recursion, and invalid `__VA_ARGS__` use.

What went poorly

- The first macro-expansion model was too global. A single active-set was not enough once the tests started depending on token-by-token non-replacement behavior.
- Several late course failures came from small parser/validator gaps, so the implementation needed repeated passes over edge cases that were easy to miss early.
- Buffering non-directive text across lines was necessary, but it also exposed more attachment/rescan edge cases than the original per-line flow.

Suggestions

- The assignment handout should say explicitly which behaviors follow the checked-in tests rather than a broader standard reading, especially for empty function-like invocations, wrong-arity invocations, and cross-line invocation handling.
- A short note about placemarkers and `##` consuming only the immediate adjacent preprocessing tokens would make the token-pasting requirements much clearer.
- Adding one or two starter tests for course-only cases like duplicate parameter names, `#undef __VA_ARGS__`, and stringized bad invocations would surface the expected behavior earlier.
