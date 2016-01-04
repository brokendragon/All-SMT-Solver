;;we known all solutions before invoking Z3
;;solution number: 20100
(set-logic QF_BV)
(set-info :source |
Bit-vector benchmarks crafted by Ting Chen, University of Electronic Science and Technology of China
|)
(set-info :smt-lib-version 2.0)
(set-info :category "industrial")
(set-info :status sat)
(declare-fun a_0 () (_ BitVec 32))
(declare-fun a_1 () (_ BitVec 32))
(assert (bvuge a_0 (_ bv0 32)))
(assert (bvuge a_1 (_ bv0 32)))
(assert (bvult a_0 (_ bv200 32)))
(assert (bvult a_1 (_ bv200 32)))
(assert (bvult (bvadd a_0 a_1) (_ bv200 32)))
(check-sat)
(exit)
