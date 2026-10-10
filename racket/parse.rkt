#lang racket/base
;; Reading a Modelita program (SPEC.md §3, §6, §8): one statement per line.
;;
;;   house = k[top](0.9) x[+z](0.6) C      a definition
;;   storey(h, r) = x[inner](h) i[top](r)  a definition with parameters
;;   k[top] house                          a word to show
;;   use "other.mdl"   gallery "g.svg"   export "f.obj" word
;;   ; a comment
;;
;; A token: head [selector] (numbers) {label} ^count, every part but the head optional.
(require racket/list racket/string "errors.rkt" "select.rkt")
(provide (struct-out statement) (struct-out token) parse-line parse-word read-label)

;; kind: 'define 'show 'use 'gallery 'export. file: for use, gallery and export.
(struct statement (kind name params word file) #:transparent)

;; head: a string. selector: data or #f. args: numbers and parameter names
;; (symbols), or #f when there are no ( ). label: a symbol or #f.
(struct token (head selector args label count text) #:transparent)

(define NAME "[A-Za-z][A-Za-z0-9_-]*")
(define DEFINE (pregexp (string-append "^\\s*(" NAME ")\\s*(?:\\(([^)]*)\\))?\\s*=(.*)$")))
(define FILE-ONLY #px"^\\s*(use|gallery)\\s+\"([^\"]*)\"\\s*$")
(define EXPORT #px"^\\s*export\\s+\"([^\"]*)\"(.*)$")
(define NUMBER #px"^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$")

(define (parse-line text)
  ;; A statement, or #f for a blank line or a comment.
  (define code (regexp-replace #rx";.*$" text ""))
  (cond
    [(regexp-match? #px"^\\s*$" code) #f]
    [(regexp-match FILE-ONLY code)
     => (λ (m) (statement (string->symbol (second m)) #f '() #f (third m)))]
    [(regexp-match? #px"^\\s*(use|gallery)\\b" code)
     (fail 'syntax "expected a file name in quotes, like gallery \"shapes.svg\"")]
    [(regexp-match EXPORT code)
     => (λ (m) (statement 'export #f '() (parse-word (third m)) (second m)))]
    [(regexp-match DEFINE code)
     => (λ (m)
          (define params
            (if (third m)
                (for/list ([p (string-split (third m) "," #:trim? #f)])
                  (define name (string-trim p))
                  (unless (regexp-match? (pregexp (string-append "^" NAME "$")) name)
                    (fail 'syntax "~s is not a parameter name" name))
                  (string->symbol name))
                '()))
          (when (check-duplicates params) (fail 'syntax "a parameter is named twice"))
          (statement 'define (second m) params (parse-word (fourth m)) #f))]
    [else (statement 'show #f '() (parse-word code) #f)]))

(define (parse-word text)
  ;; A list of tokens, at least one.
  (define len (string-length text))
  (define (skip i) (or (for/first ([k (in-range i len)] #:unless (char-whitespace? (string-ref text k))) k) len))
  (let loop ([i (skip 0)] [tokens '()])
    (cond
      [(= i len)
       (when (null? tokens) (fail 'syntax "a word needs at least a seed, like C"))
       (reverse tokens)]
      [else
       (define-values (t end) (parse-token text i))
       (when (and (< end len) (not (char-whitespace? (string-ref text end))))
         (fail 'syntax "expected a space before ~s" (substring text end)))
       (loop (skip end) (cons t tokens))])))

(define (parse-token text start)
  (define head (regexp-match (pregexp (string-append "^" NAME)) text start))
  (unless head (fail 'syntax "can't read ~s" (substring text start)))
  (define (enclosed i open close)
    ;; The text between open (at i) and close, and the position after close; or #f.
    (cond
      [(and (< i (string-length text)) (char=? (string-ref text i) open))
       (define k (for/first ([k (in-range (add1 i) (string-length text))] #:when (char=? (string-ref text k) close)) k))
       (unless k (fail 'syntax "missing ~a" close))
       (values (substring text (add1 i) k) (add1 k))]
      [else (values #f i)]))
  (define i0 (+ start (string-length (first head))))
  (define-values (sel i1) (enclosed i0 #\[ #\]))
  (define-values (args i2) (enclosed i1 #\( #\)))
  (define-values (label i3) (enclosed i2 #\{ #\}))
  (define-values (count i4)
    (cond
      [(regexp-match #px"^\\^([0-9]+)" text i3)
       => (λ (m) (values (string->number (second m)) (+ i3 (string-length (first m)))))]
      [(and (< i3 (string-length text)) (char=? (string-ref text i3) #\^)) (fail 'syntax "^ needs a count, like ^3")]
      [else (values 1 i3)]))
  (when (zero? count) (fail 'syntax "repeat at least once"))
  (values (token (first head)
                 (and sel (read-selector sel))
                 (and args (map read-arg (string-split args "," #:trim? #f)))
                 (and label (read-label label))
                 count
                 (substring text start i4))
          i4))

(define (read-arg text)
  (define t (string-trim text))
  (cond
    [(regexp-match? NUMBER t) (exact->inexact (string->number t))]
    [(regexp-match? (pregexp (string-append "^" NAME "$")) t) (string->symbol t)]
    [else (fail 'syntax "~s is not a number" t)]))

(define (read-label text)
  (define t (string-trim text))
  (unless (regexp-match? #px"^[a-z][a-z0-9_-]*$" t)
    (fail 'syntax "a label is a lowercase name, like {roof}, not ~s" t))
  (define name (string->symbol t))
  (when (or (memq name keywords) (memq name roles) (regexp-match? #px"^n[0-9]+$" t))
    (fail 'name "~a can't be a label: it already means something in a selector" t))
  name)
