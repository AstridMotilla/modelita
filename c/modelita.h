/* modelita.h — the Modelita shape language, in one C99 header.

   In exactly one C file, before including it:

       #define MODELITA_IMPLEMENTATION
       #include "modelita.h"

   Everywhere else, include it as usual. No dependencies beyond the C
   standard library (link with -lm).

   One word:

       mdl_error err;
       mdl_shape *s = mdl_word("k[top](0.9) x[+z](0.6) C", &err);
       if (!s) printf("%s error: %s\n", mdl_status_name(err.status), err.message);
       ... mdl_vertices(s), mdl_face_corners(s, i), ...
       mdl_shape_free(s);

   A whole program (definitions, `use`, shown words) calls back once per
   statement that shows something or fails; see mdl_run_program.

   The language is defined by SPEC.md; this header follows draft 1 and
   passes the conformance suite in tests/.

   There is no global state: each call allocates its own memory and frees
   it, so calls on different threads don't interfere.

   MIT License, Copyright (c) 2026 astridmotilla. See LICENSE in the
   Modelita repository for the full text. */

#ifndef MODELITA_H
#define MODELITA_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MDL_OK = 0,
    MDL_ERR_SYNTAX,       /* the line can't be read */
    MDL_ERR_NAME,         /* undefined, reserved or duplicate name; bad label */
    MDL_ERR_KIND,         /* a shape where an operator must be */
    MDL_ERR_ARITY,        /* wrong number of numbers or arguments */
    MDL_ERR_RANGE,        /* a number outside what is allowed */
    MDL_ERR_PRECONDITION, /* the selector picks something the operator can't use */
    MDL_ERR_LIMIT,        /* a step makes more than MDL_MAX_FACES faces */
    MDL_ERR_INTERNAL      /* a bug: an operator broke the shape */
} mdl_status;

#define MDL_MAX_FACES 200000

typedef struct {
    mdl_status status;
    int line;             /* in a program, the line it happened on (from 1) */
    char message[256];
} mdl_error;

typedef struct mdl_shape mdl_shape;

/* The shape a word builds, or NULL with *error filled in. Free it with
   mdl_shape_free. Names don't work here: they belong to a program. */
mdl_shape *mdl_word(const char *word, mdl_error *error);
void mdl_shape_free(mdl_shape *shape);

/* Reading a shape. Vertices are x y z triples, z up. Faces list their
   corners counterclockwise seen from outside. */
int mdl_vertex_count(const mdl_shape *s);
const double *mdl_vertices(const mdl_shape *s);
int mdl_face_count(const mdl_shape *s);
int mdl_face_size(const mdl_shape *s, int face);
const int *mdl_face_corners(const mdl_shape *s, int face);
const char *mdl_face_role(const mdl_shape *s, int face);
int mdl_face_step(const mdl_shape *s, int face);
int mdl_face_label_count(const mdl_shape *s, int face);
const char *mdl_face_label(const mdl_shape *s, int face, int k);

/* Programs. The callback runs once for each statement that shows a word
   (a plain word, or `export`) and for each statement that fails, in order.
   Everything it is given is valid only during the call. */
typedef enum { MDL_SHOW, MDL_EXPORT, MDL_DEFINE, MDL_USE, MDL_GALLERY } mdl_statement;

typedef struct {
    int line;
    mdl_statement statement;
    const mdl_shape *shape;  /* the shape shown, or NULL */
    int is_operator;         /* 1 when the word shown is an operator */
    const char *file;        /* the file named by export, use or gallery */
    int skipped;             /* picked elements the steps couldn't change */
    mdl_error error;         /* status MDL_OK unless the statement failed */
} mdl_result;

typedef void (*mdl_result_fn)(const mdl_result *result, void *user);

/* path is the program's file, for `use` (NULL: relative to the current
   directory). */
void mdl_run_program(const char *text, const char *path, mdl_result_fn fn, void *user);

const char *mdl_status_name(mdl_status status);   /* "syntax", "name", ... */

#ifdef __cplusplus
}
#endif
#endif /* MODELITA_H */

/* ===================================================================== */

#ifdef MODELITA_IMPLEMENTATION

#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- memory: an arena per statement ----------------------------------------
   Everything a statement makes comes from one arena, freed when the
   statement is done; errors jump out without cleaning up. */

typedef struct mdl__block { struct mdl__block *next; size_t used, cap; } mdl__block;
typedef struct { mdl__block *head; } mdl__arena;

static void *mdl__alloc(mdl__arena *a, size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (!a->head || a->head->used + n > a->head->cap) {
        size_t cap = n > ((size_t)1 << 20) ? n : ((size_t)1 << 20);
        mdl__block *b = (mdl__block *)malloc(sizeof(mdl__block) + 16 + cap);
        if (!b) { fputs("modelita: out of memory\n", stderr); abort(); }
        b->next = a->head; b->used = 0; b->cap = cap;
        a->head = b;
    }
    void *p = (char *)(a->head + 1) + 16 + a->head->used;
    a->head->used += n;
    return p;
}

static void *mdl__zalloc(mdl__arena *a, size_t n) { void *p = mdl__alloc(a, n); memset(p, 0, n); return p; }

static void mdl__arena_free(mdl__arena *a) {
    while (a->head) { mdl__block *next = a->head->next; free(a->head); a->head = next; }
}

static char *mdl__strndup(mdl__arena *a, const char *s, size_t n) {
    char *p = (char *)mdl__alloc(a, n + 1);
    memcpy(p, s, n); p[n] = 0;
    return p;
}

/* Growable arrays: struct { T *items; int n, cap; }. */
#define MDL__VEC(T) struct { T *items; int n, cap; }
#define MDL__PUSH(a, vec, x) do { \
        if ((vec).n == (vec).cap) mdl__grow((a), (void **)&(vec).items, &(vec).cap, sizeof *(vec).items); \
        (vec).items[(vec).n++] = (x); } while (0)

static void mdl__grow(mdl__arena *a, void **items, int *cap, size_t size) {
    int next = *cap ? *cap * 2 : 8;
    void *p = mdl__alloc(a, (size_t)next * size);
    if (*cap) memcpy(p, *items, (size_t)*cap * size);
    *items = p; *cap = next;
}

/* --- errors ------------------------------------------------------------------ */

/* A context lives on the heap: after a longjmp, the C standard leaves
   locals changed since setjmp indeterminate, but not memory they point to. */
typedef struct {
    mdl__arena *a;          /* where this statement's work goes */
    mdl__arena work;        /* the statement's own arena */
    jmp_buf *jump;
    mdl_status status;
    char message[256];
    int skips;
} mdl__ctx;

static void mdl__fail(mdl__ctx *x, mdl_status status, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(x->message, sizeof x->message, fmt, ap);
    va_end(ap);
    x->status = status;
    longjmp(*x->jump, 1);
}

const char *mdl_status_name(mdl_status s) {
    static const char *names[] = {"ok", "syntax", "name", "kind", "arity", "range",
                                  "precondition", "limit", "internal"};
    return (s >= MDL_OK && s <= MDL_ERR_INTERNAL) ? names[s] : "?";
}

/* --- maps from pairs of ints to ints -------------------------------------------- */

typedef struct { uint64_t *keys; int *vals; int cap, count; mdl__arena *a; } mdl__map;
#define MDL__EMPTY UINT64_MAX

static uint64_t mdl__key(int a, int b) { return ((uint64_t)(uint32_t)a << 32) | (uint32_t)b; }

static void mdl__map_init(mdl__map *m, mdl__arena *a, int hint) {
    int cap = 16;
    while (cap < hint * 2) cap *= 2;
    m->a = a; m->cap = cap; m->count = 0;
    m->keys = (uint64_t *)mdl__alloc(a, sizeof(uint64_t) * (size_t)cap);
    m->vals = (int *)mdl__alloc(a, sizeof(int) * (size_t)cap);
    for (int i = 0; i < cap; i++) m->keys[i] = MDL__EMPTY;
}

static int mdl__slot(const mdl__map *m, uint64_t k) {
    uint64_t h = k * 0x9E3779B97F4A7C15ull;
    int i = (int)(h >> 33) & (m->cap - 1);
    while (m->keys[i] != MDL__EMPTY && m->keys[i] != k) i = (i + 1) & (m->cap - 1);
    return i;
}

static void mdl__map_set(mdl__map *m, uint64_t k, int v);

static void mdl__map_rehash(mdl__map *m) {
    uint64_t *keys = m->keys; int *vals = m->vals; int cap = m->cap;
    mdl__map_init(m, m->a, cap);
    for (int i = 0; i < cap; i++) if (keys[i] != MDL__EMPTY) mdl__map_set(m, keys[i], vals[i]);
}

static void mdl__map_set(mdl__map *m, uint64_t k, int v) {
    if ((m->count + 1) * 2 > m->cap) mdl__map_rehash(m);
    int i = mdl__slot(m, k);
    if (m->keys[i] == MDL__EMPTY) { m->keys[i] = k; m->count++; }
    m->vals[i] = v;
}

static int mdl__map_get(const mdl__map *m, uint64_t k, int missing) {
    int i = mdl__slot(m, k);
    return m->keys[i] == MDL__EMPTY ? missing : m->vals[i];
}

/* --- shapes --------------------------------------------------------------------------- */

typedef struct { double x, y, z; } mdl__v3;

enum { R_SEED, R_TOP, R_SIDE, R_INNER, R_RIM, R_TENT, R_TUBE, R_VERTEX, R_EDGE, R_FACE, R_MERGED, R_FLIP, R_COUNT };
static const char *mdl__roles[R_COUNT] = {"seed", "top", "side", "inner", "rim", "tent", "tube",
                                          "vertex", "edge", "face", "merged", "flip"};

/* Labels are sorted arrays of strings; faces share them, nobody changes one in place. */
typedef struct { const char **items; int n; } mdl__labels;

typedef struct { int *c; int n; int role; mdl__labels labels; int step; } mdl__face;
typedef struct { mdl__v3 *v; int nv; mdl__face *f; int nf; int step; } mdl__shape;

static mdl__v3 mdl__add(mdl__v3 p, mdl__v3 q) { mdl__v3 r = {p.x + q.x, p.y + q.y, p.z + q.z}; return r; }
static mdl__v3 mdl__scale(mdl__v3 p, double s) { mdl__v3 r = {p.x * s, p.y * s, p.z * s}; return r; }
static double mdl__dot(mdl__v3 p, mdl__v3 q) { return p.x * q.x + p.y * q.y + p.z * q.z; }
static mdl__v3 mdl__lerp(mdl__v3 p, mdl__v3 q, double t) {
    mdl__v3 r = {p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t};
    return r;
}
static mdl__v3 mdl__unit(mdl__v3 p) {
    double l = sqrt(mdl__dot(p, p));
    return l == 0 ? p : mdl__scale(p, 1.0 / l);
}

static mdl__v3 mdl__normal_of(const mdl__v3 *pts, int n) {
    /* Newell's method */
    double nx = 0, ny = 0, nz = 0;
    for (int i = 0; i < n; i++) {
        mdl__v3 p = pts[i], q = pts[(i + 1) % n];
        nx += (p.y - q.y) * (p.z + q.z);
        ny += (p.z - q.z) * (p.x + q.x);
        nz += (p.x - q.x) * (p.y + q.y);
    }
    double l = sqrt(nx * nx + ny * ny + nz * nz);
    mdl__v3 r = {nx, ny, nz};
    return mdl__scale(r, 1.0 / (l == 0 ? 1 : l));
}

static mdl__v3 mdl__centroid_of(const mdl__v3 *pts, int n) {
    mdl__v3 c = {0, 0, 0};
    for (int i = 0; i < n; i++) c = mdl__add(c, pts[i]);
    c.x /= n; c.y /= n; c.z /= n;
    return c;
}

static mdl__v3 mdl__face_normal(mdl__ctx *x, const mdl__shape *s, const mdl__face *f) {
    mdl__v3 *pts = (mdl__v3 *)mdl__alloc(x->a, sizeof(mdl__v3) * (size_t)f->n);
    for (int i = 0; i < f->n; i++) pts[i] = s->v[f->c[i]];
    return mdl__normal_of(pts, f->n);
}

static mdl__v3 mdl__face_centroid(const mdl__shape *s, const mdl__face *f) {
    mdl__v3 c = {0, 0, 0};
    for (int i = 0; i < f->n; i++) c = mdl__add(c, s->v[f->c[i]]);
    c.x /= f->n; c.y /= f->n; c.z /= f->n;
    return c;
}

static mdl__labels mdl__shared(mdl__ctx *x, mdl__face **faces, int n) {
    /* The labels every one of faces has. */
    mdl__labels r = {0, 0};
    if (n == 0) return r;
    r.items = (const char **)mdl__alloc(x->a, sizeof(char *) * (size_t)(faces[0]->labels.n + 1));
    for (int k = 0; k < faces[0]->labels.n; k++) {
        const char *l = faces[0]->labels.items[k];
        int everywhere = 1;
        for (int j = 1; j < n && everywhere; j++) {
            int found = 0;
            for (int m = 0; m < faces[j]->labels.n; m++) if (!strcmp(faces[j]->labels.items[m], l)) found = 1;
            everywhere = found;
        }
        if (everywhere) r.items[r.n++] = l;
    }
    return r;
}

static mdl__labels mdl__with_label(mdl__ctx *x, mdl__labels ls, const char *label) {
    for (int k = 0; k < ls.n; k++) if (!strcmp(ls.items[k], label)) return ls;
    mdl__labels r;
    r.items = (const char **)mdl__alloc(x->a, sizeof(char *) * (size_t)(ls.n + 1));
    r.n = 0;
    int placed = 0;
    for (int k = 0; k < ls.n; k++) {
        if (!placed && strcmp(label, ls.items[k]) < 0) { r.items[r.n++] = label; placed = 1; }
        r.items[r.n++] = ls.items[k];
    }
    if (!placed) r.items[r.n++] = label;
    return r;
}

/* Building a shape: the old points, plus new ones in the order they are made. */
typedef struct {
    mdl__ctx *x;
    MDL__VEC(mdl__v3) v;
    MDL__VEC(mdl__face) f;
} mdl__build;

static void mdl__build_from(mdl__build *b, mdl__ctx *x, const mdl__shape *s) {
    memset(b, 0, sizeof *b);
    b->x = x;
    for (int i = 0; i < s->nv; i++) MDL__PUSH(x->a, b->v, s->v[i]);
}

