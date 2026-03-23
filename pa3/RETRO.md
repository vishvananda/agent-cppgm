What went well

- Reusing the `pa2` tokenizer and literal-analysis helpers made the `pa3` evaluator mostly about parsing and integer semantics.
- Parsing with an `evaluate` flag handled short-circuiting and conditional branches cleanly without needing a separate AST pass.

What went poorly

- Embedding the earlier assignments directly exposed some integration friction around `main` definitions and C++11-friendly helper construction.
- The controlling-expression rules mix parsing, type propagation, and “evaluate only one branch” behavior, so small structural mistakes can show up far away in the test matrix.

Suggestions

- Add one small checked-in test that isolates conditional-expression result typing before the broader operator matrices.
- Consider adding lightweight shared headers for reusable tokenizer/literal helpers so later assignments do not need to include earlier `.cpp` files directly.
