;;we known all solutions before invoking Z3
;;solution number: 65536 
(set-logic QF_BV)
(set-info :source |
Bit-vector benchmarks crafted by Ting Chen, University of Electronic Science and Technology of China
|)
(set-info :smt-lib-version 2.0)
(set-info :category "industrial")
(set-info :status sat)
(declare-fun a_0 () (_ BitVec 8))
(declare-fun a_1 () (_ BitVec 8))
(assert (or (= a_0 a_1) (not (= a_0 a_1))))
(check-sat)
(exit)