static int mdl__vertex(mdl__build *b, mdl__v3 p) { MDL__PUSH(b->x->a, b->v, p); return b->v.n - 1; }

static void mdl__face_add(mdl__build *b, const int *c, int n, int role, mdl__labels labels, int step) {
    mdl__face f;
    f.c = (int *)mdl__alloc(b->x->a, sizeof(int) * (size_t)n);
    memcpy(f.c, c, sizeof(int) * (size_t)n);
    f.n = n; f.role = role; f.labels = labels; f.step = step;
    MDL__PUSH(b->x->a, b->f, f);
}

static void mdl__keep(mdl__build *b, const mdl__face *f) { MDL__PUSH(b->x->a, b->f, *f); }

static mdl__shape *mdl__prune(mdl__ctx *x, mdl__shape *s) {
    /* Drop the vertices no face uses, keeping the others in order. */
    int *renumber = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)(s->nv + 1));
    for (int v = 0; v < s->nv; v++) renumber[v] = -1;
    for (int i = 0; i < s->nf; i++) for (int k = 0; k < s->f[i].n; k++) renumber[s->f[i].c[k]] = 0;
    int kept = 0;
    for (int v = 0; v < s->nv; v++) if (renumber[v] == 0) renumber[v] = kept++; else renumber[v] = -1;
    if (kept == s->nv) {
        for (int v = 0; v < s->nv; v++) renumber[v] = v;
        return s;
    }
    mdl__v3 *pts = (mdl__v3 *)mdl__alloc(x->a, sizeof(mdl__v3) * (size_t)(kept + 1));
    for (int v = 0; v < s->nv; v++) if (renumber[v] >= 0) pts[renumber[v]] = s->v[v];
    for (int i = 0; i < s->nf; i++) {
        int *c = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)s->f[i].n);
        for (int k = 0; k < s->f[i].n; k++) c[k] = renumber[s->f[i].c[k]];
        s->f[i].c = c;
    }
    s->v = pts; s->nv = kept;
    return s;
}

static mdl__shape *mdl__finish(mdl__build *b, const mdl__shape *old) {
    mdl__shape *s = (mdl__shape *)mdl__alloc(b->x->a, sizeof *s);
    s->v = b->v.items; s->nv = b->v.n;
    s->f = b->f.items; s->nf = b->f.n;
    s->step = old->step + 1;
    return mdl__prune(b->x, s);
}

static void mdl__check(mdl__ctx *x, const mdl__shape *s) {
    /* The invariant (SPEC.md §1). */
    mdl__map edges;
    int total = 0;
    for (int i = 0; i < s->nf; i++) total += s->f[i].n;
    mdl__map_init(&edges, x->a, total);
    char *used = (char *)mdl__zalloc(x->a, (size_t)s->nv + 1);
    for (int i = 0; i < s->nf; i++) {
        const mdl__face *f = &s->f[i];
        if (f->n < 3) mdl__fail(x, MDL_ERR_INTERNAL, "not a valid shape: a face with fewer than 3 corners");
        for (int k = 0; k < f->n; k++) {
            int a = f->c[k], b = f->c[(k + 1) % f->n];
            if (a < 0 || a >= s->nv) mdl__fail(x, MDL_ERR_INTERNAL, "not a valid shape: a corner out of range");
            used[a] = 1;
            if (mdl__map_get(&edges, mdl__key(a, b), 0)) mdl__fail(x, MDL_ERR_INTERNAL, "not a valid shape: an edge is used twice the same way");
            mdl__map_set(&edges, mdl__key(a, b), 1);
        }
    }
    for (int i = 0; i < s->nf; i++)
        for (int k = 0; k < s->f[i].n; k++) {
            int a = s->f[i].c[k], b = s->f[i].c[(k + 1) % s->f[i].n];
            if (!mdl__map_get(&edges, mdl__key(b, a), 0))
                mdl__fail(x, MDL_ERR_INTERNAL, "not a valid shape: edge %d-%d has no neighbour", a, b);
        }
    for (int v = 0; v < s->nv; v++)
        if (!used[v]) mdl__fail(x, MDL_ERR_INTERNAL, "not a valid shape: an unused vertex");
}

/* --- helpers for operators on regions and corners ---------------------------------------- */

typedef struct { int *start; int *list; } mdl__around;   /* vertex -> faces using it, in order */

static mdl__around mdl__faces_around(mdl__ctx *x, const mdl__shape *s) {
    mdl__around r;
    r.start = (int *)mdl__zalloc(x->a, sizeof(int) * (size_t)(s->nv + 2));
    int total = 0;
    for (int i = 0; i < s->nf; i++) total += s->f[i].n;
    r.list = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)(total + 1));
    int *count = (int *)mdl__zalloc(x->a, sizeof(int) * (size_t)(s->nv + 1));
    int *last = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)(s->nv + 1));
    for (int v = 0; v < s->nv; v++) last[v] = -1;
    for (int i = 0; i < s->nf; i++)
        for (int k = 0; k < s->f[i].n; k++) {
            int v = s->f[i].c[k];
            if (last[v] != i) { last[v] = i; count[v]++; }
        }
    for (int v = 0; v < s->nv; v++) r.start[v + 1] = r.start[v] + count[v];
    int *fill = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)(s->nv + 1));
    for (int v = 0; v < s->nv; v++) { fill[v] = r.start[v]; last[v] = -1; }
    for (int i = 0; i < s->nf; i++)
        for (int k = 0; k < s->f[i].n; k++) {
            int v = s->f[i].c[k];
            if (last[v] != i) { last[v] = i; r.list[fill[v]++] = i; }
        }
    return r;
}

static void mdl__owners(mdl__ctx *x, const mdl__shape *s, mdl__map *owner) {
    /* directed edge -> the face that has it */
    int total = 0;
    for (int i = 0; i < s->nf; i++) total += s->f[i].n;
    mdl__map_init(owner, x->a, total);
    for (int i = 0; i < s->nf; i++)
        for (int k = 0; k < s->f[i].n; k++)
            mdl__map_set(owner, mdl__key(s->f[i].c[k], s->f[i].c[(k + 1) % s->f[i].n]), i);
}

static int mdl__twin(const mdl__map *owner, int a, int b) { return mdl__map_get(owner, mdl__key(b, a), -1); }

static int mdl__after(const mdl__face *f, int v) {
    for (int k = 0; k < f->n; k++) if (f->c[k] == v) return f->c[(k + 1) % f->n];
    return -1;
}

typedef struct { int *items; int n; } mdl__ints;

static mdl__ints mdl__ring(mdl__ctx *x, const mdl__shape *s, const mdl__around *ar, const mdl__map *owner, int v) {
    /* The faces around v in order, from the lowest-numbered one (SPEC.md §1). */
    MDL__VEC(int) ring = {0, 0, 0};
    int start = ar->list[ar->start[v]], i = start;
    do {
        if (ring.n > s->nf) mdl__fail(x, MDL_ERR_INTERNAL, "the faces around vertex %d don't close up", v);
        MDL__PUSH(x->a, ring, i);
        i = mdl__map_get(owner, mdl__key(mdl__after(&s->f[i], v), v), -1);
        if (i < 0) mdl__fail(x, MDL_ERR_INTERNAL, "the faces around vertex %d don't close up", v);
    } while (i != start);
    mdl__ints r = {ring.items, ring.n};
    return r;
}

typedef struct { mdl__ints *items; int n; } mdl__fans;

static mdl__fans mdl__picked_fans(mdl__ctx *x, mdl__ints ring, const char *picked) {
    /* The runs of picked faces in a ring (SPEC.md §1). */
    MDL__VEC(mdl__ints) fans = {0, 0, 0};
    int k = -1;
    for (int i = 0; i < ring.n; i++) if (!picked[ring.items[i]]) { k = i; break; }
    if (k < 0) { MDL__PUSH(x->a, fans, ring); }
    else {
        mdl__ints fan = {(int *)mdl__alloc(x->a, sizeof(int) * (size_t)ring.n), 0};
        for (int j = 0; j < ring.n; j++) {
            int f = ring.items[(k + j) % ring.n];
            if (picked[f]) fan.items[fan.n++] = f;
            else if (fan.n) {
                MDL__PUSH(x->a, fans, fan);
                fan.items = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)ring.n); fan.n = 0;
            }
        }
        if (fan.n) MDL__PUSH(x->a, fans, fan);
    }
    mdl__fans r = {fans.items, fans.n};
    return r;
}

static char *mdl__picked_corners(mdl__ctx *x, const mdl__shape *s, const mdl__around *ar, const char *picked) {
    char *corner = (char *)mdl__zalloc(x->a, (size_t)s->nv + 1);
    for (int v = 0; v < s->nv; v++) {
        int all = ar->start[v + 1] > ar->start[v];
        for (int k = ar->start[v]; k < ar->start[v + 1]; k++) if (!picked[ar->list[k]]) all = 0;
        corner[v] = (char)all;
    }
    return corner;
}

static int *mdl__degrees(mdl__ctx *x, const mdl__shape *s) {
    int *degree = (int *)mdl__zalloc(x->a, sizeof(int) * (size_t)(s->nv + 1));
    for (int i = 0; i < s->nf; i++)
        for (int k = 0; k < s->f[i].n; k++) {
            int a = s->f[i].c[k], b = s->f[i].c[(k + 1) % s->f[i].n];
            if (a < b) { degree[a]++; degree[b]++; }
        }
    return degree;
}

static mdl__shape *mdl__moved(mdl__ctx *x, const mdl__shape *s, const mdl__v3 *pts) {
    mdl__shape *r = (mdl__shape *)mdl__alloc(x->a, sizeof *r);
    *r = *s;
    r->v = (mdl__v3 *)mdl__alloc(x->a, sizeof(mdl__v3) * (size_t)(s->nv + 1));
    memcpy(r->v, pts, sizeof(mdl__v3) * (size_t)s->nv);
    r->step = s->step + 1;
    return r;
}

static char *mdl__made_by_last_step(mdl__ctx *x, const mdl__shape *s) {
    char *p = (char *)mdl__alloc(x->a, (size_t)s->nf + 1);
    for (int i = 0; i < s->nf; i++) p[i] = (char)(s->f[i].step == s->step);
    return p;
}

/* --- face operators (SPEC.md §5.1) ------------------------------------------------------------ */

typedef mdl__shape *(*mdl__opfn)(mdl__ctx *, const mdl__shape *, const char *, const double *);

static mdl__shape *mdl__extrude(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    mdl__build b; mdl__build_from(&b, x, s);
    int step = s->step + 1;
    for (int i = 0; i < s->nf; i++) if (!picked[i]) mdl__keep(&b, &s->f[i]);
    for (int i = 0; i < s->nf; i++) {
        if (!picked[i]) continue;
        const mdl__face *f = &s->f[i];
        mdl__v3 out = mdl__scale(mdl__face_normal(x, s, f), num[0]);
        int *top = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)f->n);
        for (int k = 0; k < f->n; k++) top[k] = mdl__vertex(&b, mdl__add(s->v[f->c[k]], out));
        for (int k = 0; k < f->n; k++) {
            int q[4] = {f->c[k], f->c[(k + 1) % f->n], top[(k + 1) % f->n], top[k]};
            mdl__face_add(&b, q, 4, R_SIDE, f->labels, step);
        }
        mdl__face_add(&b, top, f->n, R_TOP, f->labels, step);
    }
    return mdl__finish(&b, s);
}

static mdl__shape *mdl__inset(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    if (!(num[0] < 1)) mdl__fail(x, MDL_ERR_RANGE, "inset: the amount must be less than 1, not %g", num[0]);
    mdl__build b; mdl__build_from(&b, x, s);
    int step = s->step + 1;
    for (int i = 0; i < s->nf; i++) if (!picked[i]) mdl__keep(&b, &s->f[i]);
    for (int i = 0; i < s->nf; i++) {
        if (!picked[i]) continue;
        const mdl__face *f = &s->f[i];
        mdl__v3 middle = mdl__face_centroid(s, f);
        int *inner = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)f->n);
        for (int k = 0; k < f->n; k++) inner[k] = mdl__vertex(&b, mdl__lerp(s->v[f->c[k]], middle, num[0]));
        for (int k = 0; k < f->n; k++) {
            int q[4] = {f->c[k], f->c[(k + 1) % f->n], inner[(k + 1) % f->n], inner[k]};
            mdl__face_add(&b, q, 4, R_RIM, f->labels, step);
        }
        mdl__face_add(&b, inner, f->n, R_INNER, f->labels, step);
    }
    return mdl__finish(&b, s);
}

static mdl__shape *mdl__kis(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    mdl__build b; mdl__build_from(&b, x, s);
    int step = s->step + 1;
    for (int i = 0; i < s->nf; i++) if (!picked[i]) mdl__keep(&b, &s->f[i]);
    for (int i = 0; i < s->nf; i++) {
        if (!picked[i]) continue;
        const mdl__face *f = &s->f[i];
        int apex = mdl__vertex(&b, mdl__add(mdl__face_centroid(s, f), mdl__scale(mdl__face_normal(x, s, f), num[0])));
        for (int k = 0; k < f->n; k++) {
            int t[3] = {f->c[k], f->c[(k + 1) % f->n], apex};
            mdl__face_add(&b, t, 3, R_TENT, f->labels, step);
        }
    }
    return mdl__finish(&b, s);
}

static int mdl__third(mdl__build *b, mdl__map *thirds, const mdl__shape *s, int p, int q) {
    /* The point a third of the way from p to q, made the first time it is asked for. */
    int t = mdl__map_get(thirds, mdl__key(p, q), -1);
    if (t < 0) {
        t = mdl__vertex(b, mdl__lerp(s->v[p], s->v[q], 1.0 / 3));
        mdl__map_set(thirds, mdl__key(p, q), t);
    }
    return t;
}

