#lang racket/base
;; A shape (SPEC.md §1):
;;   vertices  vector of (x y z) points, z is up
;;   faces     list of faces, each listing its corners counterclockwise seen from outside
;;   step      how many steps have run since the seed (the seed is step 0)
;;
;; Each face knows what it is (its role), the labels a word gave it, and the
;; step that made it.
(require racket/list racket/set "geom.rkt" "errors.rkt")
(provide (struct-out shape) (struct-out face)
         point face-points face-normal face-centroid
         shared-labels add-labels prune check edge-count)

(struct face (corners role labels step) #:transparent)
(struct shape (vertices faces step) #:transparent)

(define (point s v) (vector-ref (shape-vertices s) v))
(define (face-points s f) (for/list ([v (face-corners f)]) (point s v)))
(define (face-normal s f) (normal (face-points s f)))
(define (face-centroid s f) (centroid (face-points s f)))

(define (shared-labels faces)
  ;; The labels every one of faces has: what a face made from them inherits.
  (if (null? faces)
      '()
      (for/fold ([common (face-labels (first faces))]) ([f (rest faces)])
        (filter (λ (l) (memq l (face-labels f))) common))))

(define (add-labels labels more)
  ;; Labels are kept sorted, without repeats.
  (sort (remove-duplicates (append labels more)) symbol<?))

(define (prune s)
  ;; Drop the vertices no face uses, keeping the others in order.
  (define used (for*/seteqv ([f (shape-faces s)] [v (face-corners f)]) v))
  (define n (vector-length (shape-vertices s)))
  (cond
    [(= (set-count used) n) s]
    [else
     (define kept (for/list ([v (in-range n)] #:when (set-member? used v)) v))
     (define renumber (for/hasheqv ([v kept] [k (in-naturals)]) (values v k)))
     (shape (for/vector ([v kept]) (point s v))
            (for/list ([f (shape-faces s)])
              (struct-copy face f [corners (for/list ([v (face-corners f)]) (hash-ref renumber v))]))
            (shape-step s))]))

(define (edge-count s)
  (set-count (for*/set ([f (shape-faces s)] [e (pairs (face-corners f))])
               (cons (min (car e) (cdr e)) (max (car e) (cdr e))))))

(define (check s)
  ;; The invariant: every directed edge once, and its reverse once, and every
  ;; vertex used. A failure here is a bug in an operator, not in the word.
  (define directed (for*/list ([f (shape-faces s)] [e (pairs (face-corners f))]) e))
  (define seen (list->set directed))
  (define (broken why . args) (fail 'internal "not a valid shape: ~a" (apply format why args)))
  (for ([f (shape-faces s)])
    (unless (>= (length (face-corners f)) 3) (broken "a face with fewer than 3 corners")))
  (unless (= (length directed) (set-count seen)) (broken "an edge is used twice the same way"))
  (for ([e directed])
    (unless (set-member? seen (cons (cdr e) (car e)))
      (broken "edge ~a-~a has no neighbour" (car e) (cdr e))))
  (define used (for*/seteqv ([f (shape-faces s)] [v (face-corners f)]) v))
  (unless (= (set-count used) (vector-length (shape-vertices s))) (broken "an unused vertex"))
  s)
