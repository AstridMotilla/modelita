#lang racket/base
;; racket cli.rkt FILE       run a Modelita program, print the dump of every shown word
;; racket cli.rkt -e WORD    the dump of one word
;;
;; For each statement that shows a shape or fails, the dump has a block:
;;   line N / shape ... skipped K / end      or      line N / error CATEGORY / end
;; Error messages go to stderr. `export` also writes the OBJ file next to FILE.
(require racket/cmdline racket/file racket/port
         "shape.rkt" "errors.rkt" "run.rkt" "parse.rkt" "dump.rkt")

(define (print-result r source write-files?)
  (define st (result-statement r))
  (cond
    [(result-error r)
     (printf "line ~a\nerror ~a\nend\n" (result-line r) (exn:modelita-category (result-error r)))
     (eprintf "line ~a: ~a\n" (result-line r) (exn-message (result-error r)))]
    [(shape? (result-value r))
     (printf "line ~a\n~askipped ~a\nend\n" (result-line r) (shape->dump (result-value r)) (result-skipped r))
     (when (and write-files? (eq? (statement-kind st) 'export))
       (define path (if source
                        (let-values ([(dir n d?) (split-path source)])
                          (build-path (if (path? dir) dir (current-directory)) (statement-file st)))
                        (statement-file st)))
       (display-to-file (shape->obj (result-value r)) path #:exists 'replace)
       (eprintf "wrote ~a\n" path))]
    [(operator? (result-value r))
     (printf "line ~a\noperator\nend\n" (result-line r))]))

(define word (make-parameter #f))
(define write-files? (make-parameter #t))
(define file
  (command-line
   #:once-each
   [("-e") w "Dump one word" (word w)]
   [("--no-write") "Don't write exported files" (write-files? #f)]
   #:args ([file #f]) file))

(cond
  [(word) (for ([r (run-program (word))]) (print-result r #f #f))]
  [file (for ([r (run-program (file->string file) #:source file)]) (print-result r file (write-files?)))]
  [else (for ([r (run-program (port->string (current-input-port)))]) (print-result r #f #f))])
