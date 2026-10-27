#define _DEFAULT_SOURCE
#include "deck.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Допоміжне                                                          */

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

/* "\n" у тексті -> справжній перенос рядка (для багаторядкових визначень) */
static void unescape(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (r[0] == '\\' && r[1] == 'n') { *w++ = '\n'; r++; }
        else *w++ = *r;
    }
    *w = 0;
}

static int push_card(fc_deck *d, char *front, char *back, char *example)
{
    if (d->n == d->cap) {
        int nc = d->cap ? d->cap * 2 : 64;
        fc_card *nn = realloc(d->cards, (size_t)nc * sizeof(fc_card));
        if (!nn) return -1;
        d->cards = nn;
        d->cap = nc;
    }
    fc_card *c = &d->cards[d->n++];
    c->front = front;
    c->back = back;
    c->example = (example && *example) ? example : NULL;
    c->box = 0;
    c->due = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Колода                                                             */

int fc_deck_load(fc_deck *d, const char *path)
{
    memset(d, 0, sizeof(*d));
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 32L * 1024 * 1024) { fclose(f); return -1; }
    d->buf = malloc((size_t)sz + 1);
    if (!d->buf) { fclose(f); return -1; }
    size_t got = fread(d->buf, 1, (size_t)sz, f);
    fclose(f);
    d->buf[got] = 0;

    char *p = d->buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;

    while (p && *p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        char *line = p;
        p = nl ? nl + 1 : NULL;

        char *l = trim(line);
        if (!*l || *l == '#') continue;

        char *front = l, *back = NULL, *example = NULL;
        char *sep;
        if ((sep = strchr(l, '\t'))) {
            *sep = 0;
            back = sep + 1;
            char *sep2 = strchr(back, '\t');
            if (sep2) {
                *sep2 = 0;
                example = sep2 + 1;
                char *sep3 = strchr(example, '\t');
                if (sep3) *sep3 = 0;
            }
        } else if ((sep = strchr(l, ';'))) {
            *sep = 0;
            back = sep + 1;
        } else if ((sep = strstr(l, " - "))) {
            *sep = 0;
            back = sep + 3;
        } else {
            continue;
        }
        front = trim(front);
        back = trim(back);
        if (example) example = trim(example);
        if (!*front || !*back) continue;
        unescape(back);
        if (example) unescape(example);
        if (push_card(d, front, back, example) < 0) break;
    }
    return d->n;
}

void fc_deck_free(fc_deck *d)
{
    free(d->cards);
    free(d->buf);
    memset(d, 0, sizeof(*d));
}

/* ------------------------------------------------------------------ */
/* Прогрес                                                            */

static unsigned hash_str(const char *s)
{
    unsigned h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

int fc_progress_load(fc_deck *d, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f || d->n == 0) { if (f) fclose(f); return 0; }

    unsigned tsz = 1;
    while (tsz < (unsigned)d->n * 2) tsz <<= 1;
    int *table = malloc(tsz * sizeof(int));
    if (!table) { fclose(f); return 0; }
    for (unsigned i = 0; i < tsz; i++) table[i] = -1;
    for (int i = 0; i < d->n; i++) {
        unsigned h = hash_str(d->cards[i].front) & (tsz - 1);
        while (table[h] >= 0) h = (h + 1) & (tsz - 1);
        table[h] = i;
    }

    char line[4096];
    int matched = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        char *e = line + strlen(line);
        while (e > line && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
        char *t1 = strchr(line, '\t');
        if (!t1) continue;
        char *t2 = strchr(t1 + 1, '\t');
        if (!t2) continue;
        *t1 = 0; *t2 = 0;
        int box = atoi(line), due = atoi(t1 + 1);
        const char *front = t2 + 1;
        if (box < 0 || box > FC_MAX_BOX) continue;
        unsigned h = hash_str(front) & (tsz - 1);
        while (table[h] >= 0) {
            fc_card *c = &d->cards[table[h]];
            /* однакові терміни: заповнюємо першу ще не заповнену картку */
            if (strcmp(c->front, front) == 0 && c->box == 0 && c->due == 0) {
                c->box = box;
                c->due = due;
                matched++;
                break;
            }
            h = (h + 1) & (tsz - 1);
        }
    }
    free(table);
    fclose(f);
    return matched;
}

int fc_progress_save(const fc_deck *d, const char *path)
{
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    fprintf(f, "# flashcards progress v1: box<TAB>due_day<TAB>term\n");
    for (int i = 0; i < d->n; i++) {
        const fc_card *c = &d->cards[i];
        if (c->box == 0 && c->due == 0) continue;
        fprintf(f, "%d\t%d\t%s\n", c->box, c->due, c->front);
    }
    if (fflush(f) != 0) { fclose(f); remove(tmp); return -1; }
    fclose(f);
    if (rename(tmp, path) != 0) { remove(tmp); return -1; }
    return 0;
}

void fc_progress_reset(fc_deck *d)
{
    for (int i = 0; i < d->n; i++) { d->cards[i].box = 0; d->cards[i].due = 0; }
}

