#lang racket/base
;; Errors carry a category, so every implementation reports the same kind
;; of failure (SPEC.md §9): syntax name kind arity range precondition limit internal.
(provide (struct-out exn:modelita) fail)

(struct exn:modelita exn:fail (category))

(define (fail category message . args)
  (raise (exn:modelita (apply format message args) (current-continuation-marks) category)))
