# PA3 Retrospective

What went well:
- Reusing the PA1 tokenizer made the controlling-expression front end much smaller.
- A recursive-descent parser matched the precedence table cleanly and handled short-circuiting without special parser machinery.
- The checked-in tests were a good guide for course-specific behavior, especially `defined`, alternate operator spellings, and integer overflow handling.

What went poorly:
- The README was slightly misleading about standalone identifiers, so I had to verify the reference behavior against the tests.
- Signed shift and division corner cases needed explicit guards to avoid trapping the process.
- UTF-16 character literal range checking was easy to miss until the course `040-char` case failed.

Suggestions:
- Call out the reference behavior for identifiers, alternate operators, and `defined` more explicitly in the assignment text.
- Add one or two malformed floating-literal fixtures to show which diagnostics are expected before the controlling-expression layer.
- Mention the signed-division overflow trap more directly in the guide so students know to guard it.
