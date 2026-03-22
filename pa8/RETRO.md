What went well

- Reusing the PA7 parser and namespace/type machinery kept PA8 focused on initialization, linkage, and image layout instead of rebuilding the front end again.
- The mock-image builder split naturally into three concerns: linked entity ordering, offset/layout planning, and value materialization. That made later fixes for references and temporaries much easier.
- Tightening PA8 one checked-in behavior at a time worked well because the local suite isolates many semantic rules cleanly: alias misuse, namespace conflicts, static assertions, and qualified redeclarations all moved independently.

What went poorly

- The PA7 declarator code still had a literal-only array-bound assumption hidden inside it, and PA8’s constant-expression array bounds forced another round of parser duplication to break that out.
- Tracking “constant expression value” and “lvalue identity” simultaneously for references was easy to get wrong; early versions either lost constant-folding or created the wrong storage for bound references.
- Internal-linkage entities from different translation units exposed how many places were still keyed only by qualified name instead of true emitted-entity identity.

Suggestions for improving the assignment

- Call out earlier that post-qualified-declarator lookup context also affects array bounds inside the same declarator, not just following initializers.
- Add a short design hint that reference initialization needs both a constant-value view and an object-identity view; that distinction is central to the PA8 temporary and constant-expression behavior.
- Ship one focused starter example for cross-translation-unit linkage with internal names repeated in multiple TUs, since it is easy to accidentally key everything only by qualified name.
