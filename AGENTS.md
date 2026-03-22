# Project Layout and Workflow

This project is a series of assignments, `PA1` through `PA9`, that build
toward a self-hosting C++11 compiler for Linux x86_64.

## Project Structure

- `pa1/`: Preprocessing Tokenizer (`pptoken`)
- `pa2/`: Post-Tokenizer (`posttoken`)
- `pa3/`: Constant Expressions (`ctrlexpr`)
- `pa4/`: Macro Processor (`macro`)
- `pa5/`: Preprocessor (`preproc`)
- `pa6/`: C++ Grammar Recognizer (`recog`)
- `pa7/`: Namespace Declarations (`nsdecl`)
- `pa8/`: Namespace Initialization (`nsinit`)
- `pa9/`: Code Generation (`cy86`)
- `doc/`: Documentation and reference materials

Assignments `PA6` through `PA9` also include `.gram` files and sometimes
additional material in `grammar/` or `extras/`. Treat those as part of the
assignment specification.

## Source Layout

- `dev/`: The real assignment entrypoints, one file per tool
- `dev/src/`: Shared compiler sources and headers
- `paN/<tool>.cpp`: A committed symlink to `../dev/<tool>.cpp`

Keep the real implementation in `dev/` and `dev/src/`. `dev/src/` is built as
shared code, and headers placed there are on the include path via `-Isrc`, so
they can be included directly as `#include "foo.h"`. The `paN/` directories
hold assignment-specific wrappers, tests, and metadata. The `*-ref` binaries
are included for testing and observation only and must not be used as part of
the implementation.

## Build and Test Workflow

- `dev/Makefile` builds the canonical executables.
- Assignment Makefiles delegate to `dev/Makefile` instead of compiling shared
  sources themselves.
- Shared objects live in `obj/` for reuse across assignments.
- Assignment Makefiles lock around the central build so concurrent local builds
  do not race in `obj/`.
- Dependency tracking is automatic. Header changes should rebuild only the
  files that depend on them.
- Each assignment directory contains a committed `course` symlink to
  `../tests/course`, so shared supplemental tests are reachable as
  `course/paN/`.

Inside an assignment directory:

```bash
make
make test
VERBOSE=1 make test
```

For focused experiments inside an assignment, use `make run INPUT=...`
instead of invoking `../dev/<tool>` directly. For a single checked-in case,
use `make check TEST=...`. Both paths keep dependency checks and rebuilds in
front of the run. Add `VERBOSE=1` to see per-test progress and pass lines while
debugging, for example `VERBOSE=1 make test` or `VERBOSE=1 make check TEST=...`.

From the project root:

- `make build`: build the canonical tools once through `dev/Makefile`
- `make test`: run `make build`, then run the full assignment suite with
  rebuilds skipped
- `make test-through-paN`: follow the same build-first path, then test
  `pa1` through `paN`
- `VERBOSE=1 make paN`: rerun one assignment from the root with per-test output
- `VERBOSE=1 make test-through-paN`: rerun through a failing assignment with per-test output

After finishing `paN`, run `make test-through-paN`. Treat regression testing
as part of completing the assignment, not as optional cleanup.

## Git and Retrospectives

Commit frequently so shared-code refactors and regressions are easy to manage.

- Commit after implementing a meaningful feature or passing a new test.
- Commit before starting `pa(N+1)`, with a clean workspace and
  `make test-through-paN` passing from the root.
- Example: `git commit -am "pa1: implement trigraph replacement"`

After each assignment, add `paN/RETRO.md` and commit it with the assignment
work or immediately after. Each retrospective should cover:

- what went well
- what went poorly
- suggestions for improving the assignment, starter code, tests, or reference
  material

## Implementation Guidance

- Reuse and extend code from previous assignments instead of starting over.
- Include extra context in error cases when practical, especially filenames,
  line numbers, and the specific failing condition.
- Periodically simplify, reorganize, and refactor shared code so later
  assignments stay manageable.
- Follow the checked-in tests closely. Implement to the current assignment's
  expected behavior even if a broader feature set or a newer standard
  interpretation would differ.
- Do not implement features ahead of the assignment if doing so changes
  behavior that the current tests expect.
- If a real conflict between tests, reference behavior, and intended design
  cannot be resolved cleanly, choose the path that keeps the assignment moving
  forward and record the conflict in `RETRO.md`.

## Assignment Integrity and Test Policy

- Implement the assignment yourself in `dev/` and `dev/src/`.
- Do not edit checked-in `.ref` outputs, `.ref.*` sidecars, or test inputs to
  make an incomplete implementation appear correct.
- Do not bypass, skip, narrow, or weaken required tests. Do not modify
  Makefiles, compare scripts, or test runners just to avoid failing behavior.
- Do not shell out from the student implementation to reference binaries,
  production compilers, prior solutions, or other external programs to produce
  the required assignment output. Reference tools are for observation and local
  fixture generation only.
- Do not hardcode expected answers for specific test files or special-case
  behavior by test name. Implement the underlying rule or feature.
- If you discover a genuine harness or materials conflict, note it in
  `RETRO.md` and continue implementing the assignment yourself rather than
  treating test or harness edits as the solution.

## Tests and References

- Each `paN` directory includes a compiled reference binary such as
  `pptoken-ref`.
- `make ref-test` regenerates only assignment-local fixtures in `paN/tests/`.
- `paN/tests/` contains the original assignment-local tests, including inputs,
  reference outputs, and exit-status sidecars.
- `tests/course/paN/` contains shared supplemental tests and is exposed inside
  each assignment as `course/paN/`.
- `make test` runs both `paN/tests/` and `course/paN/`.
- Put new user-authored or shared tests in `tests/course/paN/`, not
  `paN/tests/`.
- Prefer the checked-in tests over speculative improvements or newer-standard
  behavior when they disagree.
- Do not bulk-regenerate the checked-in `.ref` files under `tests/course/paN/`
  from the local `*-ref` binaries. Those shared fixtures are authoritative and
  may intentionally diverge from the reference binaries.
- The project targets the C++11 standard, N3485, in `doc/`.
