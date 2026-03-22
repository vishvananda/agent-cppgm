# PA9 Retrospective

What went well:
- Reusing the earlier codegen infrastructure made it possible to extend the backend without rewriting the whole compiler.
- The checked-in calculator tests were effective at isolating the float and integer corner cases, especially the conversion paths.
- Running targeted checks on a single failing case made it much faster to narrow the problem down to one helper at a time.

What went poorly:
- The x87 conversion paths were easy to get subtly wrong, especially operand order for comparisons and unsigned float-to-integer bias handling.
- The backend in `dev/cy86.cpp` grew very large and accumulated dead helpers and special-case code that made debugging slower than it needed to be.

Suggestions:
- Add focused tests for x87 comparison order and float-to-unsigned conversion so operand-order mistakes are caught earlier.
- Split the larger backend into smaller translation units or helper modules before the next assignment.
- Trim the starter code or provide a narrower reference for the codegen pieces, since the current file is hard to reason about when debugging.
