What went well

- Reusing the working `pa1` tokenizer directly let `posttoken` focus on semantic analysis instead of rebuilding translation phases again.
- Building one shared literal decoder for escapes, UTF handling, and raw-string bodies covered character literals, ordinary strings, and concatenation with the same code path.

What went poorly

- The string-concatenation rules took a couple of iterations because invalid runs need to preserve the maximal string sequence source, not just the first bad token.
- Numeric literal handling exposed several course-specific constraints around user-defined suffixes and invalid standard suffix combinations that were easy to overgeneralize at first.

Suggestions

- Call out explicitly in the assignment text that numeric user-defined suffixes are expected to start with `_`, since the tests rely on that distinction heavily.
- Add one smaller checked-in concat test that demonstrates “bad string token poisons the whole maximal sequence” before the large matrix case.
