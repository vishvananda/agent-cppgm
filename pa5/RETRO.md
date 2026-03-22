PA5 Retro

What went well:
- Reusing the PA4 macro engine made it practical to build the preprocessor incrementally instead of starting from scratch.
- The checked-in tests were good at exposing the real edge cases: predefined macros, `#line`, and macro calls spanning blank lines.
- Keeping the implementation in `dev/` and sharing it through the assignment symlinks avoided duplicated fixes.

What went poorly:
- The first cut treated preprocessing too much like a line-by-line text transform, which broke cross-line macro arguments and line-sensitive builtins.
- `__LINE__` was trickier than it looked because the tokenizer had not been carrying source-line metadata.
- The interaction between directives and buffered macro expansion was easy to get wrong without a more explicit token-stream model.

Suggestions:
- Add one or two focused tests for macro invocations that span blank lines or comments, since those catch the buffering bug quickly.
- Call out the expected `__LINE__` behavior more explicitly in the assignment notes, especially around multiline comments.
- A small note in the starter about preserving source line metadata through the shared tokenizer would save time in PA5.
