#lang racket/base
;; The reference implementation of Modelita, as a library:
;;
;;   (run-word "k[top](0.9) x[+z](0.6) C")       -> a shape
;;   (run-program text)                           -> a result for each statement
;;   (shape->dump s)  (shape->obj s)
(require "shape.rkt" "run.rkt" "dump.rkt" "errors.rkt" "parse.rkt" "select.rkt")
(provide (all-from-out "shape.rkt" "run.rkt" "dump.rkt" "errors.rkt" "parse.rkt" "select.rkt"))
