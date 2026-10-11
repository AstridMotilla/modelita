#lang racket/base
;; Operators (SPEC.md §5). Each takes a shape, `picked` (a vector with one
;; boolean per face) and its numbers, and returns the shape one step later.
;;
;; Every operator follows one rule: it rebuilds only what was picked. Faces it
;; didn't pick keep their shape and corners, and at most gain or lose corners
;; along edges they share with picked faces. Those reshaped neighbours keep
;; their role, labels and step. A picked face an operator reshapes keeps its
;; role and labels but counts as made by the step, like the faces it makes.
;;
;; Output order, the same everywhere: old vertices keep their numbers and new
;; ones follow in the order they are made; then unused vertices are dropped.
;; Faces the operator didn't rebuild keep their order, and the faces it made
;; follow.
(require racket/list racket/set racket/math racket/vector data/gvector
         "geom.rkt" "shape.rkt" "errors.rkt")
(provide extrude lift inset kis bridge scale-region push merge flip mirror
         move-by turn stretch array spin
         dual ambo truncate gyro reflect
         needle zip join expand ortho meta bevel snub
         current-skips made-by-last-step MAX-FACES)

;; Conway's letters multiply the number of faces, so a short word can ask for
;; millions. Past this many, a step stops (run.rkt checks after each step).
(define MAX-FACES 200000)

;; Operators that can't change some picked element skip it and count it here.
(define current-skips (make-parameter (box 0)))
(define (skip! n) (set-box! (current-skips) (+ n (unbox (current-skips)))))

(define (made-by-last-step s)
  ;; What the selector `new` picks.
  (for/vector ([f (shape-faces s)]) (= (face-step f) (shape-step s))))

;; --- building a shape ------------------------------------------------------------

(define (points-of s)
  ;; A growable copy of s's points, and a procedure that adds one and returns its number.
  (define points (make-gvector))
  (for ([p (shape-vertices s)]) (gvector-add! points p))
  (values points (λ (p) (gvector-add! points p) (sub1 (gvector-count points)))))

(define (finish s points faces)
  (prune (shape (gvector->vector points) faces (add1 (shape-step s)))))

