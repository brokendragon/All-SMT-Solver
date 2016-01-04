;;we known all solutions before invoking Z3
;;solution number: 21484
(set-logic QF_BV)
(set-info :source |
Bit-vector benchmarks crafted by Ting Chen, University of Electronic Science and Technology of China
|)
(set-info :smt-lib-version 2.0)
(set-info :category "industrial")
(set-info :status sat)
(declare-fun a_0 () (_ BitVec 10))
(declare-fun a_1 () (_ BitVec 10))
(assert (or (and (bvult a_0 (_ bv20 10)) (bvuge a_0 (_ bv0 10))) (= a_1 (_ bv0 10))))
(check-sat)
(exit)
