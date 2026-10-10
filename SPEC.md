# Modelita

**Language specification — draft 1**

Modelita is a small language for building closed polyhedral shapes. A
*word* starts from a seed shape and applies operators to it, right to
left, like Conway's polyhedron notation. Unlike Conway's operators, which
act on everything, every Modelita operator takes a *selector* and rebuilds
only the faces it picks.

```
house  = k[top](0.9) x[+z](0.6) C       ; a name for a shape
storey(h) = x[inner](h) i[top](0.3)     ; a reusable step with a parameter
k[top](0.8) storey(1.2)^3 x[+z]{tower} C
```

A word is plain text, so it can be stored, sent and shared anywhere. Any
implementation that follows this document builds the same shape from it:
the same vertices in the same order, and the same faces in the same order
with the same tags. The conformance suite in `tests/` checks this.

This spec aims for:

1. **One meaning.** Every word means exactly one shape or one error.
2. **Totality.** Operators applied to a valid shape give a valid shape.
3. **Stable references.** Roles and labels let a selector name the same
   faces when an earlier step changes.
4. **Continuity.** Small changes to numbers give small changes to the shape.
5. **A small core.** Six of Conway's operators and nine modelling operators.
   Everything else is defined from them.

---

## 1. Shapes

A **shape** has:

- **vertices**: a sequence of points `(x, y, z)`, with z up;
- **faces**: a sequence of faces. Each face is a cycle of at least three
  vertex indices, counterclockwise seen from outside. A face also has:
  - a **role**: a word saying what made it (`seed`, `top`, `rim`, …);
  - **labels**: a set of names the word gave it (§4);
  - a **step**: the number of the step that made it (§4);
- a **step counter**: how many steps have run since the seed (0 for a seed).

Vertex and face numbers count from 0.

**Invariant.** Every shape is a closed, oriented 2-manifold:

- every directed edge `a→b` belongs to exactly one face, and its reverse
  `b→a` belongs to exactly one other face;
- every vertex is used by at least one face.

Holes (genus > 0) are allowed. An implementation checks the invariant
after every step. A violation is an `internal` error: a bug in the
implementation, never in the word.

The invariant is **topological only**. Faces may be non-planar,
degenerate (zero area) or self-intersecting. Implementations may offer a
separate `lint` that reports these, but it is not part of the language.

**Geometry used throughout:**

- The **centroid** of a face is the plain average of its corners' points.
- The **normal** of a face comes from Newell's method:
  ```
  n = Σ over edges (p → q):  ( (p.y − q.y)(p.z + q.z),
                               (p.z − q.z)(p.x + q.x),
                               (p.x − q.x)(p.y + q.y) )
  ```
  scaled to length 1. A zero-length normal stays zero.
- `unit(v)` is `v` scaled to length 1, or `v` itself if its length is 0.
- `lerp(p, q, t) = p + (q − p)·t`.

**Corners and fans.** These are used by the operators that act on
corners or regions:

- A vertex is a **picked corner** when every face that uses it is picked.
- The **ring** around vertex `v` lists the faces around it in order. It
  starts at the lowest-numbered face that uses `v`. From a face in which
  `v` is followed by `b`, the next face is the one containing the edge
  `b→v`. The ring ends when it returns to its start. Seen from outside, it
  runs clockwise.
- The **fans** of a ring are its maximal runs of consecutive picked faces.
  If every face in the ring is picked, there is one fan, the whole ring in
  ring order. Otherwise, rotate the ring to start at its first unpicked
  face, and the fans are the picked runs in that order.

## 2. Seeds

A seed is the shape a word starts from. Seeds are written in capitals and
take numbers like operators do (§3). Every seed face has role `seed`, no
labels, and step 0.

| Seed | Numbers (defaults) | Shape |
|---|---|---|
| `C` | `w, d, h` (2, 2, 2) | box centred on the origin, with size `w × d × h` along x, y, z |
| `T` | `r` (√3) | tetrahedron, circumradius `r` |
| `O` | `r` (√3) | octahedron, circumradius `r` |
| `I` | `r` (√3) | icosahedron, circumradius `r` |
| `D` | `r` (√3) | dodecahedron, circumradius `r` |
| `Pn` | `r, h` (1, square sides) | prism with n sides, circumradius `r`, height `h` |
| `An` | `r, h` (1, equilateral sides) | antiprism with n sides |
| `Yn` | `r, h` (1, `r`) | pyramid with n sides, base on z = 0, apex at (0, 0, h) |

With the defaults, C, T, O, I and D all have circumradius √3, the
circumradius of the cube with side 2.

