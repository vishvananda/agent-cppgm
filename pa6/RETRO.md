What went well

- Reusing the PA5 preprocessor directly let PA6 start from the real preprocessed token stream instead of duplicating translation logic.
- Driving the recognizer from `pa6.gram` kept most of the syntax surface in the checked-in grammar instead of hardcoding every production in C++.
- The mock name lookup rules were straightforward to layer on once the parser had explicit hooks for semantic-name nonterminals.

What went poorly

- The special close-angle handling is not expressible cleanly with a naive grammar-only parser; it needed custom parsing logic around `simple-template-id`.
- A fully generic backtracking parser exposes C++ ambiguity quickly, so some places still need targeted semantic constraints instead of pure grammar expansion.
- Making `preproc.cpp` safely reusable from PA6 required another round of entrypoint and helper refactoring after PA5 was already committed.

Suggestions for improving the assignment

- Call out earlier in the PA6 handout that `simple-template-id` and close-angle matching need custom handling beyond a literal EBNF translation.
- Include one small starter example showing how the mock name categories should restrict parsing in declaration-vs-expression ambiguities.
- Ship the grammar file in a more machine-friendly format alongside the human-readable version so parser-generator-style solutions do less input cleanup.
