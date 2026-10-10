#lang racket/base
;; Tiny vector helpers. A point is a list (x y z), z is up.
;; Centroids are plain averages of corners, and normals use Newell's method (SPEC.md §1).
(require racket/list racket/math)
(provide add scale lerp dot unit centroid normal pairs)

(define (add p q) (map + p q))

(define (scale p s) (map (λ (a) (* a s)) p))

(define (lerp p q t)
  ;; The point a fraction t of the way from p to q.
  (map (λ (a b) (+ a (* (- b a) t))) p q))

(define (dot p q) (for/sum ([a p] [b q]) (* a b)))

(define (unit p)
  ;; p scaled to length 1; the zero vector stays zero.
  (define length (sqrt (dot p p)))
  (if (zero? length) p (scale p (/ 1.0 length))))

(define (centroid points)
  (define n (length points))
  (for/list ([i 3]) (/ (for/sum ([p points]) (list-ref p i)) n)))

(define (normal points)
  ;; Newell's method: works for any polygon, flat or not.
  (define-values (nx ny nz)
    (for/fold ([nx 0.0] [ny 0.0] [nz 0.0])
              ([p points] [q (append (rest points) (list (first points)))])
      (define-values (x1 y1 z1) (apply values p))
      (define-values (x2 y2 z2) (apply values q))
      (values (+ nx (* (- y1 y2) (+ z1 z2)))
              (+ ny (* (- z1 z2) (+ x1 x2)))
              (+ nz (* (- x1 x2) (+ y1 y2))))))
  (define length (sqrt (+ (sqr nx) (sqr ny) (sqr nz))))
  (scale (list nx ny nz) (/ 1.0 (if (zero? length) 1 length))))

(define (pairs f)
  ;; The edges of a face, in order: (f0 . f1), (f1 . f2), ..., (fn . f0).
  (map cons f (append (rest f) (list (first f)))))