static mdl__shape *mdl__gyro(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    (void)num;
    mdl__build b; mdl__build_from(&b, x, s);
    int step = s->step + 1;
    mdl__map thirds;
    mdl__map_init(&thirds, x->a, 64);
#define THIRD(p, q) mdl__third(&b, &thirds, s, (p), (q))
    MDL__VEC(mdl__face) made = {0, 0, 0};
    for (int i = 0; i < s->nf; i++) {
        if (!picked[i]) continue;
        const mdl__face *f = &s->f[i];
        int c = mdl__vertex(&b, mdl__face_centroid(s, f));
        for (int k = 0; k < f->n; k++) {
            int v = f->c[k], before = f->c[(k + f->n - 1) % f->n], after = f->c[(k + 1) % f->n];
            int p[5];
            p[0] = THIRD(before, v);
            p[1] = THIRD(v, before);
            p[2] = v;
            p[3] = THIRD(v, after);
            p[4] = c;
            mdl__face nf;
            nf.c = (int *)mdl__alloc(x->a, sizeof p);
            memcpy(nf.c, p, sizeof p);
            nf.n = 5; nf.role = R_FACE; nf.labels = f->labels; nf.step = step;
            MDL__PUSH(x->a, made, nf);
        }
    }
#undef THIRD
    for (int i = 0; i < s->nf; i++) {
        if (picked[i]) continue;
        const mdl__face *f = &s->f[i];
        MDL__VEC(int) c = {0, 0, 0};
        for (int k = 0; k < f->n; k++) {
            int a = f->c[k], bb = f->c[(k + 1) % f->n];
            MDL__PUSH(x->a, c, a);
            int t = mdl__map_get(&thirds, mdl__key(a, bb), -1);
            if (t >= 0) {
                MDL__PUSH(x->a, c, t);
                MDL__PUSH(x->a, c, mdl__map_get(&thirds, mdl__key(bb, a), -1));
            }
        }
        mdl__face g = *f;
        g.c = c.items; g.n = c.n;
        mdl__keep(&b, &g);
    }
    for (int i = 0; i < made.n; i++) mdl__keep(&b, &made.items[i]);
    return mdl__finish(&b, s);
}

/* --- region operators ------------------------------------------------------------------------------- */

static mdl__shape *mdl__lift(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    int step = s->step + 1;
    mdl__map owner, copy;
    mdl__owners(x, s, &owner);
    mdl__map_init(&copy, x->a, 64);
    mdl__around ar = mdl__faces_around(x, s);
    mdl__build b; mdl__build_from(&b, x, s);
    for (int v = 0; v < s->nv; v++) {
        int any = 0;
        for (int k = ar.start[v]; k < ar.start[v + 1]; k++) if (picked[ar.list[k]]) any = 1;
        if (!any) continue;
        mdl__fans fans = mdl__picked_fans(x, mdl__ring(x, s, &ar, &owner, v), picked);
        for (int j = 0; j < fans.n; j++) {
            mdl__v3 m = {0, 0, 0};
            for (int k = 0; k < fans.items[j].n; k++) m = mdl__add(m, mdl__face_normal(x, s, &s->f[fans.items[j].items[k]]));
            int c = mdl__vertex(&b, mdl__add(s->v[v], mdl__scale(mdl__unit(m), num[0])));
            for (int k = 0; k < fans.items[j].n; k++) mdl__map_set(&copy, mdl__key(v, fans.items[j].items[k]), c);
        }
    }
    for (int i = 0; i < s->nf; i++) if (!picked[i]) mdl__keep(&b, &s->f[i]);
    for (int i = 0; i < s->nf; i++) {
        if (!picked[i]) continue;
        const mdl__face *f = &s->f[i];
        for (int k = 0; k < f->n; k++) {
            int a = f->c[k], bb = f->c[(k + 1) % f->n];
            if (picked[mdl__twin(&owner, a, bb)]) continue;
            int q[4] = {a, bb, mdl__map_get(&copy, mdl__key(bb, i), -1), mdl__map_get(&copy, mdl__key(a, i), -1)};
            mdl__face_add(&b, q, 4, R_SIDE, f->labels, step);
        }
        int *top = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)f->n);
        for (int k = 0; k < f->n; k++) top[k] = mdl__map_get(&copy, mdl__key(f->c[k], i), -1);
        mdl__face_add(&b, top, f->n, R_TOP, f->labels, step);
    }
    return mdl__finish(&b, s);
}

/* Every region of picked faces (maximal sets connected through shared corners) has its
   corners moved by to(c, p, params): c is the average of the region's corners. */
typedef mdl__v3 (*mdl__region_fn)(mdl__v3 c, mdl__v3 p, const double *params);

static mdl__shape *mdl__each_region(mdl__ctx *x, const mdl__shape *s, const char *picked, mdl__region_fn to, const double *params) {
    mdl__around ar = mdl__faces_around(x, s);
    mdl__v3 *pts = (mdl__v3 *)mdl__alloc(x->a, sizeof(mdl__v3) * (size_t)(s->nv + 1));
    memcpy(pts, s->v, sizeof(mdl__v3) * (size_t)s->nv);
    char *seen = (char *)mdl__zalloc(x->a, (size_t)s->nf + 1);
    char *in = (char *)mdl__zalloc(x->a, (size_t)s->nv + 1);
    int *queue = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)(s->nf + 1));
    for (int i = 0; i < s->nf; i++) {
        if (!picked[i] || seen[i]) continue;
        /* the piece of picked faces connected to face i through shared corners */
        memset(in, 0, (size_t)s->nv);
        int head = 0, tail = 0;
        queue[tail++] = i; seen[i] = 1;
        while (head < tail) {
            const mdl__face *f = &s->f[queue[head++]];
            for (int k = 0; k < f->n; k++) {
                int v = f->c[k];
                in[v] = 1;
                for (int m = ar.start[v]; m < ar.start[v + 1]; m++) {
                    int g = ar.list[m];
                    if (picked[g] && !seen[g]) { seen[g] = 1; queue[tail++] = g; }
                }
            }
        }
        mdl__v3 c = {0, 0, 0};
        int count = 0;
        for (int v = 0; v < s->nv; v++) if (in[v]) { c = mdl__add(c, s->v[v]); count++; }
        c.x /= count; c.y /= count; c.z /= count;
        for (int v = 0; v < s->nv; v++) if (in[v]) pts[v] = to(c, s->v[v], params);
    }
    return mdl__moved(x, s, pts);
}

static mdl__v3 mdl__scale_to(mdl__v3 c, mdl__v3 p, const double *num) { return mdl__lerp(c, p, num[0]); }

static mdl__shape *mdl__scale_region(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    /* Each region scaled about its centre. */
    return mdl__each_region(x, s, picked, mdl__scale_to, num);
}

static mdl__v3 mdl__turn_to(mdl__v3 c, mdl__v3 p, const double *t) {
    /* t: cos and sin about x, then y, then z */
    double x0 = p.x - c.x, y0 = p.y - c.y, z0 = p.z - c.z;
    double x1 = x0, y1 = y0 * t[0] - z0 * t[1], z1 = y0 * t[1] + z0 * t[0];
    double x2 = x1 * t[2] + z1 * t[3], y2 = y1, z2 = -(x1 * t[3]) + z1 * t[2];
    mdl__v3 d = {x2 * t[4] - y2 * t[5], x2 * t[5] + y2 * t[4], z2};
    return mdl__add(c, d);
}

static mdl__shape *mdl__turn(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    /* Each region turned about its centre: by num[2] degrees about the x axis, then
       num[1] about the y axis, then num[0] about the vertical. */
    double t[6];
    for (int k = 0; k < 3; k++) {
        double a = num[2 - k] * M_PI / 180;
        t[2 * k] = cos(a); t[2 * k + 1] = sin(a);
    }
    return mdl__each_region(x, s, picked, mdl__turn_to, t);
}

static mdl__v3 mdl__stretch_to(mdl__v3 c, mdl__v3 p, const double *num) {
    mdl__v3 d = {(p.x - c.x) * num[0], (p.y - c.y) * num[1], (p.z - c.z) * num[2]};
    return mdl__add(c, d);
}

static mdl__shape *mdl__stretch(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    /* Each region stretched about its centre, along each axis. */
    return mdl__each_region(x, s, picked, mdl__stretch_to, num);
}

static mdl__shape *mdl__move_by(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    /* The picked faces' corners moved by (num[0], num[1], num[2]). */
    char *touched = (char *)mdl__zalloc(x->a, (size_t)s->nv + 1);
    for (int i = 0; i < s->nf; i++) if (picked[i]) for (int k = 0; k < s->f[i].n; k++) touched[s->f[i].c[k]] = 1;
    mdl__v3 by = {num[0], num[1], num[2]};
    mdl__v3 *pts = (mdl__v3 *)mdl__alloc(x->a, sizeof(mdl__v3) * (size_t)(s->nv + 1));
    for (int v = 0; v < s->nv; v++) pts[v] = touched[v] ? mdl__add(s->v[v], by) : s->v[v];
    return mdl__moved(x, s, pts);
}

static mdl__shape *mdl__push(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    mdl__v3 *sums = (mdl__v3 *)mdl__zalloc(x->a, sizeof(mdl__v3) * (size_t)(s->nv + 1));
    char *touched = (char *)mdl__zalloc(x->a, (size_t)s->nv + 1);
    for (int i = 0; i < s->nf; i++) {
        if (!picked[i]) continue;
        mdl__v3 n = mdl__face_normal(x, s, &s->f[i]);
        for (int k = 0; k < s->f[i].n; k++) { int v = s->f[i].c[k]; sums[v] = mdl__add(sums[v], n); touched[v] = 1; }
    }
    mdl__v3 *pts = (mdl__v3 *)mdl__alloc(x->a, sizeof(mdl__v3) * (size_t)(s->nv + 1));
    for (int v = 0; v < s->nv; v++)
        pts[v] = touched[v] ? mdl__add(s->v[v], mdl__scale(mdl__unit(sums[v]), num[0])) : s->v[v];
    return mdl__moved(x, s, pts);
}

static mdl__shape *mdl__dual(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    (void)num;
    int step = s->step + 1;
    mdl__map owner;
    mdl__owners(x, s, &owner);
    mdl__around ar = mdl__faces_around(x, s);
    mdl__build b; mdl__build_from(&b, x, s);
    int *centre = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)(s->nf + 1));
    for (int i = 0; i < s->nf; i++) centre[i] = picked[i] ? mdl__vertex(&b, mdl__face_centroid(s, &s->f[i])) : -1;
    for (int i = 0; i < s->nf; i++) if (!picked[i]) mdl__keep(&b, &s->f[i]);
    for (int i = 0; i < s->nf; i++) {
        if (!picked[i]) continue;
        const mdl__face *f = &s->f[i];
        for (int k = 0; k < f->n; k++) {
            int a = f->c[k], bb = f->c[(k + 1) % f->n];
            if (picked[mdl__twin(&owner, a, bb)]) continue;
            int t[3] = {a, bb, centre[i]};
            mdl__face_add(&b, t, 3, R_EDGE, f->labels, step);
        }
    }
    for (int v = 0; v < s->nv; v++) {
        int any = 0;
        for (int k = ar.start[v]; k < ar.start[v + 1]; k++) if (picked[ar.list[k]]) any = 1;
        if (!any) continue;
        mdl__ints ring = mdl__ring(x, s, &ar, &owner, v);
        int full = 1;
        for (int k = 0; k < ring.n; k++) if (!picked[ring.items[k]]) full = 0;
        mdl__fans fans = mdl__picked_fans(x, ring, picked);
        for (int j = 0; j < fans.n; j++) {
            mdl__ints fan = fans.items[j];
            if (fan.n < (full ? 3 : 2)) continue;
            int *c = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)(fan.n + 1));
            mdl__face **from = (mdl__face **)mdl__alloc(x->a, sizeof(mdl__face *) * (size_t)fan.n);
            int n = 0;
            if (!full) c[n++] = v;
            /* the ring runs clockwise seen from outside; faces run counterclockwise */
            for (int k = fan.n - 1; k >= 0; k--) c[n++] = centre[fan.items[k]];
            for (int k = 0; k < fan.n; k++) from[k] = &s->f[fan.items[k]];
            mdl__face_add(&b, c, n, R_VERTEX, mdl__shared(x, from, fan.n), step);
        }
    }
    return mdl__finish(&b, s);
}

static mdl__shape *mdl__flip(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    (void)num;
    int step = s->step + 1;
    mdl__face *faces = (mdl__face *)mdl__alloc(x->a, sizeof(mdl__face) * (size_t)(s->nf + 1));
    memcpy(faces, s->f, sizeof(mdl__face) * (size_t)s->nf);
    mdl__map owner, edges;
    mdl__owners(x, s, &owner);
    mdl__map_init(&edges, x->a, owner.count);
#define EDGE(a, b) mdl__key((a) < (b) ? (a) : (b), (a) < (b) ? (b) : (a))
    for (int i = 0; i < s->nf; i++)
        for (int k = 0; k < s->f[i].n; k++) {
            int a = s->f[i].c[k], bb = s->f[i].c[(k + 1) % s->f[i].n];
            mdl__map_set(&edges, EDGE(a, bb), 1);
        }
    int *degree = mdl__degrees(x, s);
    char *done = (char *)mdl__zalloc(x->a, (size_t)s->nf + 1);
#define FREE(i) (picked[i] && faces[i].n == 3 && !done[i])
    for (int i = 0; i < s->nf; i++) {
        if (!FREE(i)) continue;
        for (int k = 0; k < 3; k++) {
            int a = faces[i].c[k], bb = faces[i].c[(k + 1) % 3];
            int j = mdl__map_get(&owner, mdl__key(bb, a), -1);
            if (j < 0 || !FREE(j) || degree[a] <= 3 || degree[bb] <= 3) continue;
            int c = faces[i].c[(k + 2) % 3], d = -1;
            for (int m = 0; m < 3; m++) if (faces[j].c[m] != a && faces[j].c[m] != bb) d = faces[j].c[m];
            if (c == d || mdl__map_get(&edges, EDGE(c, d), 0)) continue;
            mdl__face *pair[2] = {&faces[i], &faces[j]};
            mdl__labels labels = mdl__shared(x, pair, 2);
            int *ci = (int *)mdl__alloc(x->a, sizeof(int) * 3), *cj = (int *)mdl__alloc(x->a, sizeof(int) * 3);
            ci[0] = c; ci[1] = a; ci[2] = d;
            cj[0] = d; cj[1] = bb; cj[2] = c;
            faces[i].c = ci; faces[i].role = R_FLIP; faces[i].labels = labels; faces[i].step = step;
            faces[j].c = cj; faces[j].role = R_FLIP; faces[j].labels = labels; faces[j].step = step;
            for (int m = 0; m < 3; m++) {
                mdl__map_set(&owner, mdl__key(ci[m], ci[(m + 1) % 3]), i);
                mdl__map_set(&owner, mdl__key(cj[m], cj[(m + 1) % 3]), j);
            }
            mdl__map_set(&edges, EDGE(a, bb), 0);
            mdl__map_set(&edges, EDGE(c, d), 1);
            degree[a]--; degree[bb]--; degree[c]++; degree[d]++;
            done[i] = done[j] = 1;
            break;
        }
    }
#undef FREE
#undef EDGE
    for (int i = 0; i < s->nf; i++) if (picked[i] && !done[i]) x->skips++;
    mdl__shape *r = (mdl__shape *)mdl__alloc(x->a, sizeof *r);
    r->v = s->v; r->nv = s->nv; r->f = faces; r->nf = s->nf; r->step = step;
    return r;
}

