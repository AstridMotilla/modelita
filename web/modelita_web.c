/* The bridge between modelita.h and the playground: run a program and get
   every result back as JSON, which JavaScript reads with JSON.parse.

   [{"line": 3, "statement": "show", "kind": "shape" | "operator" | "error",
     "category": "range", "message": "...",          (errors)
     "skipped": 0,
     "vertices": [x, y, z, ...], "faces": [[0, 1, 2, 3], ...],
     "roles": ["seed", ...], "steps": [0, ...], "labels": [["roof"], [], ...]}] */
#define MODELITA_IMPLEMENTATION
#include "../c/modelita.h"
#include <emscripten.h>

typedef struct { char *text; size_t n, cap; } buffer;

static void put(buffer *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        while (b->n + n + 1 > b->cap) b->cap = b->cap ? b->cap * 2 : 1 << 16;
        b->text = realloc(b->text, b->cap);
        if (!b->text) abort();
    }
    memcpy(b->text + b->n, s, n);
    b->n += n;
    b->text[b->n] = 0;
}

static void puts_(buffer *b, const char *s) { put(b, s, strlen(s)); }

static void printf_(buffer *b, const char *fmt, ...) {
    char tmp[64];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    put(b, tmp, (size_t)n);
}

static void string(buffer *b, const char *s) {
    put(b, "\"", 1);
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { put(b, "\\", 1); put(b, s, 1); }
        else if (c < 0x20) printf_(b, "\\u%04x", c);
        else put(b, s, 1);
    }
    put(b, "\"", 1);
}

static void number(buffer *b, double x) {
    /* the shortest text that reads back as the same double */
    char tmp[32];
    if (x == 0) x = 0;
    for (int digits = 15; digits <= 17; digits++) {
        snprintf(tmp, sizeof tmp, "%.*g", digits, x);
        if (strtod(tmp, NULL) == x) break;
    }
    puts_(b, tmp);
}

static const char *statements[] = {"show", "export", "define", "use", "gallery"};

static void one(const mdl_result *r, void *user) {
    buffer *b = user;
    if (b->n > 1) puts_(b, ",");
    printf_(b, "{\"line\":%d,\"statement\":", r->line);
    string(b, statements[r->statement]);
    printf_(b, ",\"skipped\":%d,\"kind\":", r->skipped);
    if (r->error.status != MDL_OK) {
        string(b, "error");
        puts_(b, ",\"category\":");
        string(b, mdl_status_name(r->error.status));
        puts_(b, ",\"message\":");
        string(b, r->error.message);
    } else if (r->is_operator) {
        string(b, "operator");
    } else {
        const mdl_shape *s = r->shape;
        string(b, "shape");
        puts_(b, ",\"vertices\":[");
        const double *v = mdl_vertices(s);
        for (int i = 0; i < 3 * mdl_vertex_count(s); i++) { if (i) puts_(b, ","); number(b, v[i]); }
        puts_(b, "],\"faces\":[");
        for (int i = 0; i < mdl_face_count(s); i++) {
            puts_(b, i ? ",[" : "[");
            for (int k = 0; k < mdl_face_size(s, i); k++) printf_(b, k ? ",%d" : "%d", mdl_face_corners(s, i)[k]);
            puts_(b, "]");
        }
        puts_(b, "],\"roles\":[");
        for (int i = 0; i < mdl_face_count(s); i++) { if (i) puts_(b, ","); string(b, mdl_face_role(s, i)); }
        puts_(b, "],\"steps\":[");
        for (int i = 0; i < mdl_face_count(s); i++) printf_(b, i ? ",%d" : "%d", mdl_face_step(s, i));
        puts_(b, "],\"labels\":[");
        for (int i = 0; i < mdl_face_count(s); i++) {
            puts_(b, i ? ",[" : "[");
            for (int k = 0; k < mdl_face_label_count(s, i); k++) { if (k) puts_(b, ","); string(b, mdl_face_label(s, i, k)); }
            puts_(b, "]");
        }
        puts_(b, "]");
    }
    puts_(b, "}");
}

static buffer out;

/* The results of running text, as JSON. Valid until the next call. */
EMSCRIPTEN_KEEPALIVE const char *modelita_run(const char *text) {
    out.n = 0;
    put(&out, "[", 1);
    mdl_run_program(text, NULL, one, &out);
    put(&out, "]", 1);
    return out.text;
}