Every size must be greater than 0, and `n` must be at least 3. Otherwise
the word fails with a `range` error.

**Exact construction.** The vertex and face order below is part of the
spec.

- **C**: vertices for x in (−w/2, w/2), then y in (−d/2, d/2), then z in
  (−h/2, h/2), with z changing fastest. Faces:
  `0 1 3 2 · 4 6 7 5 · 0 4 5 1 · 2 3 7 6 · 0 2 6 4 · 1 5 7 3`.
- **T**: vertices `(1,1,1) (1,−1,−1) (−1,1,−1) (−1,−1,1)`, scaled by
  `r/√3`. Faces: `0 1 2 · 0 3 1 · 0 2 3 · 1 3 2`.
- **Convex seeds (O, I, P, A, Y).** Each face listed below is checked
  against the centroid `c` of all the seed's vertices. If the face's normal
  · (face centroid − c) is not positive, its corners are reversed.
- **O**: vertices `(r,0,0) (−r,0,0) (0,r,0) (0,−r,0) (0,0,r) (0,0,−r)`.
  Faces: `[x, y, z]` for x in (0, 1), y in (2, 3), z in (4, 5), with z
  changing fastest.
- **I**: let φ = (1 + √5)/2. Unscaled vertices: for a in (−1, 1), for b
  in (−φ, φ): `(0, a, b)`, `(a, b, 0)`, `(b, 0, a)`, in that order (12 in
  all). Faces: every triple i < j < k of vertices whose unscaled distances
  are all 2, in lexicographic order. The vertices are then scaled by
  `r / √(1 + φ²)`.
- **D**: the vertices and faces of `d I` (§5.3), with every vertex then
  scaled to distance `r` from the origin.
- **Pn**: `ring(n, r, z, turn)` is the points
  `(r·cos(turn + 2πi/n), r·sin(turn + 2πi/n), z)` for i = 0 … n−1.
  Vertices: `ring(n, r, −h/2, 0)`, then `ring(n, r, h/2, 0)`. Faces: the
  bottom `0 … n−1`, the top `n … 2n−1`, then for each i the quad
  `i, i+1, n+i+1, n+i` (indices within each ring taken mod n). Default
  `h = 2r·sin(π/n)`.
- **An**: vertices `ring(n, r, −h/2, 0)`, then `ring(n, r, h/2, π/n)`.
  Faces: the bottom, the top, then for each i the triangle `i, i+1, n+i`,
  then for each i the triangle `n+i, i+1, n+i+1` (mod n within each ring).
  Default `h = 2r·√((cos(π/n) − cos(2π/n)) / 2)`.
- **Yn**: vertices `ring(n, r, 0, 0)`, then `(0, 0, h)`. Faces: the base
  `0 … n−1`, then for each i the triangle `i, i+1, n`.

## 3. Words

A **word** is tokens separated by whitespace and applied right to left:

```
k[top](0.8)   storey(1.2)^3   x[+z]{tower}   C
    4th            3rd            2nd        1st
```

A token is

```
head [selector] (numbers) {label} ^count
```

and every part but the head is optional:

- **head**: an operator's name or letter (§5), a seed (§2), or a defined
  name (§6);
- **`[selector]`**: which faces the operator acts on (§7). Only primitive
  operators take one; without it, they act on `all` faces;
- **`(numbers)`**: comma-separated numbers or parameter names (§6). A
  primitive operator or seed may be given fewer numbers than it takes, and
  the rest use the defaults. A defined name with parameters must be given
  exactly one number per parameter;
- **`{label}`**: a label for every face this token makes (§4);
- **`^count`**: repeat the token `count` (≥ 1) times. Only operators can be
  repeated.

A word that ends in a shape (a seed, or a name for a shape) **is a shape**.
A word that ends in an operator **is an operator**: a reusable sequence of
steps. A shape anywhere but last is a `kind` error.

## 4. Steps, roles and labels

Each application of a primitive operator is a **step**. It raises the
shape's step counter by one, even when the selector picks nothing.

A face **made** by a step has that step's number. These are:

- every face the operator creates;
- every *picked* face the operator reshapes (it keeps its role and labels).

An *unpicked* face that only gains or loses corners where it meets picked
faces keeps its step.

**Roles.** Each operator gives the faces it creates a role, listed per
operator in §5. The roles are:
`seed top side inner rim tent tube vertex edge face merged flip`.

**Labels.** A face's labels are a set of names:

- **Inherited.** A face created from other faces gets the labels that
  *all* of those faces share. §5 says which faces each created face comes
  from.
- **Given.** A token with `{name}` adds `name` to every face made by any of
  its steps (or, for a seed or a named shape, to every face).

