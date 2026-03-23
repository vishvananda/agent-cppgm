What went well

- Reusing the PA5 preprocessor and PA2 token-classification helpers kept `recog` focused on syntax instead of rebuilding translation phases 1 through 7 again.
- Loading `pa6.gram` directly and layering only a few targeted overrides on top made it possible to cover the full checked-in grammar without hand-writing every recursive-descent function first.

What went poorly

- The angle-bracket rules are easy to get wrong if template names are allowed to fall back to plain identifiers, because the parser will silently accept `>` as a relational operator instead of a template close.
- `decl-specifier-seq` ambiguity is not obvious from the raw grammar alone; the extra semantic rule from the README is necessary to keep declaration parsing from drifting into expression parses.

Suggestions

- Add one smaller checked-in test that isolates a template-name followed by `<` in expression position, since that is the key failure mode behind the closing-angle-bracket cases.
- Call out in the starter comments that a grammar-driven parser is viable but still needs explicit hooks for mock name lookup, `decl-specifier-seq`, and angle-bracket handling.