/* --- corner operators ---------------------------------------------------------------------------------- */

static mdl__shape *mdl__cut_corners(mdl__ctx *x, const mdl__shape *s, const char *picked, double fraction) {
    int merge = fraction == 0.5;
    int step = s->step + 1;
    mdl__around ar = mdl__faces_around(x, s);
    char *corner = mdl__picked_corners(x, s, &ar, picked);
    mdl__build b; mdl__build_from(&b, x, s);
    mdl__map cuts;
    mdl__map_init(&cuts, x->a, 64);
    /* for each corner, the links b -> a its cut points make, as pairs */
    MDL__VEC(int) *links = (void *)mdl__zalloc(x->a, sizeof *links * (size_t)(s->nv + 1));
    for (int i = 0; i < s->nf; i++) {
        const mdl__face *f = &s->f[i];
        MDL__VEC(int) c = {0, 0, 0};
        int changed = 0;
        for (int k = 0; k < f->n; k++) {
            int w = f->c[k];
            if (!corner[w]) { MDL__PUSH(x->a, c, w); continue; }
            changed = 1;
            int ends[2] = {f->c[(k + f->n - 1) % f->n], f->c[(k + 1) % f->n]}, cut[2];
            for (int e = 0; e < 2; e++) {
                int u = ends[e];
                uint64_t key = (merge && corner[u]) ? mdl__key(w < u ? w : u, w < u ? u : w) : mdl__key(w, u);
                cut[e] = mdl__map_get(&cuts, key, -1);
                if (cut[e] < 0) {
                    cut[e] = mdl__vertex(&b, mdl__lerp(s->v[w], s->v[u], fraction));
                    mdl__map_set(&cuts, key, cut[e]);
                }
            }
            MDL__PUSH(x->a, links[w], cut[1]);
            MDL__PUSH(x->a, links[w], cut[0]);
            MDL__PUSH(x->a, c, cut[0]);
            MDL__PUSH(x->a, c, cut[1]);
        }
        /* neighbouring repeats removed, all the way round */
        MDL__VEC(int) d = {0, 0, 0};
        for (int k = 0; k < c.n; k++) if (c.items[k] != c.items[(k + 1) % c.n]) MDL__PUSH(x->a, d, c.items[k]);
        mdl__face g = *f;
        g.c = d.items; g.n = d.n;
        if (changed) g.step = step;
        mdl__keep(&b, &g);
    }
    for (int w = 0; w < s->nv; w++) {
        /* A corner where only two faces meet would leave a two-sided face; its two
           faces share that edge instead. */
        if (!corner[w] || ar.start[w + 1] - ar.start[w] < 3) continue;
        int pairs = links[w].n / 2, start = -1;
        for (int k = 0; k < pairs; k++) if (start < 0 || links[w].items[2 * k] < start) start = links[w].items[2 * k];
        MDL__VEC(int) c = {0, 0, 0};
        int p = start;
        do {
            if (c.n > pairs) mdl__fail(x, MDL_ERR_INTERNAL, "the cuts around vertex %d don't close up", w);
            MDL__PUSH(x->a, c, p);
            int q = -1;
            for (int k = 0; k < pairs; k++) if (links[w].items[2 * k] == p) q = links[w].items[2 * k + 1];
            if (q < 0) mdl__fail(x, MDL_ERR_INTERNAL, "the cuts around vertex %d don't close up", w);
            p = q;
        } while (p != start);
        int n = ar.start[w + 1] - ar.start[w];
        mdl__face **from = (mdl__face **)mdl__alloc(x->a, sizeof(mdl__face *) * (size_t)n);
        for (int k = 0; k < n; k++) from[k] = &s->f[ar.list[ar.start[w] + k]];
        mdl__face_add(&b, c.items, c.n, R_VERTEX, mdl__shared(x, from, n), step);
    }
    return mdl__finish(&b, s);
}

static mdl__shape *mdl__truncate(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    if (!(num[0] > 0 && num[0] <= 0.5))
        mdl__fail(x, MDL_ERR_RANGE, "truncate: the fraction must be above 0 and at most 0.5, not %g", num[0]);
    return mdl__cut_corners(x, s, picked, num[0]);
}

static mdl__shape *mdl__ambo(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    (void)num;
    return mdl__cut_corners(x, s, picked, 0.5);
}

static int *mdl__merged_polygon(mdl__ctx *x, const mdl__shape *s, int v, const int *fs, int nfs, int *count) {
    /* The faces around v joined into one, without v; NULL if not a simple polygon. */
    int **paths = (int **)mdl__alloc(x->a, sizeof(int *) * (size_t)nfs);
    int *lens = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)nfs);
    for (int j = 0; j < nfs; j++) {
        const mdl__face *f = &s->f[fs[j]];
        int k = 0;
        while (f->c[k] != v) k++;
        paths[j] = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)f->n);
        lens[j] = f->n - 1;
        for (int m = 1; m < f->n; m++) paths[j][m - 1] = f->c[(k + m) % f->n];
    }
    int start = -1, first = -1;
    for (int j = 0; j < nfs; j++) if (start < 0 || paths[j][0] < start) { start = paths[j][0]; first = j; }
    MDL__VEC(int) poly = {0, 0, 0};
    int j = first, steps = 0;
    for (;;) {
        if (steps > nfs) return NULL;
        for (int m = 0; m < lens[j] - 1; m++) MDL__PUSH(x->a, poly, paths[j][m]);
        int end = paths[j][lens[j] - 1];
        if (end == start) break;
        int next = -1;
        for (int m = 0; m < nfs; m++) if (paths[m][0] == end) next = m;
        if (next < 0) return NULL;
        j = next; steps++;
    }
    if (poly.n < 3) return NULL;
    for (int a = 0; a < poly.n; a++)
        for (int c = a + 1; c < poly.n; c++) if (poly.items[a] == poly.items[c]) return NULL;
    *count = poly.n;
    return poly.items;
}

static mdl__shape *mdl__merge(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    (void)num;
    int step = s->step + 1;
    mdl__around ar = mdl__faces_around(x, s);
    char *corner = mdl__picked_corners(x, s, &ar, picked);
    int *degree = mdl__degrees(x, s);
    char *gone = (char *)mdl__zalloc(x->a, (size_t)s->nf + 1);
    char *blocked = (char *)mdl__zalloc(x->a, (size_t)s->nv + 1);
    int *nb = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)(s->nv + 1));
    mdl__build b; mdl__build_from(&b, x, s);
    MDL__VEC(mdl__face) merged = {0, 0, 0};
    int corners = 0;
    for (int v = s->nv - 1; v >= 0; v--) {
        if (!corner[v]) continue;
        corners++;
        if (blocked[v]) continue;
        const int *fs = ar.list + ar.start[v];
        int nfs = ar.start[v + 1] - ar.start[v];
        /* the vertices joined to v, in order of first appearance */
        int nn = 0;
        for (int j = 0; j < nfs; j++) {
            const mdl__face *f = &s->f[fs[j]];
            for (int k = 0; k < f->n; k++) {
                int a = f->c[k], bb = f->c[(k + 1) % f->n], u;
                if (a == v) u = bb; else if (bb == v) u = a; else continue;
                int seen = 0;
                for (int m = 0; m < nn; m++) if (nb[m] == u) seen = 1;
                if (!seen) nb[nn++] = u;
            }
        }
        int ok = 1;
        for (int m = 0; m < nn; m++) if (degree[nb[m]] <= 3) ok = 0;
        if (!ok) continue;
        int count = 0;
        int *poly = mdl__merged_polygon(x, s, v, fs, nfs, &count);
        if (!poly) continue;
        mdl__face **from = (mdl__face **)mdl__alloc(x->a, sizeof(mdl__face *) * (size_t)nfs);
        for (int j = 0; j < nfs; j++) { gone[fs[j]] = 1; from[j] = &s->f[fs[j]]; }
        for (int k = 0; k < count; k++) blocked[poly[k]] = 1;
        for (int m = 0; m < nn; m++) degree[nb[m]]--;
        mdl__face g;
        g.c = poly; g.n = count; g.role = R_MERGED; g.labels = mdl__shared(x, from, nfs); g.step = step;
        MDL__PUSH(x->a, merged, g);
    }
    x->skips += corners - merged.n;
    for (int i = 0; i < s->nf; i++) if (!gone[i]) mdl__keep(&b, &s->f[i]);
    for (int i = 0; i < merged.n; i++) mdl__keep(&b, &merged.items[i]);
    return mdl__finish(&b, s);
}

/* --- the rest ------------------------------------------------------------------------------------------- */

static mdl__shape *mdl__bridge(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    (void)num;
    int chosen[2], count = 0;
    for (int i = 0; i < s->nf; i++) if (picked[i]) { if (count < 2) chosen[count] = i; count++; }
    if (count != 2 || s->f[chosen[0]].n != s->f[chosen[1]].n)
        mdl__fail(x, MDL_ERR_PRECONDITION, "bridge needs exactly two faces with the same number of sides, but picked %d", count);
    const mdl__face *fa = &s->f[chosen[0]], *fb = &s->f[chosen[1]];
    int n = fa->n;
    for (int k = 0; k < n; k++)
        for (int m = 0; m < n; m++)
            if (fa->c[k] == fb->c[m])
                mdl__fail(x, MDL_ERR_PRECONDITION, "bridge: the two faces touch, so a tube between them would cross itself");
    mdl__map owner;
    mdl__owners(x, s, &owner);
    for (int k = 0; k < n; k++)
        for (int m = 0; m < n; m++)
            if (mdl__map_get(&owner, mdl__key(fa->c[k], fb->c[m]), -1) >= 0)
                mdl__fail(x, MDL_ERR_PRECONDITION, "bridge: an edge already joins the two faces, so a tube would run along it");
    int *a = fa->c, *bv = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)n);
    for (int k = 0; k < n; k++) bv[k] = fb->c[n - 1 - k];
    int turn = 0;
    double best = 0;
    for (int t = 0; t < n; t++) {
        double total = 0;
        for (int i = 0; i < n; i++) {
            mdl__v3 p = s->v[a[i]], q = s->v[bv[(i + t) % n]];
            total += (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z);
        }
        if (t == 0 || total < best) { best = total; turn = t; }
    }
    mdl__face *pair[2] = {(mdl__face *)fa, (mdl__face *)fb};
    mdl__labels labels = mdl__shared(x, pair, 2);
    mdl__build b; mdl__build_from(&b, x, s);
    for (int i = 0; i < s->nf; i++) if (!picked[i]) mdl__keep(&b, &s->f[i]);
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        int q[4] = {a[i], a[j], bv[(j + turn) % n], bv[(i + turn) % n]};
        mdl__face_add(&b, q, 4, R_TUBE, labels, s->step + 1);
    }
    return mdl__finish(&b, s);
}

static mdl__shape *mdl__reflect(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    (void)num;
    for (int i = 0; i < s->nf; i++)
        if (!picked[i]) mdl__fail(x, MDL_ERR_PRECONDITION, "reflect acts on the whole shape; leave out the selector");
    mdl__shape *r = (mdl__shape *)mdl__alloc(x->a, sizeof *r);
    r->nv = s->nv; r->nf = s->nf; r->step = s->step + 1;
    r->v = (mdl__v3 *)mdl__alloc(x->a, sizeof(mdl__v3) * (size_t)(s->nv + 1));
    for (int v = 0; v < s->nv; v++) { r->v[v] = s->v[v]; r->v[v].x = -s->v[v].x; }
    r->f = (mdl__face *)mdl__alloc(x->a, sizeof(mdl__face) * (size_t)(s->nf + 1));
    for (int i = 0; i < s->nf; i++) {
        r->f[i] = s->f[i];
        r->f[i].c = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)s->f[i].n);
        for (int k = 0; k < s->f[i].n; k++) r->f[i].c[k] = s->f[i].c[s->f[i].n - 1 - k];
    }
    return r;
}

/* --- symmetry ------------------------------------------------------------------------------------------- */

static mdl__shape *mdl__mirror(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    /* The shape joined to its reflection across the picked faces, the seam, which are
       removed: build half of something, get the whole. The seam's corners are shared by
       both halves; every other vertex gets a reflected copy, and every unpicked face a
       reflected copy with its corners reversed. */
    (void)num;
    int count = 0;
    for (int i = 0; i < s->nf; i++) count += picked[i] ? 1 : 0;
    if (count == 0 || count == s->nf)
        mdl__fail(x, MDL_ERR_PRECONDITION, "mirror needs a seam: some of the faces, but not all of them");
    mdl__map owner;
    mdl__owners(x, s, &owner);
    mdl__around ar = mdl__faces_around(x, s);
    char *seam = (char *)mdl__zalloc(x->a, (size_t)s->nv + 1);
    for (int i = 0; i < s->nf; i++) if (picked[i]) for (int k = 0; k < s->f[i].n; k++) seam[s->f[i].c[k]] = 1;
    for (int v = 0; v < s->nv; v++) {
        if (!seam[v]) continue;
        mdl__ints ring = mdl__ring(x, s, &ar, &owner, v);
        int unpicked = 0;
        for (int k = 0; k < ring.n; k++) if (!picked[ring.items[k]]) unpicked = 1;
        if (unpicked && mdl__picked_fans(x, ring, picked).n > 1)
            mdl__fail(x, MDL_ERR_PRECONDITION, "mirror: the seam meets itself at corner %d, so the halves would be joined there twice", v);
    }
    for (int i = 0; i < s->nf; i++) {
        if (picked[i]) continue;
        for (int k = 0; k < s->f[i].n; k++) {
            int a = s->f[i].c[k], bb = s->f[i].c[(k + 1) % s->f[i].n];
            if (seam[a] && seam[bb] && !picked[mdl__twin(&owner, a, bb)])
                mdl__fail(x, MDL_ERR_PRECONDITION, "mirror: edge %d-%d joins two corners of the seam outside it, so both halves "
                                                   "would have it. Often this is a hidden wall between blocks extruded side by "
                                                   "side; lift pushes them out as one block", a, bb);
        }
    }
    /* The mirror's plane: through the average of the seam's corners, across the sum of its normals. */
    mdl__v3 c = {0, 0, 0}, n = {0, 0, 0};
    int corners = 0;
    for (int v = 0; v < s->nv; v++) if (seam[v]) { c = mdl__add(c, s->v[v]); corners++; }
    c.x /= corners; c.y /= corners; c.z /= corners;
    for (int i = 0; i < s->nf; i++) if (picked[i]) n = mdl__add(n, mdl__face_normal(x, s, &s->f[i]));
    n = mdl__unit(n);
    mdl__build b; mdl__build_from(&b, x, s);
    int *copy = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)(s->nv + 1));
    for (int v = 0; v < s->nv; v++) {
        if (seam[v]) { copy[v] = v; continue; }
        mdl__v3 p = s->v[v], d = {p.x - c.x, p.y - c.y, p.z - c.z};
        copy[v] = mdl__vertex(&b, mdl__add(p, mdl__scale(n, -2 * mdl__dot(d, n))));
    }
    for (int i = 0; i < s->nf; i++) if (!picked[i]) mdl__keep(&b, &s->f[i]);
    for (int i = 0; i < s->nf; i++) {
        if (picked[i]) continue;
        const mdl__face *f = &s->f[i];
        int *q = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)f->n);
        q[0] = copy[f->c[0]];
        for (int k = 1; k < f->n; k++) q[k] = copy[f->c[f->n - k]];
        mdl__face_add(&b, q, f->n, f->role, f->labels, s->step + 1);
    }
    return mdl__finish(&b, s);
}

