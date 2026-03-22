## What went well

- Replaced the starter stub with a working phase-1 through phase-3 tokenizer in the shared `dev/pptoken.cpp` entrypoint.
- Kept the implementation aligned to the checked-in tests, including course-specific behavior for empty files, raw strings, digraph operators, and escape validation.
- Landed `pa1` with both assignment-local and supplemental course tests passing.

## What went poorly

- Several behaviors in the README-level assignment summary differ from the checked-in references, so matching the repository required test-first adjustments.
- Raw-string interaction with trigraph and UCN handling needed a more lexical translation pass than a naive global replacement pipeline.

## Suggestions

- Add an explicit note to the assignment materials that the checked-in tests are authoritative when they disagree with the prose summary.
- Document the expected empty-file behavior and the raw-string/trigraph exception directly in the PA1 handout.
- Add a short note describing which escape sequences are rejected in the course tests.
