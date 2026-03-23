What went well

- Reusing the existing PA5 preprocessing stack made the CY86 frontend straightforward even with the macro-heavy helper includes in the checked-in tests.
- Resolving labels and operand widths up front kept the runtime simple enough to support the larger calculator and float-conversion workloads.
- Switching the runtime memory model from sparse per-byte paging to a few explicit mapped segments fixed the performance bottleneck without changing the frontend structure.

What went poorly

- The PA9 starter stub hides two separate problems behind one file: parsing CY86 itself and producing something executable quickly enough for the million-case tests.
- The assignment spec talks about ELF x86-64 output, but the checked-in harness only cares about executable behavior, so there is a real design fork that is not acknowledged clearly in the materials.
- Performance debugging took extra time because the default unoptimized dev build makes an interpreter-style implementation look much worse than its actual structure would suggest.

Suggestions

- Call out explicitly in the README whether behavioral equivalence is sufficient for the checked-in tests or whether the intended grading environment also inspects the generated executable format.
- Add one medium-sized throughput test before the million-case calculators so performance problems show up earlier and closer to the change that introduced them.
- Mention in the starter notes that CY86 immediates use raw literal-byte truncation and extension rules, including for string literals produced by macro stringization; that was easy to miss from the prose alone.
