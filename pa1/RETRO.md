# PA1 Retrospective

## What Went Well
- Built a complete tokenizer pipeline in `dev/pptoken.cpp` that handles UTF-8 decoding, universal-character-name decoding, trigraph replacement, line splicing, comment handling, and preprocessing-token emission in one pass.
- Matched both assignment-local tests and shared supplemental `course/pa1` tests, including edge cases for raw strings, `<::` tokenization, include-context header-name parsing, and escape-sequence validation.
- Kept all implementation work in shared `dev/` code so the assignment symlink entrypoint remained intact.

## What Went Poorly
- Starting state was still the skeleton stub, so PA1 functionality had to be implemented fully before any downstream assignment work could proceed.
- Raw string behavior required careful context-sensitive handling (phase-1/2 bypass inside raw literals) and took extra iteration to model correctly against fixtures.

## Suggestions
- Add an explicit starter checklist in PA1 docs that calls out required interactions between trigraphs, UCN decoding, and line splicing with a few minimal examples.
- Include one or two focused tests for malformed character literals to make expected error behavior clearer in the baseline suite.
- Consider adding a compact reference state-diagram for include-context `header-name` recognition; this is a common early source of confusion.
