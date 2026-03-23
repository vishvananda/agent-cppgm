What went well

- Building the tokenizer around the real translation phases made the checked-in and course tests line up without test-specific branching.
- Keeping tokenization on translated code points while falling back to raw bytes only for raw strings kept the implementation manageable.

What went poorly

- The starter stub hid a fair amount of required behavior, so the first complete pass needed several course-test follow-ups for raw strings and escape validation.
- A few course-defined edges, especially empty-file EOF handling and raw-string delimiter limits, were only obvious after running the supplemental suite.

Suggestions

- Call out the empty-file newline behavior explicitly in the assignment text, since it differs from the general “add a trailing newline” summary.
- Add the raw-string delimiter length limit and invalid-escape failure cases to the main `pa1/tests/` set so they are visible earlier.
