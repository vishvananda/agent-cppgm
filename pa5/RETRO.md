What went well

- Reusing the `pa4` macro engine inside `preproc` kept most of the work in directive orchestration, file handling, and line tracking instead of rebuilding macro expansion again.
- Carrying source file and line through macro expansion made `__LINE__`, `__FILE__`, multiline invocations, and include-driven output compose cleanly once the token origins were correct.

What went poorly

- Physical source line tracking was much more subtle than logical-line processing; using tokenizer emission time was not enough once comments and multiline text sequences entered the picture.
- The assignment mixes several independent mechanisms, including conditional inclusion, file search, pragma handling, `_Pragma`, and line control, so small bookkeeping mistakes surfaced far from the original bug.

Suggestions

- Add one smaller checked-in test that isolates multiline function-like invocation together with `__LINE__`, since that interaction is easy to miss when porting a PA4-style text-sequence processor.
- Call out explicitly that digraph spellings like `%:` and `%:%:` must participate in directive parsing and token pasting, because later PA5 fixtures depend on that PA4-compatible preprocessing behavior.
