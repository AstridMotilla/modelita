#lang racket/base
;; Selectors (SPEC.md §7): text between [ ], read into data, then tested on
;; every face of the shape an operator is about to change.
;;
;;   all  new  (tag top)  (facing + z)  (sides 4)  (index 3)
;;   (and a b)  (or a b)  (not a)  (near a)
(require racket/list racket/match racket/string "geom.rkt" "shape.rkt" "errors.rkt" "ops.rkt")
(provide read-selector pick selector->text keywords roles)

(define keywords '(all new))
(define roles '(seed top side inner rim tent tube vertex edge face merged flip))

;; --- reading -----------------------------------------------------------------------
;; Tightest first: ! and ~, then &, then |. Parentheses group.

(define (read-selector text)
  (define tokens (tokenize text))
  (define-values (sel rest) (read-or tokens))
  (unless (null? rest) (fail 'syntax "selector: unexpected ~a in [~a]" (first rest) text))
  sel)

(define (tokenize text)
  (let loop ([i 0] [out '()])
    (cond
      [(= i (string-length text)) (reverse out)]
      [(char-whitespace? (string-ref text i)) (loop (add1 i) out)]
      [(memv (string-ref text i) '(#\( #\) #\& #\| #\! #\~))
       (loop (add1 i) (cons (string (string-ref text i)) out))]
      [(regexp-match #px"^(?:[+-][a-z]|#[0-9]+|[a-z][a-z0-9_-]*)" text i)
       => (λ (m) (loop (+ i (string-length (first m))) (cons (first m) out)))]
      [else (fail 'syntax "selector: can't read ~s" (substring text i))])))

(define (read-or tokens)
  (let-values ([(a rest) (read-and tokens)])
    (if (and (pair? rest) (equal? (first rest) "|"))
        (let-values ([(b rest) (read-or (rest-of rest))]) (values `(or ,a ,b) rest))
        (values a rest))))

(define (read-and tokens)
  (let-values ([(a rest) (read-unary tokens)])
    (if (and (pair? rest) (equal? (first rest) "&"))
        (let-values ([(b rest) (read-and (rest-of rest))]) (values `(and ,a ,b) rest))
        (values a rest))))

(define (rest-of tokens) (cdr tokens))

(define (read-unary tokens)
  (when (null? tokens) (fail 'syntax "selector: something is missing"))
  (define t (first tokens))
  (cond
    [(equal? t "!") (let-values ([(a rest) (read-unary (cdr tokens))]) (values `(not ,a) rest))]
    [(equal? t "~") (let-values ([(a rest) (read-unary (cdr tokens))]) (values `(near ,a) rest))]
    [(equal? t "(")
     (let-values ([(a rest) (read-or (cdr tokens))])
       (unless (and (pair? rest) (equal? (first rest) ")")) (fail 'syntax "selector: missing )"))
       (values a (cdr rest)))]
    [else (values (atom t) (cdr tokens))]))

(define (atom t)
  (cond
    [(equal? t "all") 'all]
    [(equal? t "new") 'new]
    [(regexp-match #px"^([+-])([xyz])$" t)
     => (λ (m) `(facing ,(string->symbol (second m)) ,(string->symbol (third m))))]
    [(regexp-match #px"^n([0-9]+)$" t) => (λ (m) `(sides ,(string->number (second m))))]
    [(regexp-match #px"^#([0-9]+)$" t) => (λ (m) `(index ,(string->number (second m))))]
    [(regexp-match? #px"^[a-z][a-z0-9_-]*$" t) `(tag ,(string->symbol t))]
    [else (fail 'syntax "selector: can't read ~s" t)]))

(define (selector->text sel)
  (match sel
    ['all "all"]
    ['new "new"]
    [`(tag ,t) (symbol->string t)]
    [`(facing ,sign ,axis) (format "~a~a" sign axis)]
    [`(sides ,n) (format "n~a" n)]
    [`(index ,n) (format "#~a" n)]
    [`(and ,a ,b) (format "~a&~a" (group a '(or)) (group b '(or)))]
    [`(or ,a ,b) (format "~a|~a" (selector->text a) (selector->text b))]
    [`(not ,a) (string-append "!" (group a '(and or)))]
    [`(near ,a) (string-append "~" (group a '(and or)))]))

(define (group sel looser)
  (if (and (pair? sel) (memq (car sel) looser))
      (format "(~a)" (selector->text sel))
      (selector->text sel)))

;; --- picking -------------------------------------------------------------------------

(define (pick s sel)
  ;; A vector with one boolean per face of s.
  (define faces (list->vector (shape-faces s)))
  (define (each test) (for/vector ([f faces] [i (in-naturals)]) (test f i)))
  (let loop ([sel sel])
    (match sel
      ['all (each (λ (f i) #t))]
      ['new (made-by-last-step s)]
      [`(tag ,t) (each (λ (f i) (or (eq? (face-role f) t) (and (memq t (face-labels f)) #t))))]
      [`(facing ,sign ,axis)
       (define k (case axis [(x) 0] [(y) 1] [(z) 2]))
       (define direction (if (eq? sign '+) 1 -1))
       (each (λ (f i) (> (* direction (list-ref (face-normal s f) k)) 0.9)))]
      [`(sides ,n) (each (λ (f i) (= (length (face-corners f)) n)))]
      [`(index ,n) (each (λ (f i) (= i n)))]
      [`(and ,a ,b) (let ([x (loop a)] [y (loop b)]) (for/vector ([p x] [q y]) (and p q)))]
      [`(or ,a ,b) (let ([x (loop a)] [y (loop b)]) (for/vector ([p x] [q y]) (or p q)))]
      [`(not ,a) (for/vector ([p (loop a)]) (not p))]
      [`(near ,a)
       ;; faces that share an edge with an `a` face, and aren't one
       (define x (loop a))
       (define owner (for*/hash ([(f i) (in-indexed faces)] [e (pairs (face-corners f))]) (values e i)))
       (each (λ (f i)
               (and (not (vector-ref x i))
                    (for/or ([e (pairs (face-corners f))])
                      (vector-ref x (hash-ref owner (cons (cdr e) (car e))))))))])))
