# PA5 Retrospective

## What Went Well
- Reused and extended the PA4 macro engine instead of rewriting preprocessing logic from scratch, which kept macro behavior aligned with previously passing PA4 fixtures.
- Implemented PA5 end-to-end in `dev/preproc.cpp` with support for:
  - per-source-file isolated state,
  - conditional inclusion stack (`#if/#ifdef/#ifndef/#elif/#else/#endif`),
  - include handling with course-defined path search,
  - predefined macros (including dynamic `__FILE__` and `__LINE__`),
  - `#line` control,
  - `#pragma once` and `_Pragma(...)`,
  - invalid-token detection at phase 7.
- Passed both assignment-local PA5 tests and `course/pa5` supplemental tests.

## What Went Poorly
- Physical-line accounting for `__LINE__` was subtle because tokenizer newline emission timing does not map directly to source physical newlines; this required an additional logical-line start mapping pass.
- Integrating PA3-style controlling-expression evaluation into PA5 required careful extraction/reuse to avoid cross-assignment `main` and symbol conflicts.

## Suggestions
- Add a short PA5 starter note explicitly warning that `__LINE__` must track physical source lines even when comments and splices affect logical line structure.
- Provide a small reusable utility module for controlling-expression parsing/evaluation so PA5 can import it directly rather than duplicating extraction work.
- Include one focused starter test that isolates `_Pragma("once")` behavior in included files to clarify ordering and scope expectations.