int fc_interval(int box)
{
    static const int iv[FC_MAX_BOX + 1] = { 0, 1, 2, 4, 8, 16, 32 };
    if (box < 0) box = 0;
    if (box > FC_MAX_BOX) box = FC_MAX_BOX;
    return iv[box];
}

void fc_deck_stats(const fc_deck *d, int today, fc_stats *s)
{
    memset(s, 0, sizeof(*s));
    s->total = d->n;
    for (int i = 0; i < d->n; i++) {
        const fc_card *c = &d->cards[i];
        if (c->box == 0) s->fresh++;
        else if (c->due <= today) s->due++;
        if (c->box >= 4) s->learned++;
    }
}

int fc_due_by(const fc_deck *d, int day)
{
    int n = 0;
    for (int i = 0; i < d->n; i++)
        if (d->cards[i].box > 0 && d->cards[i].due <= day) n++;
    return n;
}

int fc_next_due(const fc_deck *d)
{
    int best = -1;
    for (int i = 0; i < d->n; i++)
        if (d->cards[i].box > 0 && (best < 0 || d->cards[i].due < best))
            best = d->cards[i].due;
    return best;
}

int fc_today(void)
{
    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);
    /* день змінюється о 03:00, щоб пізні заняття зараховувались "сьогодні" */
    long local = (long)t + lt.tm_gmtoff - 3 * 3600;
    return (int)(local / 86400);
}

/* ------------------------------------------------------------------ */
/* Сесія                                                              */

static unsigned rnd(unsigned *st)
{
    *st = *st * 1103515245u + 12345u;
    return (*st >> 16) & 0x7fff;
}

static void shuffle(int *a, int n, unsigned *st)
{
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(((rnd(st) << 15) | rnd(st)) % (unsigned)(i + 1));
        int t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

static const fc_deck *g_sort_deck;
static int cmp_due(const void *a, const void *b)
{
    const fc_card *x = &g_sort_deck->cards[*(const int *)a];
    const fc_card *y = &g_sort_deck->cards[*(const int *)b];
    if (x->due != y->due) return x->due < y->due ? -1 : 1;
    return *(const int *)a - *(const int *)b;
}

int fc_session_start(fc_session *s, const fc_deck *d, int today,
                     int new_limit, int shuffle_on, int cram, unsigned seed)
{
    memset(s, 0, sizeof(*s));
    s->rng = seed ? seed : 1;
    s->cram = cram;
    s->cap = d->n + 16;
    s->q = malloc((size_t)s->cap * sizeof(int));
    s->failed = calloc((size_t)(d->n ? d->n : 1), 1);
    if (!s->q || !s->failed) { fc_session_free(s); return 0; }

    if (cram) {
        for (int i = 0; i < d->n; i++) s->q[s->len++] = i;
        if (shuffle_on) shuffle(s->q, s->len, &s->rng);
    } else {
        for (int i = 0; i < d->n; i++)
            if (d->cards[i].box > 0 && d->cards[i].due <= today) s->q[s->len++] = i;
        int ndue = s->len;
        if (shuffle_on) shuffle(s->q, ndue, &s->rng);
        else { g_sort_deck = d; qsort(s->q, (size_t)ndue, sizeof(int), cmp_due); }

        int added = 0;
        for (int i = 0; i < d->n; i++) {
            if (d->cards[i].box != 0) continue;
            if (new_limit > 0 && added >= new_limit) break;
            s->q[s->len++] = i;
            added++;
        }
        if (shuffle_on) shuffle(s->q + ndue, added, &s->rng);
    }
    s->unique = s->len;
    return s->len;
}

int fc_session_current(const fc_session *s)
{
    return (s->q && s->pos < s->len) ? s->q[s->pos] : -1;
}

int fc_session_remaining(const fc_session *s)
{
    return s->len - s->pos;
}

#define FC_REQUEUE_GAP 4

void fc_session_answer(fc_session *s, fc_deck *d, int today, int known)
{
    int cur = fc_session_current(s);
    if (cur < 0) return;
    fc_card *c = &d->cards[cur];
    s->pos++;

    if (known) {
        if (s->failed[cur]) {
            c->box = 1;
            c->due = today + fc_interval(1);
        } else {
            s->first_try++;
            if (!s->cram) {
                if (c->box < FC_MAX_BOX) c->box++;
                c->due = today + fc_interval(c->box);
            }
        }
        s->done++;
        return;
    }

    /* "Не знаю": картка падає на рівень 1 і повертається через кілька карток */
    s->failed[cur] = 1;
    s->mistakes++;
    c->box = 1;
    c->due = today;

    if (s->len == s->cap) {
        int nc = s->cap * 2;
        int *nq = realloc(s->q, (size_t)nc * sizeof(int));
        if (!nq) return;
        s->q = nq;
        s->cap = nc;
    }
    int at = s->pos + FC_REQUEUE_GAP;
    if (at > s->len) at = s->len;
    memmove(&s->q[at + 1], &s->q[at], (size_t)(s->len - at) * sizeof(int));
    s->q[at] = cur;
    s->len++;
}

void fc_session_free(fc_session *s)
{
    free(s->q);
    free(s->failed);
    memset(s, 0, sizeof(*s));
}
