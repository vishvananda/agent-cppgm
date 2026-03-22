What went well

- Reusing the PA5 preprocessing/tokenization path kept PA9 focused on CY86 parsing and code generation instead of rebuilding earlier phases again.
- Treating CY86 as a direct-lowering problem with a small fixed x86 register map made it possible to bring up the suite incrementally: first ELF/syscalls, then integer/control flow, then the x87 conversion and comparison cases.
- The checked-in PA9 programs are good staged coverage. `100` through `400` isolated parser, layout, stack-addressing, and integer ALU bugs before the float cases started overlapping concerns.

What went poorly

- Several early failures came from codegen liveness bugs rather than algorithmic misunderstandings: using the same scratch register for syscall argument setup, address formation, and value transport corrupted otherwise-correct lowering.
- The x87 compare and unsigned-64 conversion paths were easy to get subtly wrong because operand order and `2^63` handling matter, but the emitted code is low-level enough that those mistakes are not obvious from inspection.
- Because the generated program output is the grading surface, PA9 debugging is slower than earlier assignments; when codegen is wrong, the symptom often appears many instructions after the original lowering bug.

Suggestions for improving the assignment

- Add one short note in the handout that CY86 labels can reference both code and inline literal/data statements in the same flat image, because layout decisions are a central PA9 design constraint.
- Include a tiny reference sketch for exact unsigned-64 to/from float80 conversion on x87, since that is one of the few places where “just use the obvious instruction” is not enough.
- Provide one extra checked-in micro-test that exercises stack-relative locals like `[bp-8]` and one syscall6 case in isolation; those were high-value bugs that only surfaced indirectly through larger programs.