Labels are lowercase names: `[a-z][a-z0-9_-]*`. A label may not be a
selector keyword (`all`, `new`), a role, or of the form `n` + digits.
Using one is a `name` error.

So `x[+z](0.6){roof} C` makes `roof` faces: `roof&top` is the lid and
`roof&side` the walls. After `i[roof&top]`, the inset ring and inner face
inherit `roof` as well.

## 5. Operators

Every operator follows one rule: **it rebuilds only what the selector
picked.** Faces it didn't pick keep their corners, except that they may gain
or lose corners along edges they share with picked faces.

**Output order** follows one convention unless an operator says otherwise:

- Existing vertices keep their numbers. New vertices are numbered after
  them, in the order the operator creates them.
- Unpicked faces come first, in their old order. Then come the made faces:
  for each picked face in order, the faces made from it, in the order
  listed.
- Finally, vertices no face uses any more are removed, and the rest are
  renumbered keeping their order.

Every operator has a name, and some also have a letter. The two are
interchangeable: `x[+z]` and `extrude[+z]` mean the same. Single
lowercase letters are reserved for Conway's notation and its extensions.

### 5.1 Modelling operators

| Name | Letter | Number (default, allowed) | Makes |
|---|---|---|---|
| `extrude` | `x` | height (1, any) | `side`, `top` |
| `inset` | `i` | amount (0.3, < 1) | `rim`, `inner` |
| `kis` | `k` | height (0, any) | `tent` |
| `bridge` | `h` | — | `tube` |
| `lift` | — | height (1, any) | `side`, `top` |
| `scale` | — | factor (0.5, any) | — |
| `push` | — | distance (0.5, any) | — |
| `merge` | — | — | `merged` |
| `flip` | — | — | `flip` |

**extrude** — For each picked face `v₀ … vₙ₋₁` with normal `N`: new
vertices `tᵢ = p(vᵢ) + height·N`, in order. Made, from that face: a wall
`vᵢ, vᵢ₊₁, tᵢ₊₁, tᵢ` for each i (role `side`), then the lid `t₀ … tₙ₋₁`
(role `top`). Neighbouring picked faces get separate blocks. To push
several faces out as one block, use `lift`.

**inset** — For each picked face with centroid `c`: new vertices
`iᵢ = lerp(p(vᵢ), c, amount)`, in order. Made: `vᵢ, vᵢ₊₁, iᵢ₊₁, iᵢ` for
each i (role `rim`), then `i₀ … iₙ₋₁` (role `inner`). A negative amount
grows the face outwards. An amount of 1 or more is a `range` error.

**kis** — For each picked face with centroid `c` and normal `N`: one new
vertex `a = c + height·N`. Made: `vᵢ, vᵢ₊₁, a` for each i (role `tent`).
With height 0 this is Conway's `k`.

**bridge** — The selector must pick exactly two faces, `A` then `B` in
face order. They must have the same number of corners `n`, no corner in
common, and no edge joining a corner of A to a corner of B (the tube would
run along it). Otherwise it is a `precondition` error. Let `a = A`, and `b` =
`B`'s corners reversed. Choose the turn `k` in 0 … n−1 that minimises
`Σᵢ |p(aᵢ) − p(b₍ᵢ₊ₖ₎ mod n)|²`, taking the smallest k on a tie, and let
`cᵢ = b₍ᵢ₊ₖ₎ mod n`. A and B are removed. Made: `aᵢ, aᵢ₊₁, cᵢ₊₁, cᵢ` for
each i (role `tube`, labels shared by A and B). It adds one handle, and it
is the only operator that changes the genus.

**lift** — Pushes each connected piece of picked faces out as one block.
For each vertex `v` in order that is used by a picked face, and for each
fan of its ring in order: one new vertex
`p(v) + height·unit(Σ normals of the fan's faces)`. That copy belongs to
the faces of the fan. Made, for each picked face in order:

- a wall `a, b, copy(b), copy(a)` for each edge `a→b` whose other face is
  unpicked (role `side`);
- then the face's corners replaced by their copies (role `top`).

`lift` on every face of a shape has no walls: it moves every corner
outwards.

**scale** — A *region* is a maximal set of picked faces connected through
shared vertices. For each region, let `c` be the average of its distinct
vertices' points. Each of those vertices moves to `lerp(c, p, factor)`.
No faces are made.

**push** — Each vertex of a picked face moves by
`distance·unit(Σ normals of the picked faces that use it)`. No faces are
made.

**merge** — Undoes a kis. The picked corners are taken from the
highest-numbered vertex down. Corner `v` is skipped if any of these hold:

