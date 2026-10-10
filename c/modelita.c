/* modelita FILE        run a Modelita program, print the dump of every shown word
   modelita -e WORD     the dump of one word
   modelita             a program from standard input

   The dump is described in tests/README.md. Error messages go to stderr.
   `export` also writes the OBJ file next to FILE (not with --no-write). */
#define MODELITA_IMPLEMENTATION
#include "modelita.h"

typedef struct { const char *path; int write; } options;

static void number(double x) {
    /* the shortest text that reads back as the same double */
    char buf[64];
    if (x == 0) x = 0;                                    /* no -0 */
    for (int digits = 15; digits <= 17; digits++) {
        snprintf(buf, sizeof buf, "%.*g", digits, x);
        if (strtod(buf, NULL) == x) break;
    }
    fputs(buf, stdout);
}

static void write_obj(const mdl_shape *s, const char *path) {
    /* OBJ is read with y up, so (x y z) is written as (x z -y). One group per role. */
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "can't write %s\n", path); return; }
    const double *v = mdl_vertices(s);
    for (int i = 0; i < mdl_vertex_count(s); i++)
        fprintf(f, "v %.6f %.6f %.6f\n", v[3 * i], v[3 * i + 2], -v[3 * i + 1]);
    const char *group = NULL;
    for (int i = 0; i < mdl_face_count(s); i++) {
        if (!group || strcmp(group, mdl_face_role(s, i))) fprintf(f, "g %s\n", group = mdl_face_role(s, i));
        fputc('f', f);
        for (int k = 0; k < mdl_face_size(s, i); k++) fprintf(f, " %d", mdl_face_corners(s, i)[k] + 1);
        fputc('\n', f);
    }
    fclose(f);
    fprintf(stderr, "wrote %s\n", path);
}

static void print(const mdl_result *r, void *user) {
    const options *o = (const options *)user;
    printf("line %d\n", r->line);
    if (r->error.status != MDL_OK) {
        printf("error %s\nend\n", mdl_status_name(r->error.status));
        fprintf(stderr, "line %d: %s\n", r->line, r->error.message);
        return;
    }
    if (r->is_operator) { printf("operator\nend\n"); return; }
    const mdl_shape *s = r->shape;
    printf("shape\nvertices %d\n", mdl_vertex_count(s));
    const double *v = mdl_vertices(s);
    for (int i = 0; i < mdl_vertex_count(s); i++) {
        printf("v ");
        number(v[3 * i]); putchar(' ');
        number(v[3 * i + 1]); putchar(' ');
        number(v[3 * i + 2]); putchar('\n');
    }
    printf("faces %d\n", mdl_face_count(s));
    for (int i = 0; i < mdl_face_count(s); i++) {
        printf("f %s %d ", mdl_face_role(s, i), mdl_face_step(s, i));
        if (!mdl_face_label_count(s, i)) putchar('-');
        for (int k = 0; k < mdl_face_label_count(s, i); k++) printf("%s%s", k ? "," : "", mdl_face_label(s, i, k));
        printf(" :");
        for (int k = 0; k < mdl_face_size(s, i); k++) printf(" %d", mdl_face_corners(s, i)[k]);
        putchar('\n');
    }
    printf("skipped %d\nend\n", r->skipped);
    if (r->statement == MDL_EXPORT && o->write && o->path) {
        const char *slash = strrchr(o->path, '/');
        char path[4096];
        if (slash && r->file[0] != '/') snprintf(path, sizeof path, "%.*s%s", (int)(slash - o->path + 1), o->path, r->file);
        else snprintf(path, sizeof path, "%s", r->file);
        write_obj(s, path);
    }
}

static char *slurp(FILE *f) {
    size_t cap = 4096, n = 0, got;
    char *buf = malloc(cap);
    while (buf && (got = fread(buf + n, 1, cap - n - 1, f)) > 0) {
        n += got;
        if (n + 1 == cap) buf = realloc(buf, cap *= 2);
    }
    if (buf) buf[n] = 0;
    return buf;
}

int main(int argc, char **argv) {
    options o = {NULL, 1};
    const char *word = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-e") && i + 1 < argc) word = argv[++i];
        else if (!strcmp(argv[i], "--no-write")) o.write = 0;
        else if (argv[i][0] == '-' && argv[i][1]) {
            fprintf(stderr, "usage: modelita [--no-write] [FILE | -e WORD]\n");
            return 2;
        } else o.path = argv[i];
    }
    char *text;
    if (word) {
        text = malloc(strlen(word) + 1);
        if (text) strcpy(text, word);
        o.path = NULL;
    }
    else if (o.path) {
        FILE *f = fopen(o.path, "rb");
        if (!f) { fprintf(stderr, "can't read %s\n", o.path); return 2; }
        text = slurp(f);
        fclose(f);
    } else text = slurp(stdin);
    if (!text) { fputs("out of memory\n", stderr); return 2; }
    mdl_run_program(text, o.path, print, &o);
    free(text);
    return 0;
}