/* --- copies ------------------------------------------------------------------------------------------- */

typedef mdl__v3 (*mdl__place_fn)(int k, mdl__v3 p, const double *num);

static mdl__shape *mdl__copies(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num, const char *what, mdl__place_fn place) {
    /* The shape and n - 1 copies of it, copy k with every point at place(k, p). Copies
       keep their roles and labels and are made by this step. */
    for (int i = 0; i < s->nf; i++)
        if (!picked[i]) mdl__fail(x, MDL_ERR_PRECONDITION, "%s copies the whole shape; leave out the selector", what);
    double n = num[0];
    if (!(n >= 1) || n != floor(n))
        mdl__fail(x, MDL_ERR_RANGE, "%s needs a whole number of copies, at least 1, not %g", what, n);
    if (n * s->nf > MDL_MAX_FACES)     /* checked before building them */
        mdl__fail(x, MDL_ERR_LIMIT, "%s makes %.0f faces, more than the limit of %d", what, n * s->nf, MDL_MAX_FACES);
    int count = (int)n;
    mdl__build b; mdl__build_from(&b, x, s);
    for (int k = 1; k < count; k++)
        for (int v = 0; v < s->nv; v++) mdl__vertex(&b, place(k, s->v[v], num));
    for (int i = 0; i < s->nf; i++) mdl__keep(&b, &s->f[i]);
    for (int k = 1; k < count; k++)
        for (int i = 0; i < s->nf; i++) {
            const mdl__face *f = &s->f[i];
            int *q = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)f->n);
            for (int m = 0; m < f->n; m++) q[m] = f->c[m] + k * s->nv;
            mdl__face_add(&b, q, f->n, f->role, f->labels, s->step + 1);
        }
    return mdl__finish(&b, s);
}

static mdl__v3 mdl__array_place(int k, mdl__v3 p, const double *num) {
    mdl__v3 r = {p.x + k * num[1], p.y + k * num[2], p.z + k * num[3]};
    return r;
}

static mdl__shape *mdl__array(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    /* n copies in a row, each moved (x, y, z) from the one before. */
    return mdl__copies(x, s, picked, num, "array", mdl__array_place);
}

static mdl__v3 mdl__spin_place(int k, mdl__v3 p, const double *num) {
    double a = 2 * M_PI * k / num[0];
    mdl__v3 r = {p.x * cos(a) - p.y * sin(a), p.x * sin(a) + p.y * cos(a), p.z};
    return r;
}

static mdl__shape *mdl__spin(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) {
    /* n copies turned evenly about the vertical axis through the origin. */
    return mdl__copies(x, s, picked, num, "spin", mdl__spin_place);
}

/* Conway's other letters are his identities: the rightmost step acts on the
   picked faces, each step to its left on the faces the one before made. */
static mdl__shape *mdl__on_made(mdl__ctx *x, const mdl__shape *s, const char *picked, const mdl__opfn *steps, int n) {
    static const double kis_height = 0.0, truncate_fraction = 0.3;
    for (int k = n - 1; k >= 0; k--) {
        const double *num = steps[k] == mdl__kis ? &kis_height : &truncate_fraction;
        s = steps[k](x, s, picked, num);
        picked = mdl__made_by_last_step(x, s);
    }
    return (mdl__shape *)s;
}

#define MDL__DERIVED(name, ...) \
    static mdl__shape *name(mdl__ctx *x, const mdl__shape *s, const char *picked, const double *num) { \
        static const mdl__opfn steps[] = {__VA_ARGS__}; (void)num; \
        return mdl__on_made(x, s, picked, steps, (int)(sizeof steps / sizeof *steps)); }
MDL__DERIVED(mdl__needle, mdl__kis, mdl__dual)
MDL__DERIVED(mdl__zip, mdl__dual, mdl__kis)
MDL__DERIVED(mdl__join, mdl__dual, mdl__ambo)
MDL__DERIVED(mdl__expand, mdl__ambo, mdl__ambo)
MDL__DERIVED(mdl__ortho, mdl__dual, mdl__ambo, mdl__ambo)
MDL__DERIVED(mdl__meta, mdl__kis, mdl__dual, mdl__ambo)
MDL__DERIVED(mdl__bevel, mdl__truncate, mdl__ambo)
MDL__DERIVED(mdl__snub, mdl__dual, mdl__gyro)

typedef struct { const char *name, *letter; mdl__opfn fn; int nnum; double num[4]; } mdl__operator;   /* num: the defaults */

static const mdl__operator mdl__operators[] = {
    {"dual", "d", mdl__dual, 0, {0}}, {"ambo", "a", mdl__ambo, 0, {0}},
    {"truncate", "t", mdl__truncate, 1, {0.3}}, {"kis", "k", mdl__kis, 1, {0.0}},
    {"gyro", "g", mdl__gyro, 0, {0}}, {"reflect", "r", mdl__reflect, 0, {0}},
    {"needle", "n", mdl__needle, 0, {0}}, {"zip", "z", mdl__zip, 0, {0}},
    {"join", "j", mdl__join, 0, {0}}, {"expand", "e", mdl__expand, 0, {0}},
    {"ortho", "o", mdl__ortho, 0, {0}}, {"meta", "m", mdl__meta, 0, {0}},
    {"bevel", "b", mdl__bevel, 0, {0}}, {"snub", "s", mdl__snub, 0, {0}},
    {"extrude", "x", mdl__extrude, 1, {1.0}}, {"inset", "i", mdl__inset, 1, {0.3}},
    {"bridge", "h", mdl__bridge, 0, {0}}, {"lift", NULL, mdl__lift, 1, {1.0}},
    {"scale", NULL, mdl__scale_region, 1, {0.5}}, {"push", NULL, mdl__push, 1, {0.5}},
    {"merge", NULL, mdl__merge, 0, {0}}, {"flip", NULL, mdl__flip, 0, {0}},
    {"mirror", NULL, mdl__mirror, 0, {0}},
    {"move", NULL, mdl__move_by, 3, {0, 0, 0}}, {"turn", NULL, mdl__turn, 3, {0, 0, 0}},
    {"stretch", NULL, mdl__stretch, 3, {1, 1, 1}},
    {"array", NULL, mdl__array, 4, {2, 1, 0, 0}}, {"spin", NULL, mdl__spin, 1, {6}},
};
#define MDL__NOPS ((int)(sizeof mdl__operators / sizeof *mdl__operators))

static const mdl__operator *mdl__operator_named(const char *name) {
    for (int i = 0; i < MDL__NOPS; i++)
        if (!strcmp(mdl__operators[i].name, name) || (mdl__operators[i].letter && !strcmp(mdl__operators[i].letter, name)))
            return &mdl__operators[i];
    return NULL;
}

/* --- seeds (SPEC.md §2) ------------------------------------------------------------------------------ */

static mdl__shape *mdl__seed(mdl__ctx *x, const mdl__v3 *pts, int nv, int *const *faces, const int *sizes, int nf, int convex) {
    mdl__shape *s = (mdl__shape *)mdl__alloc(x->a, sizeof *s);
    s->nv = nv; s->nf = nf; s->step = 0;
    s->v = (mdl__v3 *)mdl__alloc(x->a, sizeof(mdl__v3) * (size_t)nv);
    memcpy(s->v, pts, sizeof(mdl__v3) * (size_t)nv);
    s->f = (mdl__face *)mdl__alloc(x->a, sizeof(mdl__face) * (size_t)nf);
    mdl__v3 c = mdl__centroid_of(pts, nv);
    for (int i = 0; i < nf; i++) {
        mdl__face *f = &s->f[i];
        f->n = sizes[i]; f->role = R_SEED; f->labels.items = NULL; f->labels.n = 0; f->step = 0;
        f->c = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)f->n);
        memcpy(f->c, faces[i], sizeof(int) * (size_t)f->n);
        if (convex) {
            mdl__v3 n = mdl__face_normal(x, s, f), fc = mdl__face_centroid(s, f);
            mdl__v3 out = {fc.x - c.x, fc.y - c.y, fc.z - c.z};
            if (!(mdl__dot(n, out) > 0))
                for (int k = 0; k < f->n / 2; k++) { int t = f->c[k]; f->c[k] = f->c[f->n - 1 - k]; f->c[f->n - 1 - k] = t; }
        }
    }
    return s;
}

/* Faces given as a flat list with a size per face. */
typedef struct { int *flat; int *sizes; int nf; int used; int **ptrs; } mdl__facelist;

static void mdl__facelist_init(mdl__ctx *x, mdl__facelist *l, int maxfaces, int maxcorners) {
    l->flat = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)maxcorners);
    l->sizes = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)maxfaces);
    l->ptrs = (int **)mdl__alloc(x->a, sizeof(int *) * (size_t)maxfaces);
    l->nf = 0; l->used = 0;
}

static void mdl__facelist_add(mdl__facelist *l, const int *c, int n) {
    l->ptrs[l->nf] = l->flat + l->used;
    memcpy(l->flat + l->used, c, sizeof(int) * (size_t)n);
    l->sizes[l->nf++] = n;
    l->used += n;
}

static void mdl__positive(mdl__ctx *x, const char *name, const double *nums, int n) {
    for (int i = 0; i < n; i++)
        if (!(nums[i] > 0)) mdl__fail(x, MDL_ERR_RANGE, "%s: sizes must be greater than 0, not %g", name, nums[i]);
}

static mdl__shape *mdl__icosahedron(mdl__ctx *x, double r);

static void mdl__ring_points(mdl__v3 *out, int n, double r, double z, double turn) {
    for (int i = 0; i < n; i++) {
        double angle = turn + 2 * M_PI * i / n;
        out[i].x = r * cos(angle); out[i].y = r * sin(angle); out[i].z = z;
    }
}

static mdl__shape *mdl__make_seed(mdl__ctx *x, const char *name, const double *given, int ngiven) {
    double num[3];
    mdl__facelist l;
    if (!strcmp(name, "C")) {
        double d[3] = {2, 2, 2};
        for (int i = 0; i < ngiven; i++) d[i] = given[i];
        mdl__positive(x, "C", d, 3);
        mdl__v3 p[8];
        int k = 0;
        for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) for (int m = 0; m < 2; m++) {
            p[k].x = i ? d[0] / 2 : -d[0] / 2; p[k].y = j ? d[1] / 2 : -d[1] / 2; p[k].z = m ? d[2] / 2 : -d[2] / 2; k++;
        }
        static const int faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
        mdl__facelist_init(x, &l, 6, 24);
        for (int i = 0; i < 6; i++) mdl__facelist_add(&l, faces[i], 4);
        return mdl__seed(x, p, 8, l.ptrs, l.sizes, 6, 0);
    }
    num[0] = ngiven > 0 ? given[0] : sqrt(3);
    if (!strcmp(name, "T")) {
        mdl__positive(x, "T", num, 1);
        double k = num[0] / sqrt(3);
        mdl__v3 p[4] = {{k, k, k}, {k, -k, -k}, {-k, k, -k}, {-k, -k, k}};
        static const int faces[4][3] = {{0, 1, 2}, {0, 3, 1}, {0, 2, 3}, {1, 3, 2}};
        mdl__facelist_init(x, &l, 4, 12);
        for (int i = 0; i < 4; i++) mdl__facelist_add(&l, faces[i], 3);
        return mdl__seed(x, p, 4, l.ptrs, l.sizes, 4, 0);
    }
    if (!strcmp(name, "O")) {
        mdl__positive(x, "O", num, 1);
        double r = num[0];
        mdl__v3 p[6] = {{r, 0, 0}, {-r, 0, 0}, {0, r, 0}, {0, -r, 0}, {0, 0, r}, {0, 0, -r}};
        mdl__facelist_init(x, &l, 8, 24);
        for (int a = 0; a < 2; a++) for (int b = 2; b < 4; b++) for (int c = 4; c < 6; c++) {
            int f[3] = {a, b, c};
            mdl__facelist_add(&l, f, 3);
        }
        return mdl__seed(x, p, 6, l.ptrs, l.sizes, 8, 1);
    }
    if (!strcmp(name, "I")) {
        mdl__positive(x, "I", num, 1);
        return mdl__icosahedron(x, num[0]);
    }
    if (!strcmp(name, "D")) {
        mdl__positive(x, "D", num, 1);
        mdl__shape *ico = mdl__icosahedron(x, sqrt(3));
        char *all = (char *)mdl__alloc(x->a, (size_t)ico->nf);
        memset(all, 1, (size_t)ico->nf);
        mdl__shape *d = mdl__dual(x, ico, all, NULL);
        mdl__v3 *p = (mdl__v3 *)mdl__alloc(x->a, sizeof(mdl__v3) * (size_t)d->nv);
        for (int v = 0; v < d->nv; v++) p[v] = mdl__scale(d->v[v], num[0] / sqrt(mdl__dot(d->v[v], d->v[v])));
        mdl__facelist_init(x, &l, d->nf, 5 * d->nf);
        for (int i = 0; i < d->nf; i++) mdl__facelist_add(&l, d->f[i].c, d->f[i].n);
        return mdl__seed(x, p, d->nv, l.ptrs, l.sizes, d->nf, 0);
    }
    char kind = name[0];
    int n = atoi(name + 1);
    if (n < 3) mdl__fail(x, MDL_ERR_RANGE, "%s: needs at least 3 sides", name);
    double r = ngiven > 0 ? given[0] : 1.0, h;
    if (kind == 'P') h = 2 * r * sin(M_PI / n);
    else if (kind == 'A') h = 2 * r * sqrt((cos(M_PI / n) - cos(2 * M_PI / n)) / 2);
    else h = r;
    if (ngiven > 1) h = given[1];
    num[0] = r; num[1] = h;
    mdl__positive(x, name, num, 2);
    mdl__v3 *p = (mdl__v3 *)mdl__alloc(x->a, sizeof(mdl__v3) * (size_t)(2 * n + 1));
    int *f = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)(n + 1));
    mdl__facelist_init(x, &l, 2 * n + 2, 8 * n + 8);
    if (kind == 'Y') {
        mdl__ring_points(p, n, r, 0, 0);
        p[n].x = 0; p[n].y = 0; p[n].z = h;
        for (int i = 0; i < n; i++) f[i] = i;
        mdl__facelist_add(&l, f, n);
        for (int i = 0; i < n; i++) { int t[3] = {i, (i + 1) % n, n}; mdl__facelist_add(&l, t, 3); }
        return mdl__seed(x, p, n + 1, l.ptrs, l.sizes, l.nf, 1);
    }
    mdl__ring_points(p, n, r, -h / 2, 0);
    mdl__ring_points(p + n, n, r, h / 2, kind == 'A' ? M_PI / n : 0);
    for (int i = 0; i < n; i++) f[i] = i;
    mdl__facelist_add(&l, f, n);
    for (int i = 0; i < n; i++) f[i] = n + i;
    mdl__facelist_add(&l, f, n);
    if (kind == 'P') {
        for (int i = 0; i < n; i++) { int q[4] = {i, (i + 1) % n, n + (i + 1) % n, n + i}; mdl__facelist_add(&l, q, 4); }
    } else {
        for (int i = 0; i < n; i++) { int t[3] = {i, (i + 1) % n, n + i}; mdl__facelist_add(&l, t, 3); }
        for (int i = 0; i < n; i++) { int t[3] = {n + i, (i + 1) % n, n + (i + 1) % n}; mdl__facelist_add(&l, t, 3); }
    }
    return mdl__seed(x, p, 2 * n, l.ptrs, l.sizes, l.nf, 1);
}

