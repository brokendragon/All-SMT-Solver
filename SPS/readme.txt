Before compiling:
1. Change the header file path "E:\project\Program_Analysis\z3_program\CNS\z3-4.3.2-x64-win\include\z3++.h" to your own Z3 header file path.
2. Change the variable fname = "E:\\project\\Program_Analysis\\z3_program\\CNS\\benchmark\\adjust\\z3test2.smt2" to the benchmark file path you wants to solve.
3. You can change TIME_OUT to specify time budget.
4. You can change SUBSPACE_SIZE to adjust the size of subspace.
5. SPS is just for comparison, so it just supports booleans and bitvectors no larger than 63 bits.