- `v` lies on a polygon already merged by this step;
- a vertex joined to `v` by an edge has 3 or fewer edges at that point;
- the merged polygon is not simple.

**The merged polygon:** take each face around `v`, rotated to start at
`v`, without `v`. Call these the face's *paths*. Start with the path whose
first vertex is lowest, and repeatedly append the path that starts where
the last one ended, dropping each path's last vertex. The polygon is
simple if this closes back to the start, uses every face, and repeats no
vertex.

A merged corner removes its faces and adds the polygon (role `merged`).
Each vertex on the polygon becomes "already merged", and each neighbour of
`v` loses one edge. Faces not removed keep their order, and the merged
polygons follow in the order they were made. Each skipped corner counts as
one **skip** (§9).

**flip** — For each picked triangle `i` in face order that hasn't flipped
yet: find the first edge `a→b` of `i` whose other face `j` meets all of
these conditions:

- `j` is a picked triangle that hasn't flipped yet;
- both `a` and `b` have more than 3 edges;
- the third corners `c` (of `i`) and `d` (of `j`) differ;
- `c–d` is not already an edge.

If there is one, `i` becomes `c, a, d` and `j` becomes `d, b, c`, in place
(role `flip`, labels shared by both). Edge and degree counts update as
flips happen. Every picked face that didn't flip counts as one skip.

### 5.2 Conway's operators

| Name | Letter | Number (default, allowed) | Makes |
|---|---|---|---|
| `dual` | `d` | — | `edge`, `vertex` |
| `ambo` | `a` | — | `vertex` |
| `truncate` | `t` | fraction (0.3, above 0 and ≤ 0.5) | `vertex` |
| `kis` | `k` | §5.1 | `tent` |
| `gyro` | `g` | — | `face` |
| `reflect` | `r` | — | — |

With every face picked, each is exactly Conway's operator.

**truncate / ambo** — Cuts off every picked corner. `ambo` is `truncate`
with fraction 0.5. Write `cut(w, u)` for the point `lerp(p(w), p(u),
fraction)` on edge `w–u` near `w`. It is created the first time it is
needed. At fraction 0.5, when `u` is also a picked corner, `cut(w, u)` and
`cut(u, w)` are the same point.

Every face is rewritten in order. Each corner `w` that is a picked corner
is replaced by `cut(w, previous corner), cut(w, next corner)`, and then
consecutive repeats are removed all the way round. Faces that changed were
picked, so they count as made by this step (§4).

Then, for each picked corner `w` in increasing order where at least three
faces meet, add a face (role `vertex`, labels shared by the faces around
`w`). Its corners: in each face around `w`, the corner was replaced by
`a, b`, which links `b` to `a`. Start from the lowest-numbered of these cut
points and follow the links until returning to the start.

A corner where only two faces meet (a partial `g` leaves such corners)
gets no face, because it would have only two sides. Its two faces already
share the edge between its cut points.

The fraction must be above 0 and at most 0.5, otherwise it is a `range`
error.

**dual** — Builds the dual inside the picked faces:

1. For each picked face in order, a new vertex at its centroid.
2. **Border triangles**: for each picked face in order, and each edge
   `a→b` whose other face is unpicked: `a, b, centre` (role `edge`,
   labels of that face).
3. **Corner faces**: for each vertex `v` in order that is used by a picked
   face, and each fan of its ring in order. A full ring (every face
   picked) of 3 or more faces gives the fan's centres in *reverse* fan
   order. A full ring of 2 faces gives nothing: its two faces' corner
   faces already share the edge between their centres. A partial fan of
   2 or more faces gives `v` followed by the fan's centres in reverse
   order. Partial fans of one face give nothing. Role `vertex`, labels
   shared by the fan's faces.

The output is the unpicked faces, then the border triangles, then the
corner faces, and unused vertices are removed. On a single face, `d` is a
kis.

**gyro** — Write `third(p, q)` for the point `lerp(p(p), p(q), 1/3)`,
created the first time it is needed. For each picked face in order:

- a new vertex `c` at its centroid;
- then for each corner `v`, with previous corner `u` and next corner
  `w`, the pentagon `third(u, v), third(v, u), v, third(v, w), c` (role
  `face`), creating the points in that order.

Each unpicked face gains, along each edge `a→b` for which `third(a, b)`
exists, the corners `third(a, b), third(b, a)` after `a`. The output is
the unpicked faces, then the pentagons.

**reflect** — The mirror image across x = 0: every x is negated and every
face's corners are reversed. Faces keep their role, labels and step. A
selector that doesn't pick every face is a `precondition` error.

### 5.3 Derived letters