static mdl__shape *mdl__icosahedron(mdl__ctx *x, double r) {
    double phi = (1 + sqrt(5)) / 2;
    mdl__v3 u[12];
    int k = 0;
    double as[2] = {-1, 1}, bs[2] = {-phi, phi};
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) {
        double a = as[i], b = bs[j];
        u[k].x = 0; u[k].y = a; u[k].z = b; k++;
        u[k].x = a; u[k].y = b; u[k].z = 0; k++;
        u[k].x = b; u[k].y = 0; u[k].z = a; k++;
    }
#define EDGE2(p, q) (fabs((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z) - 4) < 1e-9)
    mdl__facelist l;
    mdl__facelist_init(x, &l, 20, 60);
    for (int i = 0; i < 12; i++) for (int j = i + 1; j < 12; j++) for (int m = j + 1; m < 12; m++)
        if (EDGE2(u[i], u[j]) && EDGE2(u[j], u[m]) && EDGE2(u[i], u[m])) { int t[3] = {i, j, m}; mdl__facelist_add(&l, t, 3); }
#undef EDGE2
    double scale = r / sqrt(1 + phi * phi);
    for (int i = 0; i < 12; i++) u[i] = mdl__scale(u[i], scale);
    return mdl__seed(x, u, 12, l.ptrs, l.sizes, l.nf, 1);
}

static int mdl__is_seed(const char *name) {
    if (!strcmp(name, "C") || !strcmp(name, "T") || !strcmp(name, "O") || !strcmp(name, "I") || !strcmp(name, "D")) return 1;
    if ((name[0] == 'P' || name[0] == 'A' || name[0] == 'Y') && name[1]) {
        for (const char *p = name + 1; *p; p++) if (*p < '0' || *p > '9') return 0;
        return 1;
    }
    return 0;
}

static int mdl__seed_numbers(const char *name) {
    if (!strcmp(name, "C")) return 3;
    if (name[0] == 'P' || name[0] == 'A' || name[0] == 'Y') return 2;
    return 1;
}

/* --- selectors (SPEC.md §7) ------------------------------------------------------------------------- */

enum { S_ALL, S_NEW, S_TAG, S_FACING, S_SIDES, S_INDEX, S_AND, S_OR, S_NOT, S_NEAR };
typedef struct mdl__sel { int kind; const char *tag; int sign, axis, n; struct mdl__sel *a, *b; } mdl__sel;

static const char *mdl__keywords[] = {"all", "new"};

typedef struct { mdl__ctx *x; const char **tok; int n, i; const char *text; } mdl__selreader;

static int mdl__is_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}
static int mdl__is_lower_name(const char *s) {
    if (!(*s >= 'a' && *s <= 'z')) return 0;
    for (s++; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_' || *s == '-')) return 0;
    return 1;
}
static int mdl__is_digits(const char *s) {
    if (!*s) return 0;
    for (; *s; s++) if (*s < '0' || *s > '9') return 0;
    return 1;
}

static mdl__sel *mdl__sel_new(mdl__ctx *x, int kind) {
    mdl__sel *s = (mdl__sel *)mdl__zalloc(x->a, sizeof *s);
    s->kind = kind;
    return s;
}

static mdl__sel *mdl__read_or(mdl__selreader *r);

static mdl__sel *mdl__atom(mdl__ctx *x, const char *t) {
    if (!strcmp(t, "all")) return mdl__sel_new(x, S_ALL);
    if (!strcmp(t, "new")) return mdl__sel_new(x, S_NEW);
    if ((t[0] == '+' || t[0] == '-') && (t[1] == 'x' || t[1] == 'y' || t[1] == 'z') && !t[2]) {
        mdl__sel *s = mdl__sel_new(x, S_FACING);
        s->sign = t[0] == '+' ? 1 : -1; s->axis = t[1] - 'x';
        return s;
    }
    if (t[0] == 'n' && mdl__is_digits(t + 1)) { mdl__sel *s = mdl__sel_new(x, S_SIDES); s->n = atoi(t + 1); return s; }
    if (t[0] == '#' && mdl__is_digits(t + 1)) { mdl__sel *s = mdl__sel_new(x, S_INDEX); s->n = atoi(t + 1); return s; }
    if (mdl__is_lower_name(t)) { mdl__sel *s = mdl__sel_new(x, S_TAG); s->tag = t; return s; }
    mdl__fail(x, MDL_ERR_SYNTAX, "selector: can't read \"%s\"", t);
    return NULL;
}

static mdl__sel *mdl__read_unary(mdl__selreader *r) {
    if (r->i >= r->n) mdl__fail(r->x, MDL_ERR_SYNTAX, "selector: something is missing");
    const char *t = r->tok[r->i++];
    if (!strcmp(t, "!") || !strcmp(t, "~")) {
        mdl__sel *s = mdl__sel_new(r->x, t[0] == '!' ? S_NOT : S_NEAR);
        s->a = mdl__read_unary(r);
        return s;
    }
    if (!strcmp(t, "(")) {
        mdl__sel *s = mdl__read_or(r);
        if (r->i >= r->n || strcmp(r->tok[r->i], ")")) mdl__fail(r->x, MDL_ERR_SYNTAX, "selector: missing )");
        r->i++;
        return s;
    }
    return mdl__atom(r->x, t);
}

static mdl__sel *mdl__read_and(mdl__selreader *r) {
    mdl__sel *a = mdl__read_unary(r);
    if (r->i < r->n && !strcmp(r->tok[r->i], "&")) {
        r->i++;
        mdl__sel *s = mdl__sel_new(r->x, S_AND);
        s->a = a; s->b = mdl__read_and(r);
        return s;
    }
    return a;
}

static mdl__sel *mdl__read_or(mdl__selreader *r) {
    mdl__sel *a = mdl__read_and(r);
    if (r->i < r->n && !strcmp(r->tok[r->i], "|")) {
        r->i++;
        mdl__sel *s = mdl__sel_new(r->x, S_OR);
        s->a = a; s->b = mdl__read_or(r);
        return s;
    }
    return a;
}

static mdl__sel *mdl__read_selector(mdl__ctx *x, const char *text) {
    MDL__VEC(const char *) toks = {0, 0, 0};
    const char *p = text;
    while (*p) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') { p++; continue; }
        if (strchr("()&|!~", *p)) { MDL__PUSH(x->a, toks, mdl__strndup(x->a, p, 1)); p++; continue; }
        const char *q = p;
        if ((*p == '+' || *p == '-') && p[1] >= 'a' && p[1] <= 'z') q = p + 2;
        else if (*p == '#' && p[1] >= '0' && p[1] <= '9') { q = p + 1; while (*q >= '0' && *q <= '9') q++; }
        else if (*p >= 'a' && *p <= 'z') {
            q = p + 1;
            while ((*q >= 'a' && *q <= 'z') || (*q >= '0' && *q <= '9') || *q == '_' || *q == '-') q++;
        } else mdl__fail(x, MDL_ERR_SYNTAX, "selector: can't read \"%s\"", p);
        MDL__PUSH(x->a, toks, mdl__strndup(x->a, p, (size_t)(q - p)));
        p = q;
    }
    mdl__selreader r = {x, toks.items, toks.n, 0, text};
    mdl__sel *s = mdl__read_or(&r);
    if (r.i < r.n) mdl__fail(x, MDL_ERR_SYNTAX, "selector: unexpected %s in [%s]", r.tok[r.i], text);
    return s;
}

static char *mdl__pick(mdl__ctx *x, const mdl__shape *s, const mdl__sel *sel) {
    char *p = (char *)mdl__alloc(x->a, (size_t)s->nf + 1);
    int kind = sel ? sel->kind : S_ALL;
    switch (kind) {
    case S_ALL: memset(p, 1, (size_t)s->nf); break;
    case S_NEW: for (int i = 0; i < s->nf; i++) p[i] = (char)(s->f[i].step == s->step); break;
    case S_TAG:
        for (int i = 0; i < s->nf; i++) {
            const mdl__face *f = &s->f[i];
            p[i] = !strcmp(mdl__roles[f->role], sel->tag);
            for (int k = 0; k < f->labels.n && !p[i]; k++) p[i] = !strcmp(f->labels.items[k], sel->tag);
        }
        break;
    case S_FACING:
        for (int i = 0; i < s->nf; i++) {
            mdl__v3 n = mdl__face_normal(x, s, &s->f[i]);
            double c = sel->axis == 0 ? n.x : sel->axis == 1 ? n.y : n.z;
            p[i] = sel->sign * c > 0.9;
        }
        break;
    case S_SIDES: for (int i = 0; i < s->nf; i++) p[i] = (char)(s->f[i].n == sel->n); break;
    case S_INDEX: for (int i = 0; i < s->nf; i++) p[i] = (char)(i == sel->n); break;
    case S_AND: case S_OR: {
        char *a = mdl__pick(x, s, sel->a), *b = mdl__pick(x, s, sel->b);
        for (int i = 0; i < s->nf; i++) p[i] = kind == S_AND ? (a[i] && b[i]) : (a[i] || b[i]);
        break;
    }
    case S_NOT: { char *a = mdl__pick(x, s, sel->a); for (int i = 0; i < s->nf; i++) p[i] = !a[i]; break; }
    case S_NEAR: {
        char *a = mdl__pick(x, s, sel->a);
        mdl__map owner;
        mdl__owners(x, s, &owner);
        for (int i = 0; i < s->nf; i++) {
            p[i] = 0;
            if (a[i]) continue;
            for (int k = 0; k < s->f[i].n; k++) {
                int t = mdl__twin(&owner, s->f[i].c[k], s->f[i].c[(k + 1) % s->f[i].n]);
                if (t >= 0 && a[t]) { p[i] = 1; break; }
            }
        }
        break;
    }
    }
    return p;
}

/* --- reading programs (SPEC.md §3, §6, §8) --------------------------------------------------------------- */

typedef struct { int is_param; double num; const char *param; } mdl__arg;

typedef struct {
    const char *head;
    mdl__sel *sel;
    mdl__arg *args; int nargs, has_args;
    const char *label;
    int count;
} mdl__token;

typedef struct { mdl__token *items; int n; int *terms; int nterms; } mdl__word_ast;   /* terms: where each part between + starts */

typedef struct {
    mdl_statement kind;
    const char *name;
    const char **params; int nparams;
    mdl__word_ast word;
    const char *file;
} mdl__stmt;

static int mdl__space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

static const char *mdl__trim(mdl__ctx *x, const char *s, size_t n) {
    while (n && mdl__space(*s)) { s++; n--; }
    while (n && mdl__space(s[n - 1])) n--;
    return mdl__strndup(x->a, s, n);
}

static int mdl__is_name(const char *s) {
    if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z'))) return 0;
    for (s++; *s; s++) if (!mdl__is_name_char(*s)) return 0;
    return 1;
}

static int mdl__is_number(const char *s) {
    /* [+-]? (digits (. digits*)? | . digits) ([eE] [+-]? digits)? */
    if (*s == '+' || *s == '-') s++;
    int digits = 0;
    while (*s >= '0' && *s <= '9') { s++; digits++; }
    if (*s == '.') {
        s++;
        int more = 0;
        while (*s >= '0' && *s <= '9') { s++; more++; }
        if (!digits && !more) return 0;
    } else if (!digits) return 0;
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '+' || *s == '-') s++;
        if (!(*s >= '0' && *s <= '9')) return 0;
        while (*s >= '0' && *s <= '9') s++;
    }
    return *s == 0;
}

static int mdl__is_role(const char *s) {
    for (int i = 0; i < R_COUNT; i++) if (!strcmp(mdl__roles[i], s)) return 1;
    return 0;
}

