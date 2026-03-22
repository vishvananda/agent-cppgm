# PA4 Retrospective

## What went well
- The earlier tokenizer and posttoken work carried over cleanly into macro expansion.
- Building the expander in small steps made it easy to keep the local and course tests green.
- The checked tests were specific enough to expose the exact edge cases for stringization, placemarkers, and `##`.

## What went poorly
- The first cut at macro expansion expanded arguments too eagerly, which caused failures in stringized cases.
- I initially treated `##` too strictly and had to relax paste handling for multi-token rescans.
- Some course cases depended on subtle phase-order behavior around trigraphs and raw strings, which took a few iterations to isolate.

## Suggestions
- Add one or two more course tests around stringized arguments that contain macro calls and malformed invocations.
- Document the expected phase ordering around macro stringization, trigraphs, and raw strings more explicitly in the assignment notes.
- Keep a focused test for UCNs in pasted tokens, since that was the hardest edge case to infer from the existing suite.
