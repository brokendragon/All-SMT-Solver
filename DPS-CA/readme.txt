Before compiling:
1. Change the header file path "E:\project\Program_Analysis\z3_program\CNS\z3-4.3.2-x64-win\include\z3++.h" to your own Z3 header file path.
2. Change the variable fname = "E:\\project\\Program_Analysis\\z3_program\\CNS\\benchmark\\adjust\\z3test2.smt2" to the benchmark file path you wants to solve.
3. You can change TIME_OUT to specify time budget.
4. You can change MEMORY_OUT to adapt your physical memory.
5. You can change PARALLEL to create more worker threads that is able to accelerate solving process. Remeber that, more worker threads use more CPU resources and memory. BTW, the parallelization of the tool is far from perfect. So you cannot expect linear speedup when increasing workers number.
