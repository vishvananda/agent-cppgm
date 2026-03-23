What went well

- Reusing the `pa1` tokenizer and `pa2` literal emission helpers kept `macro` focused on preprocessing semantics instead of rebuilding tokenization again.
- Building one expansion engine that handles object-like macros, function-like macros, `#`, `##`, variadics, and recursion made the later course-test fixes local instead of scattered.

What went poorly

- The checked-in `pa4` behavior diverges from the raw `pa2` posttoken rules in a few pasted-token cases, so blindly reusing `posttoken` was not enough.
- Expansion order around stringization and token pasting is easy to get subtly wrong; eager argument expansion and object-like `##` both caused regressions that only showed up in the course suite.

Suggestions

- Add one checked-in test that isolates object-like `##` expansion, since `hash_hash` is an important rule and currently only shows up later in the matrix.
- Call out explicitly that duplicate macro parameters and the relaxed pasted-token UDL behavior are required, because both are course-visible constraints rather than obvious starter-code extensions.
