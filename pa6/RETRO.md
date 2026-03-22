# PA6 Retrospective

## What Went Well
- Reused the PA5 preprocessor/tokenization pipeline directly in `recog`, so phase 1-7 behavior stayed consistent with already passing assignments.
- Implemented a grammar-driven recognizer that reads `pa6.gram`, parses EBNF operators (`?`, `*`, `+`, grouping), and executes recursive matching with backtracking.
- Added explicit handling for PA6 special tokens and mock name lookup categories (`class-name`, `template-name`, `typedef-name`, `enum-name`, `namespace-name`).
- Implemented template-angle token reservation logic so close-angle-bracket cases match the checked-in tests, including split `>>` behavior.

## What Went Poorly
- The close-angle-bracket failures were subtle because the generic parser could otherwise reinterpret `>` and `>>` as expression operators inside template arguments.
- Building the parser as a standalone phase required careful adaptation of posttoken output into parse tokens while preserving PA6-specific context-sensitive behavior.

## Suggestions
- Add one focused PA6 starter note with a concrete token-level walkthrough of why `TC1< 1>2 > x1;` is `BAD` and `TC1<(1>2)> x;` is `OK`.
- Provide a small starter utility for loading/parsing `.gram` EBNF into an AST to reduce repeated parser bootstrap effort.
- Include one starter fixture that prints the normalized parse token stream (`TT_IDENTIFIER`, `ST_RSHIFT_1`, etc.) to simplify debugging grammar mismatches.
