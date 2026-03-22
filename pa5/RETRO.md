What went well

- Reusing the PA4 macro engine kept most macro-expansion semantics in one place and made PA5 mostly about directive control, file handling, and line tracking.
- The checked-in tests gave a clear sequence for landing features incrementally: includes, predefined macros, `#line`, and pragma handling.
- Moving PA5 output assembly to a single final post-token emission preserved cross-line and cross-include string literal concatenation without duplicating PA2 logic.

What went poorly

- `__LINE__` was easy to get subtly wrong because the tokenizer collapses multi-line comments, so raw physical line accounting had to be reconstructed separately.
- Multiline function-like macro invocations across physical newlines exposed edge cases where line-oriented flushing and PA4 expansion semantics pulled in opposite directions.
- `_Pragma("once")` and `#pragma once` required file-identity handling, so include-path spelling and actual file identity had to be treated separately.

Suggestions for improving the assignment

- Call out explicitly in the PA5 handout that `__LINE__` uses physical source line numbers even when block comments collapse multiple lines into one preprocessor line.
- Add a smaller checked-in test that isolates function-like macro invocation with the macro name at end-of-line and `(` on the next line.
- Add a short note in the starter material that include processing should preserve path spelling for `__FILE__` but use file identity for once-only suppression.