static const char *mdl__read_label(mdl__ctx *x, const char *text) {
    const char *t = mdl__trim(x, text, strlen(text));
    if (!mdl__is_lower_name(t)) mdl__fail(x, MDL_ERR_SYNTAX, "a label is a lowercase name, like {roof}, not \"%s\"", t);
    if (!strcmp(t, mdl__keywords[0]) || !strcmp(t, mdl__keywords[1]) || mdl__is_role(t) || (t[0] == 'n' && mdl__is_digits(t + 1)))
        mdl__fail(x, MDL_ERR_NAME, "%s can't be a label: it already means something in a selector", t);
    return t;
}

static const char *mdl__enclosed(mdl__ctx *x, const char **p, char open, char close) {
    /* The text between open (at *p) and close, moving *p past close; or NULL. */
    if (**p != open) return NULL;
    const char *end = strchr(*p + 1, close);
    if (!end) mdl__fail(x, MDL_ERR_SYNTAX, "missing %c", close);
    const char *r = mdl__strndup(x->a, *p + 1, (size_t)(end - *p - 1));
    *p = end + 1;
    return r;
}

static const char *mdl__parse_token(mdl__ctx *x, const char *p, mdl__token *t) {
    const char *start = p;
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) mdl__fail(x, MDL_ERR_SYNTAX, "can't read \"%s\"", p);
    p++;
    while (mdl__is_name_char(*p)) p++;
    t->head = mdl__strndup(x->a, start, (size_t)(p - start));
    const char *sel = mdl__enclosed(x, &p, '[', ']');
    const char *args = mdl__enclosed(x, &p, '(', ')');
    const char *label = mdl__enclosed(x, &p, '{', '}');
    t->count = 1;
    if (*p == '^') {
        p++;
        if (!(*p >= '0' && *p <= '9')) mdl__fail(x, MDL_ERR_SYNTAX, "^ needs a count, like ^3");
        long c = 0;
        while (*p >= '0' && *p <= '9') { c = c * 10 + (*p - '0'); if (c > 1000000) c = 1000000; p++; }
        t->count = (int)c;
    }
    if (t->count == 0) mdl__fail(x, MDL_ERR_SYNTAX, "repeat at least once");
    t->sel = sel ? mdl__read_selector(x, sel) : NULL;
    t->has_args = args != NULL;
    t->nargs = 0; t->args = NULL;
    if (args && *args) {
        int n = 1;
        for (const char *q = args; *q; q++) if (*q == ',') n++;
        t->args = (mdl__arg *)mdl__alloc(x->a, sizeof(mdl__arg) * (size_t)n);
        const char *q = args;
        for (int i = 0; i < n; i++) {
            const char *comma = strchr(q, ',');
            size_t len = comma ? (size_t)(comma - q) : strlen(q);
            const char *a = mdl__trim(x, q, len);
            if (mdl__is_number(a)) { t->args[i].is_param = 0; t->args[i].num = strtod(a, NULL); }
            else if (mdl__is_name(a)) { t->args[i].is_param = 1; t->args[i].param = a; }
            else mdl__fail(x, MDL_ERR_SYNTAX, "\"%s\" is not a number", a);
            q = comma ? comma + 1 : q + len;
        }
        t->nargs = n;
    }
    t->label = label ? mdl__read_label(x, label) : NULL;
    return p;
}

static mdl__word_ast mdl__parse_word(mdl__ctx *x, const char *text) {
    /* Tokens, and the terms they fall into: the parts between top-level + signs. */
    MDL__VEC(mdl__token) toks = {0, 0, 0};
    MDL__VEC(int) terms = {0, 0, 0};
    const char *p = text;
    while (mdl__space(*p)) p++;
    MDL__PUSH(x->a, terms, 0);
    while (*p) {
        if (*p == '+' && (!p[1] || mdl__space(p[1]))) {
            if (toks.n == terms.items[terms.n - 1])
                mdl__fail(x, MDL_ERR_SYNTAX, "a word needs at least a seed, like C, on each side of +");
            MDL__PUSH(x->a, terms, toks.n);
            p++;
            while (mdl__space(*p)) p++;
            continue;
        }
        mdl__token t;
        p = mdl__parse_token(x, p, &t);
        if (*p && !mdl__space(*p)) mdl__fail(x, MDL_ERR_SYNTAX, "expected a space before \"%s\"", p);
        MDL__PUSH(x->a, toks, t);
        while (mdl__space(*p)) p++;
    }
    if (toks.n == terms.items[terms.n - 1])
        mdl__fail(x, MDL_ERR_SYNTAX, terms.n > 1 ? "a word needs at least a seed, like C, on each side of +"
                                                 : "a word needs at least a seed, like C");
    mdl__word_ast w = {toks.items, toks.n, terms.items, terms.n};
    return w;
}

static const char *mdl__keyword(const char *p, const char *word) {
    /* p after leading spaces and the keyword, if the keyword ends there; else NULL. */
    while (mdl__space(*p)) p++;
    size_t n = strlen(word);
    if (strncmp(p, word, n)) return NULL;
    char c = p[n];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') return NULL;
    return p + n;
}

static const char *mdl__quoted(mdl__ctx *x, const char **p) {
    /* After spaces (at least one), a "file name"; NULL if there isn't one. */
    const char *q = *p;
    if (!mdl__space(*q)) return NULL;
    while (mdl__space(*q)) q++;
    if (*q != '"') return NULL;
    const char *end = strchr(q + 1, '"');
    if (!end) return NULL;
    *p = end + 1;
    return mdl__strndup(x->a, q + 1, (size_t)(end - q - 1));
}

static int mdl__parse_line(mdl__ctx *x, const char *line, mdl__stmt *st) {
    /* 0 for a blank line or a comment. */
    const char *semi = strchr(line, ';');
    const char *code = mdl__strndup(x->a, line, semi ? (size_t)(semi - line) : strlen(line));
    const char *p = code;
    while (mdl__space(*p)) p++;
    if (!*p) return 0;
    memset(st, 0, sizeof *st);
    const char *after;
    if ((after = mdl__keyword(code, "use")) || (after = mdl__keyword(code, "gallery"))) {
        int use = mdl__keyword(code, "use") != NULL;
        const char *q = after, *file = mdl__quoted(x, &q);
        if (file) {
            while (mdl__space(*q)) q++;
            if (!*q) { st->kind = use ? MDL_USE : MDL_GALLERY; st->file = file; return 1; }
        }
        mdl__fail(x, MDL_ERR_SYNTAX, "expected a file name in quotes, like gallery \"shapes.svg\"");
    }
    if ((after = mdl__keyword(code, "export"))) {
        const char *q = after, *file = mdl__quoted(x, &q);
        if (file) {
            st->kind = MDL_EXPORT; st->file = file;
            st->word = mdl__parse_word(x, q);
            return 1;
        }
    }
    /* name, optional (params), then = */
    const char *q = p;
    if ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z')) {
        q++;
        while (mdl__is_name_char(*q)) q++;
        const char *name_end = q, *params = NULL, *params_end = NULL;
        while (mdl__space(*q)) q++;
        if (*q == '(') {
            const char *close = strchr(q, ')');
            if (close) { params = q + 1; params_end = close; q = close + 1; }
            else q = NULL;
        }
        if (q) while (mdl__space(*q)) q++;
        if (q && *q == '=') {
            st->kind = MDL_DEFINE;
            st->name = mdl__strndup(x->a, p, (size_t)(name_end - p));
            if (params && *mdl__trim(x, params, (size_t)(params_end - params))) {
                MDL__VEC(const char *) ps = {0, 0, 0};
                const char *r = params;
                for (;;) {
                    const char *comma = memchr(r, ',', (size_t)(params_end - r));
                    const char *end = comma ? comma : params_end;
                    const char *name = mdl__trim(x, r, (size_t)(end - r));
                    if (!mdl__is_name(name)) mdl__fail(x, MDL_ERR_SYNTAX, "\"%s\" is not a parameter name", name);
                    for (int i = 0; i < ps.n; i++) if (!strcmp(ps.items[i], name)) mdl__fail(x, MDL_ERR_SYNTAX, "a parameter is named twice");
                    MDL__PUSH(x->a, ps, name);
                    if (!comma) break;
                    r = comma + 1;
                }
                st->params = ps.items; st->nparams = ps.n;
            }
            st->word = mdl__parse_word(x, q + 1);
            return 1;
        }
    }
    st->kind = MDL_SHOW;
    st->word = mdl__parse_word(x, code);
    return 1;
}

/* --- what words mean (SPEC.md §3–§6) --------------------------------------------------------------- */

enum { OP_PRIM, OP_SEQ, OP_LABEL, OP_REPEAT };
typedef struct mdl__op {
    int kind;
    const mdl__operator *prim; const mdl__sel *sel; double num[4];
    struct mdl__op **ops; int nops;     /* OP_SEQ: applied in order */
    struct mdl__op *inner; const char *label; int count;
} mdl__op;

typedef struct { mdl__shape *shape; mdl__op *op; } mdl__value;

typedef struct { const char *name; const char **params; int nparams; mdl__word_ast word; } mdl__def;
typedef MDL__VEC(mdl__def) mdl__env;
typedef struct { const char **names; double *values; int n; } mdl__bindings;

static mdl__shape *mdl__apply(mdl__ctx *x, const mdl__op *op, mdl__shape *s) {
    switch (op->kind) {
    case OP_PRIM: {
        mdl__shape *out = op->prim->fn(x, s, mdl__pick(x, s, op->sel), op->num);
        if (out->nf > MDL_MAX_FACES)
            mdl__fail(x, MDL_ERR_LIMIT, "%s makes %d faces, more than the limit of %d", op->prim->name, out->nf, MDL_MAX_FACES);
        mdl__check(x, out);
        return out;
    }
    case OP_SEQ:
        for (int i = 0; i < op->nops; i++) s = mdl__apply(x, op->ops[i], s);
        return s;
    case OP_REPEAT:
        for (int i = 0; i < op->count; i++) s = mdl__apply(x, op->inner, s);
        return s;
    case OP_LABEL: {
        int before = s->step;
        mdl__shape *out = mdl__apply(x, op->inner, s);
        mdl__shape *r = (mdl__shape *)mdl__alloc(x->a, sizeof *r);
        *r = *out;
        r->f = (mdl__face *)mdl__alloc(x->a, sizeof(mdl__face) * (size_t)(out->nf + 1));
        for (int i = 0; i < out->nf; i++) {
            r->f[i] = out->f[i];
            if (out->f[i].step > before) r->f[i].labels = mdl__with_label(x, out->f[i].labels, op->label);
        }
        return r;
    }
    }
    return s;
}

static mdl__op *mdl__op_new(mdl__ctx *x, int kind) {
    mdl__op *o = (mdl__op *)mdl__zalloc(x->a, sizeof *o);
    o->kind = kind;
    return o;
}

static int mdl__reserved(const char *name) {
    return mdl__operator_named(name) || mdl__is_seed(name) || (name[0] >= 'a' && name[0] <= 'z' && !name[1]);
}

static mdl__value mdl__eval_word(mdl__ctx *x, const mdl__env *env, const mdl__word_ast *w, const mdl__bindings *b);

static double *mdl__numbers(mdl__ctx *x, const mdl__token *t, int most, const mdl__bindings *b) {
    if (t->nargs > most) {
        if (most == 0) mdl__fail(x, MDL_ERR_ARITY, "%s takes no numbers", t->head);
        if (most == 1) mdl__fail(x, MDL_ERR_ARITY, "%s takes at most one number", t->head);
        mdl__fail(x, MDL_ERR_ARITY, "%s takes at most %d numbers", t->head, most);
    }
    double *out = (double *)mdl__alloc(x->a, sizeof(double) * (size_t)(t->nargs + 1));
    for (int i = 0; i < t->nargs; i++) {
        if (!t->args[i].is_param) { out[i] = t->args[i].num; continue; }
        int found = 0;
        for (int k = 0; b && k < b->n; k++) if (!strcmp(b->names[k], t->args[i].param)) { out[i] = b->values[k]; found = 1; }
        if (!found) mdl__fail(x, MDL_ERR_NAME, "%s isn't a parameter here", t->args[i].param);
    }
    return out;
}

static mdl__value mdl__token_value(mdl__ctx *x, const mdl__env *env, const mdl__token *t, const mdl__bindings *b) {
    mdl__value v = {NULL, NULL};
    const mdl__operator *o = mdl__operator_named(t->head);
    const mdl__def *def = NULL;
    if (!o && !mdl__is_seed(t->head) && env)
        for (int i = 0; i < env->n; i++) if (!strcmp(env->items[i].name, t->head)) def = &env->items[i];
    if (o) {
        double *num = mdl__numbers(x, t, o->nnum, b);
        mdl__op *op = mdl__op_new(x, OP_PRIM);
        op->prim = o; op->sel = t->sel;
        for (int k = 0; k < o->nnum; k++) op->num[k] = k < t->nargs ? num[k] : o->num[k];
        v.op = op;
    } else if (mdl__is_seed(t->head)) {
        if (t->sel) mdl__fail(x, MDL_ERR_SYNTAX, "only primitive operators take [selectors]; %s is a seed", t->head);
        double *num = mdl__numbers(x, t, mdl__seed_numbers(t->head), b);
        v.shape = mdl__make_seed(x, t->head, num, t->nargs);
    } else if (def) {
        if (t->sel) mdl__fail(x, MDL_ERR_SYNTAX, "only primitive operators take [selectors]; %s is a name", t->head);
        double *num = mdl__numbers(x, t, t->nargs, b);
        if (t->nargs != def->nparams)
            mdl__fail(x, MDL_ERR_ARITY, "%s takes %d number%s, not %d", t->head, def->nparams, def->nparams == 1 ? "" : "s", t->nargs);
        mdl__bindings inner = {def->params, num, def->nparams};
        v = mdl__eval_word(x, env, &def->word, &inner);
    } else if (t->head[0] >= 'a' && t->head[0] <= 'z' && !t->head[1]) {
        mdl__fail(x, MDL_ERR_NAME, "there is no operator %s", t->head);
    } else {
        mdl__fail(x, MDL_ERR_NAME, "%s isn't defined above this line", t->head);
    }
    if (t->label) {
        if (v.shape) {
            mdl__shape *r = (mdl__shape *)mdl__alloc(x->a, sizeof *r);
            *r = *v.shape;
            r->f = (mdl__face *)mdl__alloc(x->a, sizeof(mdl__face) * (size_t)(r->nf + 1));
            for (int i = 0; i < r->nf; i++) {
                r->f[i] = v.shape->f[i];
                r->f[i].labels = mdl__with_label(x, r->f[i].labels, t->label);
            }
            v.shape = r;
        } else {
            mdl__op *l = mdl__op_new(x, OP_LABEL);
            l->inner = v.op; l->label = t->label;
            v.op = l;
        }
    }
    if (t->count != 1) {
        if (v.shape) mdl__fail(x, MDL_ERR_KIND, "only operators can be repeated, and %s is a shape", t->head);
        mdl__op *r = mdl__op_new(x, OP_REPEAT);
        r->inner = v.op; r->count = t->count;
        v.op = r;
    }
    return v;
}

