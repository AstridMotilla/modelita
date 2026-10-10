#lang racket/base
;; Seeds: the shapes a word starts from (SPEC.md §2). Every seed takes
;; numbers like an operator does; leaving them out gives the defaults.
(require racket/list racket/math "geom.rkt" "shape.rkt" "errors.rkt" "ops.rkt")
(provide seed-for)

(define (seed points faces)
  (shape (for/vector ([p points]) (map exact->inexact p))
         (for/list ([f faces]) (face f 'seed '() 0))
         0))

(define (convex points faces)
  ;; Each face turned to face away from the centre of all the points.
  (define c (centroid points))
  (seed points
        (for/list ([f faces])
          (define corners (for/list ([i f]) (list-ref points i)))
          (if (positive? (dot (normal corners) (map - (centroid corners) c))) f (reverse f)))))

(define (positive! name . numbers)
  (for ([x numbers])
    (unless (positive? x) (fail 'range "~a: sizes must be greater than 0, not ~a" name x))))

(define (box [w 2.0] [d 2.0] [h 2.0])
  (positive! "C" w d h)
  (seed (for*/list ([x (list (- (/ w 2)) (/ w 2))] [y (list (- (/ d 2)) (/ d 2))] [z (list (- (/ h 2)) (/ h 2))])
          (list x y z))
        '((0 1 3 2) (4 6 7 5) (0 4 5 1) (2 3 7 6) (0 2 6 4) (1 5 7 3))))

(define (tetrahedron [r (sqrt 3)])
  (positive! "T" r)
  (define k (/ r (sqrt 3)))
  (seed (for/list ([p '((1 1 1) (1 -1 -1) (-1 1 -1) (-1 -1 1))]) (scale p k))
        '((0 1 2) (0 3 1) (0 2 3) (1 3 2))))

(define (octahedron [r (sqrt 3)])
  (positive! "O" r)
  (convex (list (list r 0 0) (list (- r) 0 0) (list 0 r 0) (list 0 (- r) 0) (list 0 0 r) (list 0 0 (- r)))
          (for*/list ([x '(0 1)] [y '(2 3)] [z '(4 5)]) (list x y z))))

(define (icosahedron [r (sqrt 3)])
  (positive! "I" r)
  (define phi (/ (+ 1 (sqrt 5)) 2))
  (define unit-points
    (append* (for*/list ([a '(-1 1)] [b (list (- phi) phi)])
               (list (list 0 a b) (list a b 0) (list b 0 a)))))
  (define (edge? p q) (< (abs (- (for/sum ([x p] [y q]) (sqr (- x y))) 4)) 1e-9))
  (define faces
    (for*/list ([i 12] [j (in-range (add1 i) 12)] [k (in-range (add1 j) 12)]
                #:when (and (edge? (list-ref unit-points i) (list-ref unit-points j))
                            (edge? (list-ref unit-points j) (list-ref unit-points k))
                            (edge? (list-ref unit-points i) (list-ref unit-points k))))
      (list i j k)))
  (convex (for/list ([p unit-points]) (scale p (/ r (sqrt (+ 1 (sqr phi)))))) faces))

(define (dodecahedron [r (sqrt 3)])
  ;; The dual of the icosahedron, its corners pushed out to radius r.
  (positive! "D" r)
  (define d (dual (icosahedron) (make-vector 20 #t)))
  (seed (for/list ([p (shape-vertices d)]) (scale p (/ r (sqrt (dot p p)))))
        (map face-corners (shape-faces d))))

(define (ring n r z [turn 0])
  (for/list ([i n]) (list (* r (cos (+ turn (/ (* 2 pi i) n)))) (* r (sin (+ turn (/ (* 2 pi i) n)))) z)))

(define (sides! name n)
  (unless (>= n 3) (fail 'range "~a~a: needs at least 3 sides" name n)))

(define ((prism n) [r 1.0] [h (* 2 r (sin (/ pi n)))])  ; square sides
  (positive! (format "P~a" n) r h)
  (convex (append (ring n r (- (/ h 2))) (ring n r (/ h 2)))
          (append (list (range n) (range n (* 2 n)))
                  (for/list ([i n]) (list i (modulo (add1 i) n) (+ n (modulo (add1 i) n)) (+ n i))))))

(define ((antiprism n) [r 1.0] [h (* 2 r (sqrt (/ (- (cos (/ pi n)) (cos (/ (* 2 pi) n))) 2)))]) ; equilateral sides
  (positive! (format "A~a" n) r h)
  (convex (append (ring n r (- (/ h 2))) (ring n r (/ h 2) (/ pi n)))
          (append (list (range n) (range n (* 2 n)))
                  (for/list ([i n]) (list i (modulo (add1 i) n) (+ n i)))
                  (for/list ([i n]) (list (+ n i) (modulo (add1 i) n) (+ n (modulo (add1 i) n)))))))

(define ((pyramid n) [r 1.0] [h r])                      ; the base sits on z = 0
  (positive! (format "Y~a" n) r h)
  (convex (append (ring n r 0) (list (list 0 0 h)))
          (cons (range n) (for/list ([i n]) (list i (modulo (add1 i) n) n)))))

(define fixed
  (hash "C" (cons 3 box) "T" (cons 1 tetrahedron) "O" (cons 1 octahedron)
        "I" (cons 1 icosahedron) "D" (cons 1 dodecahedron)))

(define (seed-for name)
  ;; (cons most-numbers procedure) for a seed's name, or #f.
  (cond
    [(hash-ref fixed name #f)]
    [(regexp-match #px"^([PAY])([0-9]+)$" name)
     => (λ (m)
          (define n (string->number (caddr m)))
          (define (check-sides make) (λ numbers (sides! (cadr m) n) (apply (make n) numbers)))
          (cons 2 (check-sides (case (cadr m) [("P") prism] [("A") antiprism] [("Y") pyramid]))))]
    [else #f]))
