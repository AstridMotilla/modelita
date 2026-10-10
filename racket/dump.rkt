#lang racket/base
;; The conformance dump (tests/README.md): a plain-text listing of a shape
;; that every implementation prints the same way, apart from how numbers are
;; written. The test runner compares numbers with a tolerance.
(require racket/list racket/string "shape.rkt")
(provide shape->dump shape->obj)

(define (number x) (number->string (if (zero? x) 0.0 (exact->inexact x))))

(define (shape->dump s)
  (string-append*
   (append
    (list "shape\n" (format "vertices ~a\n" (vector-length (shape-vertices s))))
    (for/list ([p (shape-vertices s)]) (format "v ~a\n" (string-join (map number p))))
    (list (format "faces ~a\n" (length (shape-faces s))))
    (for/list ([f (shape-faces s)])
      (format "f ~a ~a ~a : ~a\n" (face-role f) (face-step f)
              (if (null? (face-labels f)) "-" (string-join (map symbol->string (face-labels f)) ","))
              (string-join (map number->string (face-corners f))))))))

(define (shape->obj s)
  ;; OBJ is usually read with y up, so (x y z) is written as (x z -y): a
  ;; turn, which keeps every face pointing outwards. One group per role.
  (define (num x) (~r* x))
  (define out (open-output-string))
  (for ([p (shape-vertices s)])
    (fprintf out "v ~a ~a ~a\n" (num (first p)) (num (third p)) (num (- (second p)))))
  (for/fold ([current #f]) ([f (shape-faces s)])
    (unless (eq? (face-role f) current) (fprintf out "g ~a\n" (face-role f)))
    (fprintf out "f ~a\n" (string-join (for/list ([v (face-corners f)]) (number->string (add1 v)))))
    (face-role f))
  (get-output-string out))

(define (~r* x) (number (/ (round (* x 1e6)) 1e6)))
