## PA1 Retro

### What went well

- The token stream interface and the checked-in fixtures made it straightforward to validate each lexical class.
- Splitting the work into decode, translation, and tokenization passes kept the implementation manageable.
- The course tests were strong at catching edge cases around trigraphs, raw strings, and header-name recognition.

### What went poorly

- Several course-specific rules diverged from a naive reading of the standard, especially raw-string trigraph handling and the `<::` ambiguity.
- Escape-sequence validation took a few iterations because the visible tests only covered the happy path.
- The supplied fixtures did not document some diagnostics up front, so error-message matching was trial and error.

### Suggestions

- Add a short summary of the course-specific deviations near the PA1 spec, especially for raw strings, trigraphs, and `%:` directives.
- Include a couple of negative tests for invalid escapes and raw-string delimiters in the main fixture set, not only the supplemental course set.
- Call out the intended behavior for `%:`/`%:%:` and `<::` directly in the README to reduce guesswork.