(define (rebuild s picked make)
  ;; Face operators: (make f vertex! step) returns the faces made from the picked face f.
  (define step (add1 (shape-step s)))
  (define-values (points vertex!) (points-of s))
  (define kept (for/list ([f (shape-faces s)] [p picked] #:unless p) f))
  (define made (append* (for/list ([f (shape-faces s)] [p picked] #:when p) (make f vertex! step))))
  (finish s points (append kept made)))

(define (quads ring1 ring2)
  ;; A band of quads joining two rings of the same length: one per edge of ring1.
  (for/list ([e (pairs ring1)] [e2 (pairs ring2)])
    (list (car e) (cdr e) (cdr e2) (car e2))))

;; --- face operators ------------------------------------------------------------------

(define (extrude s picked [height 1.0])
  ;; Each picked face moved along its normal, joined to its old border by walls.
  (rebuild s picked
           (λ (f vertex! step)
             (define out (scale (face-normal s f) height))
             (define top (for/list ([p (face-points s f)]) (vertex! (add p out))))
             (append (for/list ([q (quads (face-corners f) top)]) (face q 'side (face-labels f) step))
                     (list (face top 'top (face-labels f) step))))))

(define (inset s picked [amount 0.3])
  ;; Each picked face shrunk towards its centroid, joined by a ring of quads.
  (unless (< amount 1) (fail 'range "inset: the amount must be less than 1, not ~a" amount))
  (rebuild s picked
           (λ (f vertex! step)
             (define middle (face-centroid s f))
             (define inner (for/list ([p (face-points s f)]) (vertex! (lerp p middle amount))))
             (append (for/list ([q (quads (face-corners f) inner)]) (face q 'rim (face-labels f) step))
                     (list (face inner 'inner (face-labels f) step))))))

(define (kis s picked [height 0.0])
  ;; A tent on each picked face: a point above its centroid joined to every edge.
  (rebuild s picked
           (λ (f vertex! step)
             (define apex (vertex! (add (face-centroid s f) (scale (face-normal s f) height))))
             (for/list ([e (pairs (face-corners f))])
               (face (list (car e) (cdr e) apex) 'tent (face-labels f) step)))))

(define (gyro s picked)
  ;; Each picked n-gon becomes n pentagons around its centroid, using points a
  ;; third of the way along each edge. Unpicked neighbours gain those points.
  (define step (add1 (shape-step s)))
  (define-values (points vertex!) (points-of s))
  (define thirds (make-hash))
  (define (third! v w) (hash-ref! thirds (cons v w) (λ () (vertex! (lerp (point s v) (point s w) 1/3)))))
  (define made
    (append*
     (for/list ([f (shape-faces s)] [p picked] #:when p)
       (define c (vertex! (face-centroid s f)))
       (define vs (face-corners f))
       (for/list ([v vs] [before (cons (last vs) vs)] [after (append (rest vs) (list (first vs)))])
         (define a (third! before v))
         (define b (third! v before))
         (face (list a b v (third! v after) c) 'face (face-labels f) step)))))
  (define (with-thirds f)
    (struct-copy face f
                 [corners (append* (for/list ([e (pairs (face-corners f))])
                                     (if (hash-has-key? thirds e)
                                         (list (car e) (hash-ref thirds e) (hash-ref thirds (cons (cdr e) (car e))))
                                         (list (car e)))))]))
  (finish s points (append (for/list ([f (shape-faces s)] [p picked] #:unless p) (with-thirds f)) made)))

;; --- helpers for regions and corners ---------------------------------------------------

(define (owners faces)
  ;; directed edge -> the face that has it
  (for*/hash ([(f i) (in-indexed faces)] [e (pairs (face-corners f))]) (values e i)))

(define (faces-around s)
  ;; vertex -> the faces that use it, lowest number first
  (define around (make-vector (vector-length (shape-vertices s)) '()))
  (for ([f (reverse (shape-faces s))] [i (in-range (sub1 (length (shape-faces s))) -1 -1)])
    (for ([v (remove-duplicates (face-corners f))]) (vector-set! around v (cons i (vector-ref around v)))))
  around)

(define (ring-around v around faces owner)
  ;; The faces around v in order, starting from the lowest-numbered one: from
  ;; a face where v -> b, the next face is the one with the edge b -> v.
  (define (after f) (let* ([c (face-corners f)] [k (index-of c v)]) (list-ref c (modulo (add1 k) (length c)))))
  (define start (first (vector-ref around v)))
  (let walk ([i start] [ring '()])
    (when (> (length ring) (vector-length faces))
      (fail 'internal "the faces around vertex ~a don't close up" v))
    (define next (hash-ref owner (cons (after (vector-ref faces i)) v)))
    (if (= next start) (reverse (cons i ring)) (walk next (cons i ring)))))

(define (picked-fans ring picked)
  ;; The runs of picked faces in a ring. With every face picked, one run: the
  ;; whole ring. Otherwise the ring is turned to start at its first unpicked face.
  (define k (index-where ring (λ (i) (not (vector-ref picked i)))))
  (cond
    [(not k) (list ring)]
    [else
     (let split ([rest (append (drop ring k) (take ring k))] [fan '()] [fans '()])
       (cond
         [(null? rest) (reverse (if (null? fan) fans (cons (reverse fan) fans)))]
         [(vector-ref picked (car rest)) (split (cdr rest) (cons (car rest) fan) fans)]
         [(null? fan) (split (cdr rest) '() fans)]
         [else (split (cdr rest) '() (cons (reverse fan) fans))]))]))

(define (picked-corners s picked)
  ;; Vertices whose faces are all picked, in order.
  (define around (faces-around s))
  (for/list ([v (in-range (vector-length around))]
             #:when (for/and ([i (vector-ref around v)]) (vector-ref picked i)))
    v))

(define (degrees s)
  ;; vertex -> how many edges meet there
  (define degree (make-vector (vector-length (shape-vertices s)) 0))
  (for* ([f (shape-faces s)] [e (pairs (face-corners f))] #:when (< (car e) (cdr e)))
    (vector-set! degree (car e) (add1 (vector-ref degree (car e))))
    (vector-set! degree (cdr e) (add1 (vector-ref degree (cdr e)))))
  degree)

(define (move s moves)
  ;; The same faces with some vertices moved (moves: vertex -> point), one step later.
  (shape (for/vector ([p (shape-vertices s)] [v (in-naturals)]) (hash-ref moves v p))
         (shape-faces s)
         (add1 (shape-step s))))

;; --- region operators ----------------------------------------------------------------------

(define (lift s picked [height 1.0])
  ;; Each connected piece of picked faces pushed out as one block: walls only
  ;; along the piece's border. A vertex gets one copy per fan of picked faces
  ;; around it, moved along the fan's average normal.
  (define step (add1 (shape-step s)))
  (define faces (list->vector (shape-faces s)))
  (define owner (owners faces))
  (define around (faces-around s))
  (define-values (points vertex!) (points-of s))
  (define copy (make-hash))                               ; (vertex . face) -> moved copy
  (for ([v (in-range (vector-length around))]
        #:when (for/or ([i (vector-ref around v)]) (vector-ref picked i)))
    (for ([fan (picked-fans (ring-around v around faces owner) picked)])
      (define n (unit (for/fold ([m '(0.0 0.0 0.0)]) ([i fan]) (add m (face-normal s (vector-ref faces i))))))
      (define c (vertex! (add (point s v) (scale n height))))
      (for ([i fan]) (hash-set! copy (cons v i) c))))
  (define made
    (append*
     (for/list ([f faces] [i (in-naturals)] #:when (vector-ref picked i))
       (define (copy-of v) (hash-ref copy (cons v i)))
       (append
        (for/list ([e (pairs (face-corners f))]
                   #:unless (vector-ref picked (hash-ref owner (cons (cdr e) (car e)))))
          (face (list (car e) (cdr e) (copy-of (cdr e)) (copy-of (car e))) 'side (face-labels f) step))
        (list (face (map copy-of (face-corners f)) 'top (face-labels f) step))))))
  (finish s points (append (for/list ([f faces] [p picked] #:unless p) f) made)))

;; --- moving the corners of regions ---------------------------------------------------------

(define (regions s picked)
  ;; The regions of picked faces: maximal sets connected through shared
  ;; corners. Each is the list of its corners in order.
  (define faces (list->vector (shape-faces s)))
  (define around (faces-around s))
  (define seen (make-vector (vector-length faces) #f))
  (for/list ([i (vector-length faces)] #:when (and (vector-ref picked i) (not (vector-ref seen i))))
    (let grow ([todo (list i)] [vs (seteqv)])
      (cond
        [(null? todo) (sort (set->list vs) <)]
        [(vector-ref seen (car todo)) (grow (cdr todo) vs)]
        [else
         (define j (car todo))
         (vector-set! seen j #t)
         (define corners (face-corners (vector-ref faces j)))
         (grow (append (cdr todo)
                       (for*/list ([v corners] [k (vector-ref around v)]
                                   #:when (and (vector-ref picked k) (not (vector-ref seen k))))
                         k))
               (set-union vs (list->seteqv corners)))]))))

(define (each-region s picked to)
  ;; Every region's corners moved by (to c p): c is the average of the
  ;; region's corners, p the corner's point. No new faces.
  (move s (for*/fold ([moves (hasheqv)]) ([vs (regions s picked)]
                                          [c (in-value (centroid (map (λ (v) (point s v)) vs)))]
                                          [v vs])
            (hash-set moves v (to c (point s v))))))

(define (scale-region s picked [factor 0.5])
  ;; Each region scaled about its centre.
  (each-region s picked (λ (c p) (lerp c p factor))))

(define (turn s picked [z 0.0] [y 0.0] [x 0.0])
  ;; Each region turned about its centre: by x degrees about the x axis,
  ;; then y about the y axis, then z about the vertical.
  (define (cs a) (let ([t (/ (* a pi) 180)]) (values (cos t) (sin t))))
  (define-values (cx sx) (cs x))
  (define-values (cy sy) (cs y))
  (define-values (cz sz) (cs z))
  (each-region s picked
               (λ (c p)
                 (define-values (x0 y0 z0) (apply values (map - p c)))
                 (define-values (x1 y1 z1) (values x0 (- (* y0 cx) (* z0 sx)) (+ (* y0 sx) (* z0 cx))))
                 (define-values (x2 y2 z2) (values (+ (* x1 cy) (* z1 sy)) y1 (+ (- (* x1 sy)) (* z1 cy))))
                 (add c (list (- (* x2 cz) (* y2 sz)) (+ (* x2 sz) (* y2 cz)) z2)))))

(define (stretch s picked [x 1.0] [y 1.0] [z 1.0])
  ;; Each region stretched about its centre, by x, y and z along each axis.
  (each-region s picked (λ (c p) (add c (map * (map - p c) (list x y z))))))

(define (move-by s picked [x 0.0] [y 0.0] [z 0.0])
  ;; The picked faces' corners moved by (x, y, z).
  (define vs (for*/seteqv ([(f p) (in-parallel (shape-faces s) picked)] #:when p [v (face-corners f)]) v))
  (move s (for/hasheqv ([v vs]) (values v (add (point s v) (list x y z))))))

(define (push s picked [distance 0.5])
  ;; The picked faces' corners moved along the average normal of the picked
  ;; faces around them. No new faces.
  (define sums (make-hasheqv))
  (for ([f (shape-faces s)] [p picked] #:when p)
    (define n (face-normal s f))
    (for ([v (face-corners f)]) (hash-update! sums v (λ (m) (add m n)) '(0.0 0.0 0.0))))
  (move s (for/hasheqv ([(v m) sums]) (values v (add (point s v) (scale (unit m) distance))))))

(define (dual s picked)
  ;; Conway's d inside the picked faces: a point at each picked face's
  ;; centroid; a face around each corner the picked faces surround; and along
  ;; the border, a triangle joining each border edge to its face's point.
  (define step (add1 (shape-step s)))
  (define faces (list->vector (shape-faces s)))
  (define owner (owners faces))
  (define around (faces-around s))
  (define-values (points vertex!) (points-of s))
  (define centre
    (for/hasheqv ([f faces] [i (in-naturals)] #:when (vector-ref picked i))
      (values i (vertex! (face-centroid s f)))))
  (define border
    (for*/list ([(f i) (in-indexed faces)] #:when (vector-ref picked i)
                [e (pairs (face-corners f))]
                #:unless (vector-ref picked (hash-ref owner (cons (cdr e) (car e)))))
      (face (list (car e) (cdr e) (hash-ref centre i)) 'edge (face-labels f) step)))
  (define corner-faces
    (for*/list ([v (in-range (vector-length around))]
                #:when (for/or ([i (vector-ref around v)]) (vector-ref picked i))
                [ring (in-value (ring-around v around faces owner))]
                [full? (in-value (for/and ([i ring]) (vector-ref picked i)))]
                [fan (picked-fans ring picked)]
                #:when (if full? (>= (length fan) 3) (>= (length fan) 2)))
      ;; The ring runs clockwise seen from outside; faces run counterclockwise.
      (define centres (reverse (for/list ([i fan]) (hash-ref centre i))))
      (face (if full? centres (cons v centres)) 'vertex
            (shared-labels (for/list ([i fan]) (vector-ref faces i))) step)))
  (finish s points (append (for/list ([f faces] [p picked] #:unless p) f) border corner-faces)))

(define (flip s picked)
  ;; Flip the edge between two picked triangles: abc + bad -> cad + dbc. Each
  ;; triangle flips at most once, never onto an edge that exists, and never
  ;; when a or b would be left with fewer than three edges.
  (define step (add1 (shape-step s)))
  (define faces (list->vector (shape-faces s)))
  (define owner (make-hash))
  (define (own! i) (for ([e (pairs (face-corners (vector-ref faces i)))]) (hash-set! owner e i)))
  (for ([i (vector-length faces)]) (own! i))
  (define (edge a b) (cons (min a b) (max a b)))
  (define edges-now (mutable-set))
  (for* ([f faces] [e (pairs (face-corners f))]) (set-add! edges-now (edge (car e) (cdr e))))
  (define (triangle? i) (= (length (face-corners (vector-ref faces i))) 3))
  (define (third-of i a b) (first (remove b (remove a (face-corners (vector-ref faces i))))))
  (define degree (degrees s))
  (define done (mutable-seteqv))
  (define (free? i) (and (vector-ref picked i) (triangle? i) (not (set-member? done i))))
  (for ([i (vector-length faces)] #:when (free? i))
    (for/first ([e (pairs (face-corners (vector-ref faces i)))]
                #:when (let ([j (hash-ref owner (cons (cdr e) (car e)))] [a (car e)] [b (cdr e)])
                         (and (free? j)
                              (> (vector-ref degree a) 3) (> (vector-ref degree b) 3)
                              (let ([c (third-of i a b)] [d (third-of j a b)])
                                (and (not (= c d)) (not (set-member? edges-now (edge c d))))))))
      (define-values (a b) (values (car e) (cdr e)))
      (define j (hash-ref owner (cons b a)))
      (define-values (c d) (values (third-of i a b) (third-of j a b)))
      (define labels (shared-labels (list (vector-ref faces i) (vector-ref faces j))))
      (vector-set! faces i (face (list c a d) 'flip labels step))
      (vector-set! faces j (face (list d b c) 'flip labels step))
      (own! i) (own! j)
      (set-remove! edges-now (edge a b))
      (set-add! edges-now (edge c d))
      (for ([v (list a b)]) (vector-set! degree v (sub1 (vector-ref degree v))))
      (for ([v (list c d)]) (vector-set! degree v (add1 (vector-ref degree v))))
      (set-add! done i) (set-add! done j)))
  (skip! (for/sum ([i (vector-length faces)] #:when (and (vector-ref picked i) (not (set-member? done i)))) 1))
  (shape (shape-vertices s) (vector->list faces) step))

;; --- corner operators --------------------------------------------------------------------

(define (cut-corners s picked fraction)
  ;; Cut off each picked corner, `fraction` of the way along each of its
  ;; edges. At one half, an edge cut from both ends shrinks to its midpoint.
  (define merge? (= fraction 0.5))
  (define corners (list->seteqv (picked-corners s picked)))
  (define step (add1 (shape-step s)))
  (define-values (points vertex!) (points-of s))
  (define cuts (make-hash))
  (define (cut! v u)                                      ; the point on edge v-u, near v
    (define key (if (and merge? (set-member? corners u)) (cons (min v u) (max v u)) (cons v u)))
    (hash-ref! cuts key (λ () (vertex! (lerp (point s v) (point s u) fraction)))))
  (define next (make-hasheqv))                            ; corner -> (cut point -> next cut point)
  (define reshaped
    (for/list ([f (shape-faces s)])
      (define vs (face-corners f))
      ;; A face around a cut corner is always picked, so it counts as rebuilt.
      (struct-copy face f
                   [step (if (for/or ([v vs]) (set-member? corners v)) step (face-step f))]
                   [corners (without-repeats
                             (append*
                              (for/list ([w vs] [before (cons (last vs) vs)] [after (append (rest vs) (list (first vs)))])
                                (cond
                                  [(set-member? corners w)
                                   (define-values (a b) (values (cut! w before) (cut! w after)))
                                   (hash-set! (hash-ref! next w make-hasheqv) b a)   ; the corner's face runs b -> a
                                   (list a b)]
                                  [else (list w)]))))])))
  (define around (faces-around s))
  (define faces (list->vector (shape-faces s)))
  (define corner-faces
    (for*/list ([w (sort (set->list corners) <)]
                ;; A corner where only two faces meet would leave a two-sided face; its two
                ;; faces share that edge instead.
                #:when (>= (length (vector-ref around w)) 3))
      (define ring (hash-ref next w))
      (define start (apply min (hash-keys ring)))
      (define corners-of-face
        (let walk ([p start] [out '()])
          (when (> (length out) (hash-count ring)) (fail 'internal "the cuts around vertex ~a don't close up" w))
          (define q (hash-ref ring p))
          (if (= q start) (reverse (cons p out)) (walk q (cons p out)))))
      (face corners-of-face 'vertex
            (shared-labels (for/list ([i (vector-ref around w)]) (vector-ref faces i))) step)))
  (finish s points (append reshaped corner-faces)))

(define (without-repeats vs)
  ;; A face with neighbouring repeats removed, all the way round.
  (for/list ([v vs] [w (append (rest vs) (list (first vs)))] #:unless (= v w)) v))

(define (truncate s picked [fraction 0.3])
  (unless (and (> fraction 0) (<= fraction 0.5))
    (fail 'range "truncate: the fraction must be above 0 and at most 0.5, not ~a" fraction))
  (cut-corners s picked fraction))

(define (ambo s picked) (cut-corners s picked 0.5))

(define (merge s picked)
  ;; The opposite of kis: remove each picked corner, merging its faces into one
  ;; polygon. Newest corners first. A corner is skipped when it is on a polygon
  ;; already merged, when a neighbour would be left with fewer than three
  ;; edges, or when the merged polygon wouldn't be simple.
  (define step (add1 (shape-step s)))
  (define faces (list->vector (shape-faces s)))
  (define around (faces-around s))
  (define degree (degrees s))
  (define gone (mutable-seteqv))
  (define blocked (mutable-seteqv))
  (define corners (reverse (picked-corners s picked)))
  (define merged
    (for*/list ([v corners]
                #:unless (set-member? blocked v)
                [fs (in-value (for/list ([i (vector-ref around v)]) (vector-ref faces i)))]
                [neighbours (in-value (neighbours-of v fs))]
                #:when (for/and ([u neighbours]) (> (vector-ref degree u) 3))
                [polygon (in-value (merged-polygon v fs))]
                #:when polygon)
      (for ([i (vector-ref around v)]) (set-add! gone i))
      (for ([u polygon]) (set-add! blocked u))
      (for ([u neighbours]) (vector-set! degree u (sub1 (vector-ref degree u))))
      (face polygon 'merged (shared-labels fs) step)))
  (skip! (- (length corners) (length merged)))
  (define-values (points vertex!) (points-of s))
  (finish s points (append (for/list ([f faces] [i (in-naturals)] #:unless (set-member? gone i)) f) merged)))

(define (neighbours-of v faces)
  ;; The vertices joined to v by an edge, in order of first appearance.
  (remove-duplicates
   (for*/list ([f faces] [e (pairs (face-corners f))] #:when (or (= (car e) v) (= (cdr e) v)))
     (if (= (car e) v) (cdr e) (car e)))))

(define (merged-polygon v faces)
  ;; The faces around v joined into one, without v; #f if that isn't a simple polygon.
  (define paths                                           ; first vertex -> the face's path around v
    (for/hasheqv ([f faces])
      (define c (face-corners f))
      (define k (index-of c v))
      (define path (rest (append (drop c k) (take c k))))
      (values (first path) path)))
  (define start (apply min (hash-keys paths)))
  (define polygon
    (let walk ([path (hash-ref paths start)] [polygon '()] [count 0])
      (define more (append polygon (drop-right path 1)))
      (cond
        [(> count (length faces)) #f]
        [(= (last path) start) more]
        [(hash-ref paths (last path) #f) => (λ (next) (walk next more (add1 count)))]
        [else #f])))
  (and polygon (>= (length polygon) 3) (not (check-duplicates polygon)) polygon))

;; --- the rest ------------------------------------------------------------------------------

(define (bridge s picked)
  ;; Remove two picked faces and join their borders with a tube: a handle.
  (define chosen (for/list ([f (shape-faces s)] [p picked] #:when p) f))
  (unless (and (= (length chosen) 2)
               (= (length (face-corners (first chosen))) (length (face-corners (second chosen)))))
    (fail 'precondition "bridge needs exactly two faces with the same number of sides, but picked ~a"
          (length chosen)))
  (define-values (fa fb) (values (first chosen) (second chosen)))
  (when (for/or ([v (face-corners fa)]) (memv v (face-corners fb)))
    (fail 'precondition "bridge: the two faces touch, so a tube between them would cross itself"))
  (define joined (for*/set ([f (shape-faces s)] [e (pairs (face-corners f))]) e))
  (when (for*/or ([v (face-corners fa)] [w (face-corners fb)]) (set-member? joined (cons v w)))
    (fail 'precondition "bridge: an edge already joins the two faces, so a tube would run along it"))
  (define a (list->vector (face-corners fa)))
  (define b (list->vector (reverse (face-corners fb))))
  (define n (vector-length a))
  (define (distance v w) (for/sum ([x (point s v)] [y (point s w)]) (sqr (- x y))))
  (define turn                                            ; the first turn with the shortest tube
    (argmin (λ (k) (for/sum ([i n]) (distance (vector-ref a i) (vector-ref b (modulo (+ i k) n)))))
            (range n)))
  (define c (for/vector ([i n]) (vector-ref b (modulo (+ i turn) n))))
  (define step (add1 (shape-step s)))
  (define labels (shared-labels chosen))
  (define-values (points vertex!) (points-of s))
  (finish s points
          (append (for/list ([f (shape-faces s)] [p picked] #:unless p) f)
                  (for/list ([q (quads (vector->list a) (vector->list c))]) (face q 'tube labels step)))))

(define (reflect s picked)
  ;; The mirror image across x = 0. Only of the whole shape.
  (unless (for/and ([p picked]) p)
    (fail 'precondition "reflect acts on the whole shape; leave out the selector"))
  (shape (for/vector ([p (shape-vertices s)]) (cons (- (first p)) (rest p)))
         (for/list ([f (shape-faces s)]) (struct-copy face f [corners (reverse (face-corners f))]))
         (add1 (shape-step s))))

;; --- symmetry -------------------------------------------------------------------------------

(define (mirror s picked)
  ;; The shape joined to its reflection across the picked faces, the seam,
  ;; which are removed: build half of something, get the whole. The seam's
  ;; corners are shared by both halves; every other vertex gets a reflected
  ;; copy, and every unpicked face a reflected copy with its corners reversed.
  (define faces (list->vector (shape-faces s)))
  (define count (for/sum ([p picked]) (if p 1 0)))
  (when (or (zero? count) (= count (vector-length faces)))
    (fail 'precondition "mirror needs a seam: some of the faces, but not all of them"))
  (define owner (owners faces))
  (define around (faces-around s))
  (define seam (make-vector (vector-length (shape-vertices s)) #f))
  (for* ([(f i) (in-indexed faces)] #:when (vector-ref picked i) [v (face-corners f)]) (vector-set! seam v #t))
  (define (seam? v) (vector-ref seam v))
  (for ([v (in-range (vector-length seam))] #:when (seam? v))
    (define ring (ring-around v around faces owner))
    (when (and (for/or ([i ring]) (not (vector-ref picked i)))
               (> (length (picked-fans ring picked)) 1))
      (fail 'precondition "mirror: the seam meets itself at corner ~a, so the halves would be joined there twice" v)))
  (for* ([(f i) (in-indexed faces)] #:unless (vector-ref picked i) [e (pairs (face-corners f))])
    (when (and (seam? (car e)) (seam? (cdr e))
               (not (vector-ref picked (hash-ref owner (cons (cdr e) (car e))))))
      (fail 'precondition (string-append "mirror: edge ~a-~a joins two corners of the seam outside it, so both halves "
                                         "would have it. Often this is a hidden wall between blocks extruded side by "
                                         "side; lift pushes them out as one block")
            (car e) (cdr e))))
  ;; The mirror's plane: through the average of the seam's corners, across the sum of its normals.
  (define c (centroid (for/list ([v (in-range (vector-length seam))] #:when (seam? v)) (point s v))))
  (define n (unit (for/fold ([m '(0.0 0.0 0.0)]) ([(f i) (in-indexed faces)] #:when (vector-ref picked i))
                    (add m (face-normal s f)))))
  (define (reflected p) (add p (scale n (* -2 (dot (map - p c) n)))))
  (define step (add1 (shape-step s)))
  (define-values (points vertex!) (points-of s))
  (define copy (for/vector ([p (shape-vertices s)] [v (in-naturals)]) (if (seam? v) v (vertex! (reflected p)))))
  (define kept (for/list ([f faces] [p picked] #:unless p) f))
  (finish s points
          (append kept
                  (for/list ([f kept])
                    (define corners (map (λ (v) (vector-ref copy v)) (face-corners f)))
                    (struct-copy face f [corners (cons (first corners) (reverse (rest corners)))] [step step])))))

;; --- copies -------------------------------------------------------------------------------

(define (copies s picked n what place)
  ;; The shape and n - 1 copies of it, copy k with every point moved by
  ;; (place k p). Copies keep their roles and labels and are made by this step.
  (unless (for/and ([p picked]) p)
    (fail 'precondition "~a copies the whole shape; leave out the selector" what))
  (unless (and (>= n 1) (= n (floor n)))
    (fail 'range "~a needs a whole number of copies, at least 1, not ~a" what n))
  (when (> (* n (length (shape-faces s))) MAX-FACES)    ; checked before building them
    (fail 'limit "~a makes ~a faces, more than the limit of ~a" what (inexact->exact (* n (length (shape-faces s)))) MAX-FACES))
  (define step (add1 (shape-step s)))
  (define nv (vector-length (shape-vertices s)))
  (define ks (range 1 (inexact->exact n)))
  (shape (apply vector-append (shape-vertices s)
                (for/list ([k ks]) (for/vector ([p (shape-vertices s)]) (place k p))))
         (append (shape-faces s)
                 (for*/list ([k ks] [f (shape-faces s)])
                   (struct-copy face f [corners (map (λ (v) (+ v (* k nv))) (face-corners f))] [step step])))
         step))

(define (array s picked [n 2.0] [x 1.0] [y 0.0] [z 0.0])
  ;; n copies in a row, each moved (x, y, z) from the one before.
  (copies s picked n "array" (λ (k p) (add p (list (* k x) (* k y) (* k z))))))

(define (spin s picked [n 6.0])
  ;; n copies turned evenly about the vertical axis through the origin.
  (copies s picked n "spin"
          (λ (k p)
            (define a (/ (* 2 pi k) n))
            (list (- (* (first p) (cos a)) (* (second p) (sin a)))
                  (+ (* (first p) (sin a)) (* (second p) (cos a)))
                  (third p)))))

;; Conway's other letters are his identities. The rightmost step acts on the
;; picked faces; each step to its left acts on the faces the one before made.
(define ((on-made . steps) s picked)
  (for/fold ([s s] [picked picked] #:result s) ([step (reverse steps)])
    (define next (step s picked))
    (values next (made-by-last-step next))))

(define needle (on-made kis dual))                        ; n = kd
(define zip (on-made dual kis))                           ; z = dk
(define join (on-made dual ambo))                         ; j = da
(define expand (on-made ambo ambo))                       ; e = aa
(define ortho (on-made dual ambo ambo))                   ; o = daa
(define meta (on-made kis dual ambo))                     ; m = kda
(define bevel (on-made truncate ambo))                    ; b = ta
(define snub (on-made dual gyro))                         ; s = dg
