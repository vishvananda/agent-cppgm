## What went well

- Extended the shared pipeline from `pa1` into `posttoken` so PA2 now reuses the same phase-1 through phase-3 behavior before post-token analysis.
- Implemented enough literal analysis to cover the checked-in integer, floating, character, string, raw-string, and string-concatenation cases.
- Matched both the assignment-local tests and the supplemental course tests, including the repository-specific concatenation and escape edge cases.

## What went poorly

- Keeping the PA2 behavior aligned with the checked-in references required several course-specific adjustments beyond a straightforward standard reading, especially around string concatenation and ordinary character literal typing.
- Because PA1 helpers were not yet shared in `dev/src/`, `posttoken.cpp` had to duplicate tokenizer machinery that should eventually be refactored.

## Suggestions

- Move the phase-1 through phase-3 tokenizer into shared `dev/src/` code before later assignments build on it again.
- Add a short PA2 note summarizing the repository’s string-literal concatenation rules, especially how user-defined suffixes interact with mixed encodings.
- Add a small reference table for ordinary character literal typing so the `char` versus `int` cases are explicit.