static mdl__shape *mdl__side_by_side(mdl__ctx *x, const mdl__shape *a, const mdl__shape *b) {
    /* a's vertices and faces, then b's. The step counter is the larger one. */
    mdl__build out; mdl__build_from(&out, x, a);
    for (int v = 0; v < b->nv; v++) mdl__vertex(&out, b->v[v]);
    for (int i = 0; i < a->nf; i++) mdl__keep(&out, &a->f[i]);
    for (int i = 0; i < b->nf; i++) {
        const mdl__face *f = &b->f[i];
        int *q = (int *)mdl__alloc(x->a, sizeof(int) * (size_t)f->n);
        for (int k = 0; k < f->n; k++) q[k] = f->c[k] + a->nv;
        mdl__face_add(&out, q, f->n, f->role, f->labels, f->step);
    }
    mdl__shape *s = (mdl__shape *)mdl__alloc(x->a, sizeof *s);
    s->v = out.v.items; s->nv = out.v.n; s->f = out.f.items; s->nf = out.f.n;
    s->step = a->step > b->step ? a->step : b->step;
    return s;
}

static mdl__value mdl__eval_term(mdl__ctx *x, const mdl__env *env, const mdl__word_ast *w, const mdl__bindings *b);

static mdl__value mdl__eval_word(mdl__ctx *x, const mdl__env *env, const mdl__word_ast *w, const mdl__bindings *b) {
    /* One term is a word as before. Several are shapes side by side (SPEC.md §3). */
    if (w->nterms <= 1) return mdl__eval_term(x, env, w, b);
    mdl__value r = {NULL, NULL};
    for (int k = 0; k < w->nterms; k++) {
        int from = w->terms[k], to = k + 1 < w->nterms ? w->terms[k + 1] : w->n;
        mdl__word_ast term = {w->items + from, to - from, NULL, 1};
        mdl__value v = mdl__eval_term(x, env, &term, b);
        if (!v.shape) mdl__fail(x, MDL_ERR_KIND, "+ puts shapes side by side, and %s is an operator", w->items[to - 1].head);
        r.shape = r.shape ? mdl__side_by_side(x, r.shape, v.shape) : v.shape;
    }
    return r;
}

static mdl__value mdl__eval_term(mdl__ctx *x, const mdl__env *env, const mdl__word_ast *w, const mdl__bindings *b) {
    mdl__value *vs = (mdl__value *)mdl__alloc(x->a, sizeof(mdl__value) * (size_t)w->n);
    for (int i = 0; i < w->n; i++) vs[i] = mdl__token_value(x, env, &w->items[i], b);
    for (int i = 0; i < w->n - 1; i++)
        if (vs[i].shape) mdl__fail(x, MDL_ERR_KIND, "%s is a shape, so it can only come last in a word", w->items[i].head);
    mdl__value base = vs[w->n - 1], r = {NULL, NULL};
    if (base.shape) {
        mdl__shape *s = base.shape;
        for (int i = w->n - 2; i >= 0; i--) s = mdl__apply(x, vs[i].op, s);
        r.shape = s;
    } else {
        mdl__op *seq = mdl__op_new(x, OP_SEQ);
        seq->nops = w->n;
        seq->ops = (mdl__op **)mdl__alloc(x->a, sizeof(mdl__op *) * (size_t)w->n);
        for (int i = 0; i < w->n; i++) seq->ops[i] = vs[w->n - 1 - i].op;
        r.op = seq;
    }
    return r;
}

/* --- shapes for the caller ------------------------------------------------------------------------------ */

struct mdl_shape {
    int nv, nf;
    double *v;
    int *start, *corners, *role, *step, *lstart;
    const char **labels;
    char *text;
};

static mdl_shape *mdl__export(const mdl__shape *s) {
    mdl_shape *r = (mdl_shape *)calloc(1, sizeof *r);
    int corners = 0, labels = 0;
    size_t text = 0;
    for (int i = 0; i < s->nf; i++) {
        corners += s->f[i].n;
        labels += s->f[i].labels.n;
        for (int k = 0; k < s->f[i].labels.n; k++) text += strlen(s->f[i].labels.items[k]) + 1;
    }
    r->nv = s->nv; r->nf = s->nf;
    r->v = (double *)malloc(sizeof(double) * (size_t)(3 * s->nv + 1));
    r->start = (int *)malloc(sizeof(int) * (size_t)(s->nf + 1));
    r->corners = (int *)malloc(sizeof(int) * (size_t)(corners + 1));
    r->role = (int *)malloc(sizeof(int) * (size_t)(s->nf + 1));
    r->step = (int *)malloc(sizeof(int) * (size_t)(s->nf + 1));
    r->lstart = (int *)malloc(sizeof(int) * (size_t)(s->nf + 1));
    r->labels = (const char **)malloc(sizeof(char *) * (size_t)(labels + 1));
    r->text = (char *)malloc(text + 1);
    if (!r->v || !r->start || !r->corners || !r->role || !r->step || !r->lstart || !r->labels || !r->text) {
        fputs("modelita: out of memory\n", stderr);
        abort();
    }
    for (int v = 0; v < s->nv; v++) { r->v[3 * v] = s->v[v].x; r->v[3 * v + 1] = s->v[v].y; r->v[3 * v + 2] = s->v[v].z; }
    int c = 0, l = 0;
    char *t = r->text;
    for (int i = 0; i < s->nf; i++) {
        r->start[i] = c;
        r->lstart[i] = l;
        r->role[i] = s->f[i].role;
        r->step[i] = s->f[i].step;
        for (int k = 0; k < s->f[i].n; k++) r->corners[c++] = s->f[i].c[k];
        for (int k = 0; k < s->f[i].labels.n; k++) {
            size_t n = strlen(s->f[i].labels.items[k]) + 1;
            memcpy(t, s->f[i].labels.items[k], n);
            r->labels[l++] = t;
            t += n;
        }
    }
    r->start[s->nf] = c;
    r->lstart[s->nf] = l;
    return r;
}

void mdl_shape_free(mdl_shape *s) {
    if (!s) return;
    free(s->v); free(s->start); free(s->corners); free(s->role); free(s->step);
    free(s->lstart); free(s->labels); free(s->text); free(s);
}

int mdl_vertex_count(const mdl_shape *s) { return s->nv; }
const double *mdl_vertices(const mdl_shape *s) { return s->v; }
int mdl_face_count(const mdl_shape *s) { return s->nf; }
int mdl_face_size(const mdl_shape *s, int i) { return s->start[i + 1] - s->start[i]; }
const int *mdl_face_corners(const mdl_shape *s, int i) { return s->corners + s->start[i]; }
const char *mdl_face_role(const mdl_shape *s, int i) { return mdl__roles[s->role[i]]; }
int mdl_face_step(const mdl_shape *s, int i) { return s->step[i]; }
int mdl_face_label_count(const mdl_shape *s, int i) { return s->lstart[i + 1] - s->lstart[i]; }
const char *mdl_face_label(const mdl_shape *s, int i, int k) { return s->labels[s->lstart[i] + k]; }

/* --- running -------------------------------------------------------------------------------------------- */

static mdl__ctx *mdl__ctx_new(jmp_buf *jump) {
    mdl__ctx *x = (mdl__ctx *)calloc(1, sizeof *x);
    if (!x) { fputs("modelita: out of memory\n", stderr); abort(); }
    x->a = &x->work;
    x->jump = jump;
    return x;
}

static void mdl__ctx_free(mdl__ctx *x) {
    mdl__arena_free(&x->work);
    free(x);
}

mdl_shape *mdl_word(const char *word, mdl_error *error) {
    jmp_buf jump;
    mdl__ctx *const x = mdl__ctx_new(&jump);
    mdl_shape *volatile r = NULL;                /* set after setjmp, read after a jump */
    if (error) { error->status = MDL_OK; error->line = 0; error->message[0] = 0; }
    if (!setjmp(jump)) {
        mdl__word_ast w = mdl__parse_word(x, word);
        mdl__value v = mdl__eval_word(x, NULL, &w, NULL);
        if (!v.shape) mdl__fail(x, MDL_ERR_KIND, "this word is an operator, not a shape: it needs a seed at the end");
        r = mdl__export(v.shape);
    } else if (error) {
        error->status = x->status;
        snprintf(error->message, sizeof error->message, "%s", x->message);
    }
    mdl__ctx_free(x);
    return r;
}

static char *mdl__read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 4096, n = 0;
    char *buf = (char *)malloc(cap);
    size_t got;
    while (buf && (got = fread(buf + n, 1, cap - n - 1, f)) > 0) {
        n += got;
        if (cap - n - 1 == 0) { char *more = (char *)realloc(buf, cap * 2); if (!more) { free(buf); buf = NULL; break; } buf = more; cap *= 2; }
    }
    fclose(f);
    if (buf) buf[n] = 0;
    return buf;
}

static char *mdl__beside(mdl__arena *a, const char *path, const char *file) {
    /* file, in the same folder as path */
    const char *slash = path ? strrchr(path, '/') : NULL;
    if (!slash || file[0] == '/') return mdl__strndup(a, file, strlen(file));
    size_t dir = (size_t)(slash - path) + 1, n = strlen(file);
    char *r = (char *)mdl__alloc(a, dir + n + 1);
    memcpy(r, path, dir);
    memcpy(r + dir, file, n + 1);
    return r;
}

typedef struct { mdl__arena *program; mdl__env env; const char *path; int depth; } mdl__program;

static void mdl__run_text(mdl__program *pg, const char *text, mdl_result_fn fn, void *user, int *failed);

static void mdl__count_failure(const mdl_result *r, void *user) {
    if (r->error.status != MDL_OK && !*(int *)user) *(int *)user = r->line;
}

static void mdl__run_line(mdl__program *pg, const char *line, int lineno, mdl_result_fn fn, void *user, int *failed) {
    jmp_buf jump;
    mdl__ctx *const x = mdl__ctx_new(&jump);
    /* set after setjmp and read after a jump, so volatile */
    volatile int kind = MDL_SHOW, report = 0, is_operator = 0;
    const char *volatile file = NULL;
    mdl_shape *volatile shown = NULL;
    if (!setjmp(jump)) {
        mdl__stmt st;
        x->a = pg->program;                       /* what is read outlives the line */
        if (!mdl__parse_line(x, line, &st)) { mdl__ctx_free(x); return; }
        x->a = &x->work;
        kind = st.kind;
        file = st.file;
        switch (st.kind) {
        case MDL_DEFINE: {
            if (mdl__reserved(st.name)) mdl__fail(x, MDL_ERR_NAME, "%s is reserved: it is an operator, a letter or a seed", st.name);
            for (int i = 0; i < pg->env.n; i++)
                if (!strcmp(pg->env.items[i].name, st.name)) mdl__fail(x, MDL_ERR_NAME, "%s is already defined", st.name);
            /* run it once now, so a broken definition is reported here */
            if (!st.nparams) mdl__eval_word(x, &pg->env, &st.word, NULL);
            mdl__def d = {st.name, st.params, st.nparams, st.word};
            MDL__PUSH(pg->program, pg->env, d);
            break;
        }
        case MDL_SHOW: case MDL_EXPORT: {
            mdl__value v = mdl__eval_word(x, &pg->env, &st.word, NULL);
            if (v.shape) shown = mdl__export(v.shape);
            is_operator = v.op != NULL;
            report = 1;
            break;
        }
        case MDL_USE: {
            if (pg->depth > 16) mdl__fail(x, MDL_ERR_NAME, "use: files use each other too deeply");
            const char *path = mdl__beside(pg->program, pg->path, st.file);
            char *text = mdl__read_file(path);
            if (!text) mdl__fail(x, MDL_ERR_NAME, "use: can't read %s", path);
            /* its own run first, to report its errors here; then its definitions */
            mdl__program other = {pg->program, {0, 0, 0}, path, pg->depth + 1};
            int bad = 0;
            mdl__run_text(&other, text, mdl__count_failure, &bad, NULL);
            free(text);
            if (bad) mdl__fail(x, MDL_ERR_NAME, "%s, line %d has an error", st.file, bad);
            for (int i = 0; i < other.env.n; i++) MDL__PUSH(pg->program, pg->env, other.env.items[i]);
            break;
        }
        case MDL_GALLERY:
            break;
        }
    } else {
        report = 1;
        if (failed && !*failed) *failed = lineno;
    }
    mdl_result r;
    memset(&r, 0, sizeof r);
    r.line = lineno;
    r.statement = (mdl_statement)kind;
    r.file = file;
    r.shape = shown;
    r.is_operator = is_operator;
    r.skipped = x->skips;
    r.error.status = x->status;
    if (x->status != MDL_OK) {
        r.error.line = lineno;
        snprintf(r.error.message, sizeof r.error.message, "%s", x->message);
    }
    if (report && fn) fn(&r, user);
    mdl_shape_free(shown);
    mdl__ctx_free(x);
}

static void mdl__run_text(mdl__program *pg, const char *text, mdl_result_fn fn, void *user, int *failed) {
    const char *p = text;
    int lineno = 1;
    for (;;) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n && p[n - 1] == '\r') n--;
        char *line = (char *)malloc(n + 1);
        if (!line) { fputs("modelita: out of memory\n", stderr); abort(); }
        memcpy(line, p, n); line[n] = 0;
        mdl__run_line(pg, line, lineno, fn, user, failed);
        free(line);
        if (!end) break;
        p = end + 1;
        lineno++;
    }
}

void mdl_run_program(const char *text, const char *path, mdl_result_fn fn, void *user) {
    mdl__arena program = {0};
    mdl__program pg = {&program, {0, 0, 0}, path, 0};
    mdl__run_text(&pg, text, fn, user, NULL);
    mdl__arena_free(&program);
}

#endif /* MODELITA_IMPLEMENTATION */
