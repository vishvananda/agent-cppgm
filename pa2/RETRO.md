What went well:
- Reusing `pptoken` as a shared tokenizer kept PA2 focused on classification instead of duplicating preprocessing logic.
- The checked-in tests made it straightforward to tighten integer, character, and raw-string behavior to the assignment's exact rules.
- Running focused fixtures early helped isolate the raw-string and float-syntax edge cases quickly.

What went poorly:
- The initial `pp-number` handling was too permissive and accepted malformed suffix and floating forms.
- Raw-string phase ordering was easy to get wrong; line splicing and UCN decoding needed to respect raw bodies.
- I spent too much time cleaning up small control-flow mistakes in the large classifier block.

Suggestions:
- Add a few more narrow course tests for malformed floating literals like `2.2.2` and raw strings with line-spliced bodies.
- The assignment would benefit from a clearer note that raw strings should be preserved through earlier translation steps.
- A small table of valid integer suffix combinations would help avoid the mixed-case suffix mistakes.