| Name | Letter | Means |
|---|---|---|
| `needle` | `n` | `kd` |
| `zip` | `z` | `dk` |
| `join` | `j` | `da` |
| `expand` | `e` | `aa` |
| `ortho` | `o` | `daa` |
| `meta` | `m` | `kda` |
| `bevel` | `b` | `ta` |
| `snub` | `s` | `dg` |

Each runs its letters right to left, each with its default number. The
rightmost uses the token's selector, and each one after it acts on `new`
(the faces the previous one made). With every face picked, these are
Conway's identities. A derived letter takes no numbers and runs several
steps.

## 6. Definitions

```
name = word
name(p₁, p₂, …) = word
```

A definition gives a word a name. Names match `[A-Za-z][A-Za-z0-9_-]*`.
It is a `name` error if the name is:

- an operator's name or letter;
- any single lowercase letter;
- a seed;
- already defined.

A name can be used from the next line on.

A definition without parameters is checked when it is defined, so an error
in it is reported on its own line. Parameters are numbers. A parameter's
name may appear wherever a number may in the word (an operator's or seed's
numbers, or another name's arguments). A definition is evaluated afresh
each time it is used.

## 7. Selectors

A selector is evaluated against the shape just before the step runs. It
picks each face for which it is true.

| Selector | Picks |
|---|---|
| `all` | every face |
| `new` | faces whose step is the shape's step counter: the faces made by the previous step (the seed's faces, on a seed) |
| `name` | faces whose role is `name`, or that have the label `name` |
| `+x -x +y -y +z -z` | faces whose normal's component along that axis, signed, is above 0.9 |
| `n4` | faces with exactly 4 corners |
| `#3` | the face numbered 3 |
| `a&b` | faces picked by both |
| `a|b` | faces picked by either |
| `!a` | faces not picked by `a` |
| `~a` | faces not picked by `a` that share an edge with a face picked by `a` |
| `(a)` | grouping |

Precedence, tightest first: `!` and `~`, then `&`, then `|`. Spaces are
allowed inside `[ ]`.

`#n` depends on every step before it. It is meant for people pointing at
faces in a tool; generated words should name faces by role and label
instead.

## 8. Programs

A program is lines, one statement per line. `;` starts a comment that runs
to the end of the line.

| Statement | Means |
|---|---|
| `name = word`, `name(p, …) = word` | a definition (§6) |
| `word` | show the word |
| `use "file"` | the definitions in another program, relative to this one |
| `gallery "file.svg"` | draw the words shown so far (a directive) |
| `export "file.obj" word` | show the word, and write its shape as OBJ (a directive) |

Definitions, words and `use` are the **core**. `gallery` and `export` are
**directives**: hosts carry them out as they can (a terminal tool writes
files, a web page may offer a download), and they never change what a word
means.

When writing OBJ, `(x, y, z)` becomes `(x, z, −y)` (y up), faces keep
their order, and each role starts a group.

## 9. Errors and skips

When a statement fails, it produces an error with one of these categories:

| Category | When |
|---|---|
| `syntax` | the line can't be read, or a selector is given to something other than a primitive operator |
| `name` | an undefined name, a reserved or duplicate definition, a missing parameter, or a forbidden label |
| `kind` | a shape that isn't last in its word, or a repeated shape |
| `arity` | too many numbers, or a defined name given the wrong number of arguments |
| `range` | a number outside what the operator or seed allows |
| `precondition` | the selector picks something the operator can't work with (`bridge`, `reflect`) |
| `limit` | a step makes more than 200 000 faces |
| `internal` | an implementation broke the invariant (§1) |

A statement is processed in this order, and the first error found is
reported:

1. reading the line;
2. resolving tokens left to right (names, numbers, building seeds, and
   `^` on a shape);
3. checking the word's kinds;
4. running the steps right to left.

A failed statement doesn't stop the program, and a failed definition
defines nothing.

A **skip** is not an error. It counts the picked elements an operator
could not change (`merge`, `flip`). Tools should show the count.

## 10. Conformance

`tests/cases/` holds programs (`.mdl`) and their expected dumps (`.out`).
`tests/README.md` describes the dump format. An implementation conforms
when `tests/run.py` passes against it. Coordinates are compared with a
tolerance of 1e-9; everything else must match exactly.

## 11. Not in v1

- several bodies, unions and other booleans;
- moving, rotating or scaling the whole shape (apart from `r`);
- subdivision and curves;
- symmetry operators and arrays beyond `^n`;
- selectors as parameters.

The likely order for adding them: symmetry, whole-shape transforms, one
subdivision operator, several bodies.
