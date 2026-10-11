#lang racket/base
;; What words mean (SPEC.md §3–§6). Every token is a value: a shape (a seed,
;; or a name for one) or an operator (a primitive, or a name for a word that
;; ends in one). Tokens side by side compose right to left.
(require racket/list racket/file racket/vector racket/match "shape.rkt" "errors.rkt" "ops.rkt" "seeds.rkt" "select.rkt" "parse.rkt")
(provide (struct-out operator) (struct-out result) operators
         run-program run-word MAX-FACES)

(struct operator (run))

;; What one statement produced. value: a shape, an operator, or #f; error: an
;; exn:modelita or #f; skipped: how many picked elements the word's steps skipped.
(struct result (line statement value error skipped) #:transparent)

;; name, letter (or #f), procedure, default numbers (SPEC.md §5)
(define operators
  (list (list "dual" "d" dual '())
        (list "ambo" "a" ambo '())
        (list "truncate" "t" truncate '(0.3))
        (list "kis" "k" kis '(0.0))
        (list "gyro" "g" gyro '())
        (list "reflect" "r" reflect '())
        (list "needle" "n" needle '())
        (list "zip" "z" zip '())
        (list "join" "j" join '())
        (list "expand" "e" expand '())
        (list "ortho" "o" ortho '())
        (list "meta" "m" meta '())
        (list "bevel" "b" bevel '())
        (list "snub" "s" snub '())
        (list "extrude" "x" extrude '(1.0))
        (list "inset" "i" inset '(0.3))
        (list "bridge" "h" bridge '())
        (list "lift" #f lift '(1.0))
        (list "scale" #f scale-region '(0.5))
        (list "push" #f push '(0.5))
        (list "merge" #f merge '())
        (list "flip" #f flip '())
        (list "mirror" #f mirror '())
        (list "move" #f move-by '(0.0 0.0 0.0))
        (list "turn" #f turn '(0.0 0.0 0.0))
        (list "stretch" #f stretch '(1.0 1.0 1.0))
        (list "array" #f array '(2.0 1.0 0.0 0.0))
        (list "spin" #f spin '(6.0))))

(define operator-table
  (for*/hash ([o operators] [key (list (first o) (second o))] #:when key) (values key o)))


(define (step-value o selector numbers)
  (define-values (name proc) (values (first o) (third o)))
  (operator
   (λ (s)
     (define out (apply proc s (pick s (or selector 'all)) numbers))
     (when (> (length (shape-faces out)) MAX-FACES)
       (fail 'limit "~a makes ~a faces, more than the limit of ~a" name (length (shape-faces out)) MAX-FACES))
     (check out))))

(define (reserved? name)
  (or (hash-has-key? operator-table name) (seed-for name) (regexp-match? #px"^[a-z]$" name)))

;; --- tokens and words --------------------------------------------------------------

;; env: name -> definition. Definitions are run again each time they are
;; used: words are pure, and this keeps each word's skip count its own.
;; bindings: parameter -> a number, or a passed selector (SPEC.md §6).
(struct definition (params selector word))
(struct passed (selector))

(define (bind-selector sel bindings)
  ;; sel with every selector parameter replaced by the selector passed for it.
  (match sel
    [`(tag ,t) (match (hash-ref bindings t #f) [(passed s) s] [_ sel])]
    [`(and ,a ,b) `(and ,(bind-selector a bindings) ,(bind-selector b bindings))]
    [`(or ,a ,b) `(or ,(bind-selector a bindings) ,(bind-selector b bindings))]
    [`(not ,a) `(not ,(bind-selector a bindings))]
    [`(near ,a) `(near ,(bind-selector a bindings))]
    [_ sel]))

(define (token-value t env bindings)
  (define head (token-head t))
  (define (numbers most what)
    (define args (or (token-args t) '()))
    (when (> (length args) most)
      (fail 'arity "~a takes ~a" what
            (case most [(0) "no numbers"] [(1) "at most one number"] [else (format "at most ~a numbers" most)])))
    (for/list ([a args])
      (cond
        [(not (symbol? a)) a]
        [(hash-ref bindings a #f) => (λ (v) (if (real? v) v (fail 'name "~a is a selector, not a number" a)))]
        [else (fail 'name "~a isn't a parameter here" a)])))
  (define (no-selector what)
    (when (token-selector t)
      (fail 'syntax "only operators and names with a selector parameter take [selectors]; ~a is ~a" head what)))
  (define selector (and (token-selector t) (bind-selector (token-selector t) bindings)))
  (define value
    (cond
      [(hash-ref operator-table head #f)
       => (λ (o)
            (define given (numbers (length (fourth o)) head))
            (step-value o selector (append given (drop (fourth o) (length given)))))]
      [(seed-for head)
       => (λ (sd)
            (no-selector "a seed")
            (apply (cdr sd) (numbers (car sd) head)))]
      [(hash-ref env head #f)
       => (λ (d)
            (unless (definition-selector d) (no-selector "a name without one"))
            (define params (definition-params d))
            (define args (if (token-args t) (numbers (length (token-args t)) head) '()))
            (unless (= (length args) (length params))
              (fail 'arity "~a takes ~a number~a, not ~a" head (length params)
                    (if (= 1 (length params)) "" "s") (length args)))
            (define inner (for/hash ([p params] [a args]) (values p a)))
            (word-value (definition-word d) env
                        (if (definition-selector d)
                            (hash-set inner (definition-selector d) (passed (or selector 'all)))
                            inner)))]
      [(regexp-match? #px"^[a-z]$" head) (fail 'name "there is no operator ~a" head)]
      [else (fail 'name "~a isn't defined above this line" head)]))
  (repeat (labelled value (token-label t)) (token-count t) head))

(define (labelled v label)
  (cond
    [(not label) v]
    [(shape? v)
     (struct-copy shape v [faces (for/list ([f (shape-faces v)])
                                   (struct-copy face f [labels (add-labels (face-labels f) (list label))]))])]
    [else
     (operator
      (λ (s)
        (define before (shape-step s))
        (define out ((operator-run v) s))
        (struct-copy shape out
                     [faces (for/list ([f (shape-faces out)])
                              (if (> (face-step f) before)
                                  (struct-copy face f [labels (add-labels (face-labels f) (list label))])
                                  f))])))]))

(define (repeat v times head)
  (cond
    [(= times 1) v]
    [(shape? v) (fail 'kind "only operators can be repeated, and ~a is a shape" head)]
    [else (operator (λ (s) (for/fold ([s s]) ([k times]) ((operator-run v) s))))]))

(define (word-value terms env bindings)
  ;; One term is a word as before. Several are shapes side by side (SPEC.md §3).
  (cond
    [(null? (rest terms)) (term-value (first terms) env bindings)]
    [else
     (for/fold ([s #f] #:result s) ([tokens terms])
       (define v (term-value tokens env bindings))
       (unless (shape? v)
         (fail 'kind "+ puts shapes side by side, and ~a is an operator" (token-text (last tokens))))
       (if s (side-by-side s v) v))]))

(define (side-by-side a b)
  ;; a's vertices and faces, then b's. The step counter is the larger one.
  (define offset (vector-length (shape-vertices a)))
  (shape (vector-append (shape-vertices a) (shape-vertices b))
         (append (shape-faces a)
                 (for/list ([f (shape-faces b)])
                   (struct-copy face f [corners (map (λ (v) (+ v offset)) (face-corners f))])))
         (max (shape-step a) (shape-step b))))

(define (term-value tokens env bindings)
  (define vs (for/list ([t tokens]) (token-value t env bindings)))
  (for ([v (drop-right vs 1)] [t tokens] #:unless (operator? v))
    (fail 'kind "~a is a shape, so it can only come last in a word" (token-head t)))
  (define base (last vs))
  (define steps (reverse (drop-right vs 1)))
  (if (shape? base)
      (for/fold ([s base]) ([op steps]) ((operator-run op) s))
      (operator (λ (s) (for/fold ([s ((operator-run base) s)]) ([op steps]) ((operator-run op) s))))))

(define (run-word text [env (hash)])
  ;; The value of one word written as text.
  (word-value (parse-word text) env (hash)))

;; --- programs --------------------------------------------------------------------------

(define (run-program text #:source [source #f])
  ;; A result for every statement, in order.
  (define env (make-hash))
  (for*/list ([(line-text n) (in-indexed (regexp-split #rx"\r?\n" text))]
              [r (in-value (run-line line-text (add1 n) env source))]
              #:when r)
    r))

(define (run-line text n env source)
  (define skips (box 0))
  (define (fine st v) (result n st v #f (unbox skips)))
  (with-handlers ([exn:modelita? (λ (e) (result n #f #f e (unbox skips)))])
    (parameterize ([current-skips skips])
      (define st (parse-line text))
      (and st
           (case (statement-kind st)
             [(define)
              (define name (statement-name st))
              (when (reserved? name) (fail 'name "~a is reserved: it is an operator, a letter or a seed" name))
              (when (hash-has-key? env name) (fail 'name "~a is already defined" name))
              ;; Run it once now, so a broken definition is reported here; one
              ;; with parameters is checked when it is used.
              (unless (or (pair? (statement-params st)) (statement-selector st))
                (word-value (statement-word st) env (hash)))
              (hash-set! env name (statement->definition st))
              (fine st #f)]
             [(show export) (fine st (word-value (statement-word st) env (hash)))]
             [(use)
              (define file (if source (build-path (path-only* source) (statement-file st)) (statement-file st)))
              (define others (run-program (file->string file) #:source file))
              (for ([r others] #:when (result-error r))
                (fail 'name "~a, line ~a: ~a" (statement-file st) (result-line r) (exn-message (result-error r))))
              (load-definitions! (file->string file) env)
              (fine st #f)]
             [(gallery) (fine st #f)])))))

(define (load-definitions! text env)
  (for ([line-text (regexp-split #rx"\r?\n" text)])
    (define st (parse-line line-text))
    (when (and st (eq? (statement-kind st) 'define))
      (hash-set! env (statement-name st) (statement->definition st)))))

(define (statement->definition st)
  (definition (statement-params st) (statement-selector st) (statement-word st)))

(define (path-only* p)
  (define-values (dir name must-be-dir?) (split-path (if (string? p) (string->path p) p)))
  (if (path? dir) dir (current-directory)))
