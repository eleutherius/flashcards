/*
 * Картки — флеш-картки для вивчення слів на PocketBook (InkView).
 *
 * Колоди: файли .tsv/.txt у папці flashcards у корені пам'яті (або SD-карти).
 * Формат: термін<TAB>переклад[<TAB>приклад] — так само, як експорт з Quizlet.
 * Прогрес: flashcards/.progress/<файл>.progress
 */
#include <inkview.h>
#include <math.h>
#include <strings.h>

#include "deck.h"
#include "sample_deck.h"

#ifndef FC_ROOT
#define FC_ROOT "/mnt/ext1/flashcards"
#endif
#ifndef FC_ROOT_SD
#define FC_ROOT_SD "/mnt/ext2/flashcards"
#endif
#define FC_STATE FC_ROOT "/.progress"
#define SAMPLE_NAME "english-basics.tsv"

/* На кольоровому Kaleido акценти будуть синіми, на ч/б екрані — сірими. */
#define ACCENT 0x1E5BB8
#define MUTED  0x5A5A5A
#define SOFT   0x8A8A8A
#define LINE   0xBDBDBD
#define TRACK  0xE2E2E2

enum { ST_REGULAR = 0, ST_BOLD = 1, ST_ITALIC = 2 };
enum { SCR_DECKS, SCR_STUDY, SCR_DONE };

typedef struct { int x, y, w, h; } rect;

typedef struct {
    char path[1024];
    char name[256];
    char prog[1200];
    fc_stats st;
    int today_count;
} deck_entry;

#define MAX_DECKS 128
static deck_entry decks[MAX_DECKS];
static int ndecks, page, pages;

static int screen = SCR_DECKS;
static fc_deck deck;
static fc_session sess;
static int cur_deck = -1, flipped, session_size, today, advances;

static int set_reverse = 0, set_new_limit = 20, set_shuffle = 1;

static int W, H, U, M;

static rect r_back, r_menu, r_card, r_btn1, r_btn2, r_btn3, r_exit, r_pprev, r_pnext;
#define MAX_ROWS 32
static rect r_rows[MAX_ROWS];
static int row_deck[MAX_ROWS], nrows;

/* ------------------------------------------------------------------ */
/* Дрібниці                                                           */

static int hit(rect r, int x, int y)
{
    return r.w > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static rect mk(int x, int y, int w, int h) { rect r = { x, y, w, h }; return r; }

/* українська множина: 1 картка, 2 картки, 5 карток */
static const char *plural(int n, const char *one, const char *few, const char *many)
{
    int a = abs(n) % 100, b = a % 10;
    if (a >= 11 && a <= 14) return many;
    if (b == 1) return one;
    if (b >= 2 && b <= 4) return few;
    return many;
}

static void metrics(void)
{
    W = ScreenWidth();
    H = ScreenHeight();
    U = (W < H ? W : H) / 100;
    if (U < 4) U = 4;
    M = U * 5;
}

/* ------------------------------------------------------------------ */
/* Шрифти й текст                                                     */

typedef struct { int size, style; ifont *f; } font_slot;
#define FONT_SLOTS 24
static font_slot fonts[FONT_SLOTS];
static int font_next;

static ifont *get_font(int size, int style)
{
    for (int i = 0; i < FONT_SLOTS; i++)
        if (fonts[i].f && fonts[i].size == size && fonts[i].style == style)
            return fonts[i].f;
    font_slot *s = &fonts[font_next];
    font_next = (font_next + 1) % FONT_SLOTS;
    if (s->f) CloseFont(s->f);
    const char *name = style == ST_BOLD ? DEFAULTFONTB : style == ST_ITALIC ? DEFAULTFONTI : DEFAULTFONT;
    s->f = OpenFont(name, size, 1);
    s->size = size;
    s->style = style;
    return s->f;
}

static void free_fonts(void)
{
    for (int i = 0; i < FONT_SLOTS; i++)
        if (fonts[i].f) { CloseFont(fonts[i].f); fonts[i].f = NULL; }
}

static void use(int size, int style, int color)
{
    SetFont(get_font(size, style), color);
}

static void text(rect r, const char *s, int size, int style, int color, int flags)
{
    use(size, style, color);
    DrawTextRect(r.x, r.y, r.w, r.h, (char *)s, flags);
}

/* Найбільший розмір шрифту, з яким текст влазить у прямокутник. */
static int fit_text(rect r, const char *s, int maxsz, int minsz, int style, int color, int align)
{
    int sz = maxsz;
    for (;;) {
        if (sz < minsz) sz = minsz;
        use(sz, style, color);
        int th = TextRectHeight(r.w, (char *)s, align);
        if (th <= r.h || sz == minsz) {
            DrawTextRect(r.x, r.y, r.w, r.h, (char *)s, align | VALIGN_MIDDLE | (th > r.h ? DOTS : 0));
            return sz;
        }
        int next = sz * 9 / 10;
        sz = next < sz ? next : sz - 1;
    }
}

/* ------------------------------------------------------------------ */
/* Примітиви                                                          */

static void fill_round(rect r, int rad, int color)
{
    if (r.w <= 0 || r.h <= 0) return;
    if (rad * 2 > r.h) rad = r.h / 2;
    if (rad * 2 > r.w) rad = r.w / 2;
    if (r.h - 2 * rad > 0) FillArea(r.x, r.y + rad, r.w, r.h - 2 * rad, color);
    for (int i = 0; i < rad; i++) {
        double dy = rad - i - 0.5;
        int dx = rad - (int)(sqrt((double)rad * rad - dy * dy) + 0.5);
        FillArea(r.x + dx, r.y + i, r.w - 2 * dx, 1, color);
        FillArea(r.x + dx, r.y + r.h - 1 - i, r.w - 2 * dx, 1, color);
    }
}

static void frame_round(rect r, int rad, int t, int color, int bg)
{
    fill_round(r, rad, color);
    fill_round(mk(r.x + t, r.y + t, r.w - 2 * t, r.h - 2 * t), rad > t ? rad - t : 0, bg);
}

static void button(rect r, const char *label, int primary)
{
    int rad = U * 2;
    if (primary) {
        fill_round(r, rad, BLACK);
        text(r, label, U * 34 / 10, ST_BOLD, WHITE, ALIGN_CENTER | VALIGN_MIDDLE | DOTS);
    } else {
        frame_round(r, rad, 3, BLACK, WHITE);
        text(r, label, U * 34 / 10, ST_BOLD, BLACK, ALIGN_CENTER | VALIGN_MIDDLE | DOTS);
    }
}

static int header(const char *title, const char *left, const char *right)
{
    int hh = U * 10;
    FillArea(0, 0, W, hh, WHITE);
    int side = U * 24;
    r_back = mk(0, 0, 0, 0);
    r_menu = mk(0, 0, 0, 0);
    if (left) {
        r_back = mk(0, 0, side, hh);
        text(mk(M, 0, side - M, hh), left, U * 32 / 10, ST_REGULAR, BLACK, ALIGN_LEFT | VALIGN_MIDDLE);
    }
    if (right) {
        r_menu = mk(W - side, 0, side, hh);
        text(mk(W - side, 0, side - M, hh), right, U * 32 / 10, ST_REGULAR, BLACK, ALIGN_RIGHT | VALIGN_MIDDLE);
    }
    text(mk(side, 0, W - 2 * side, hh), title, U * 36 / 10, ST_BOLD, BLACK, ALIGN_CENTER | VALIGN_MIDDLE | DOTS);
    FillArea(0, hh - 2, W, 2, BLACK);
    return hh;
}

/* ------------------------------------------------------------------ */
/* Налаштування й колоди                                              */

static void ensure_dirs(void)
{
    mkdir(FC_ROOT, 0755);
    mkdir(FC_STATE, 0755);
}

static void load_settings(void)
{
    FILE *f = fopen(FC_STATE "/settings.ini", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        int v;
        if (sscanf(line, "reverse=%d", &v) == 1) set_reverse = v ? 1 : 0;
        else if (sscanf(line, "new_limit=%d", &v) == 1) set_new_limit = v < 0 ? 20 : v;
        else if (sscanf(line, "shuffle=%d", &v) == 1) set_shuffle = v ? 1 : 0;
    }
    fclose(f);
}

static void save_settings(void)
{
    FILE *f = fopen(FC_STATE "/settings.ini", "w");
    if (!f) return;
    fprintf(f, "reverse=%d\nnew_limit=%d\nshuffle=%d\n", set_reverse, set_new_limit, set_shuffle);
    fclose(f);
}

static void maybe_create_sample(void)
{
    char marker[512], path[512];
    snprintf(marker, sizeof marker, "%s/.sample_created", FC_STATE);
    if (access(marker, F_OK) == 0) return;
    snprintf(path, sizeof path, "%s/%s", FC_ROOT, SAMPLE_NAME);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(sample_deck_tsv, 1, sample_deck_tsv_len, f);
        fclose(f);
    }
    FILE *m = fopen(marker, "w");
    if (m) fclose(m);
}

static int is_deck_file(const char *n)
{
    if (n[0] == '.') return 0;
    const char *dot = strrchr(n, '.');
    if (!dot) return 0;
    return !strcasecmp(dot, ".tsv") || !strcasecmp(dot, ".txt") || !strcasecmp(dot, ".csv");
}

static int cmp_deck(const void *a, const void *b)
{
    return strcasecmp(((const deck_entry *)a)->name, ((const deck_entry *)b)->name);
}

static void scan_dir(const char *dir, const char *prefix)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && ndecks < MAX_DECKS) {
        if (!is_deck_file(e->d_name)) continue;
        deck_entry *de = &decks[ndecks];
        snprintf(de->path, sizeof de->path, "%s/%s", dir, e->d_name);
        snprintf(de->prog, sizeof de->prog, "%s/%s%s.progress", FC_STATE, prefix, e->d_name);
        snprintf(de->name, sizeof de->name, "%s", e->d_name);
        char *dot = strrchr(de->name, '.');
        if (dot) *dot = 0;
        for (char *p = de->name; *p; p++) if (*p == '_') *p = ' ';

        fc_deck tmp;
        if (fc_deck_load(&tmp, de->path) < 0) { fc_deck_free(&tmp); continue; }
        fc_progress_load(&tmp, de->prog);
        fc_deck_stats(&tmp, today, &de->st);
        int nw = de->st.fresh;
        if (set_new_limit > 0 && nw > set_new_limit) nw = set_new_limit;
        de->today_count = de->st.due + nw;
        fc_deck_free(&tmp);
        ndecks++;
    }
    closedir(d);
}

static void scan_decks(void)
{
    today = fc_today();
    ndecks = 0;
    scan_dir(FC_ROOT, "");
    scan_dir(FC_ROOT_SD, "sd-");
    qsort(decks, (size_t)ndecks, sizeof(deck_entry), cmp_deck);
}

/* ------------------------------------------------------------------ */
/* Екран: список колод                                                */

static void draw_decks(void)
{
    ClearScreen();
    r_back = r_menu = r_btn1 = r_btn2 = r_btn3 = r_card = mk(0, 0, 0, 0);

    int hh = U * 12;
    text(mk(M, 0, W - 2 * M - U * 20, hh), "Мої картки", U * 5, ST_BOLD, BLACK, ALIGN_LEFT | VALIGN_MIDDLE);
    r_exit = mk(W - U * 22, 0, U * 22, hh);
    text(mk(W - U * 22, 0, U * 22 - M, hh), "Вийти", U * 32 / 10, ST_REGULAR, MUTED, ALIGN_RIGHT | VALIGN_MIDDLE);
    r_menu = mk(W - U * 44, 0, U * 22, hh);
    text(mk(W - U * 44, 0, U * 20, hh), "Меню", U * 32 / 10, ST_REGULAR, BLACK, ALIGN_RIGHT | VALIGN_MIDDLE);

    int total = 0;
    for (int i = 0; i < ndecks; i++) total += decks[i].today_count;
    char sum[160];
    if (ndecks == 0)
        snprintf(sum, sizeof sum, "Колод поки немає");
    else if (total == 0)
        snprintf(sum, sizeof sum, "На сьогодні все вивчено");
    else
        snprintf(sum, sizeof sum, "Сьогодні до вивчення: %d %s", total, plural(total, "картка", "картки", "карток"));
    text(mk(M, hh - U, W - 2 * M, U * 6), sum, U * 28 / 10, ST_REGULAR, MUTED, ALIGN_LEFT | VALIGN_MIDDLE);

    int y0 = hh + U * 6;
    FillArea(M, y0, W - 2 * M, 2, BLACK);
    y0 += 2;

    int footer = U * 12;
    int rowh = U * 14;
    int per = (H - y0 - footer) / rowh;
    if (per < 1) per = 1;
    if (per > MAX_ROWS) per = MAX_ROWS;
    pages = ndecks ? (ndecks + per - 1) / per : 1;
    if (page >= pages) page = pages - 1;
    if (page < 0) page = 0;

    nrows = 0;
    if (ndecks == 0) {
        text(mk(M, y0 + U * 6, W - 2 * M, U * 40),
             "Скопіюйте файли колод у папку flashcards у пам'яті книжки.\n\n"
             "Формат — як експорт з Quizlet: слово, табуляція, переклад, "
             "по одній картці в рядку. Можна додати третю колонку з прикладом.",
             U * 3, ST_REGULAR, BLACK, ALIGN_LEFT | VALIGN_TOP);
    }

    for (int k = 0; k < per; k++) {
        int i = page * per + k;
        if (i >= ndecks) break;
        deck_entry *de = &decks[i];
        int y = y0 + k * rowh;
        r_rows[nrows] = mk(0, y, W, rowh);
        row_deck[nrows++] = i;

        int rightw = U * 22;
        text(mk(M, y + U * 2, W - 2 * M - rightw, U * 6), de->name, U * 38 / 10, ST_BOLD, BLACK,
             ALIGN_LEFT | VALIGN_MIDDLE | DOTS);
        char det[200];
        snprintf(det, sizeof det, "%d %s  ·  %d %s  ·  засвоєно %d",
                 de->st.total, plural(de->st.total, "картка", "картки", "карток"),
                 de->st.fresh, plural(de->st.fresh, "нова", "нові", "нових"),
                 de->st.learned);
        text(mk(M, y + U * 8, W - 2 * M - rightw, U * 4), det, U * 26 / 10, ST_REGULAR, MUTED,
             ALIGN_LEFT | VALIGN_MIDDLE | DOTS);

        rect rr = mk(W - M - rightw, y + U * 2, rightw, U * 7);
        if (de->today_count > 0) {
            char num[16];
            snprintf(num, sizeof num, "%d", de->today_count);
            text(rr, num, U * 55 / 10, ST_BOLD, BLACK, ALIGN_RIGHT | VALIGN_MIDDLE);
            text(mk(rr.x, y + U * 9, rightw, U * 3), "сьогодні", U * 24 / 10, ST_REGULAR, MUTED,
                 ALIGN_RIGHT | VALIGN_MIDDLE);
        } else {
            text(rr, "готово", U * 3, ST_REGULAR, SOFT, ALIGN_RIGHT | VALIGN_MIDDLE);
        }
        FillArea(M, y + rowh - 1, W - 2 * M, 1, LINE);
    }

    int fy = H - footer;
    r_pprev = r_pnext = mk(0, 0, 0, 0);
    if (pages > 1) {
        char pg[64];
        snprintf(pg, sizeof pg, "‹   %d / %d   ›", page + 1, pages);
        text(mk(0, fy, W, U * 7), pg, U * 34 / 10, ST_BOLD, BLACK, ALIGN_CENTER | VALIGN_MIDDLE);
        r_pprev = mk(0, fy - U * 2, W / 2, footer + U * 2);
        r_pnext = mk(W / 2, fy - U * 2, W / 2, footer + U * 2);
    }
    text(mk(M, H - U * 5, W - 2 * M, U * 4), "Колоди: папка flashcards  ·  формат TSV (експорт Quizlet)",
         U * 22 / 10, ST_REGULAR, SOFT, ALIGN_CENTER | VALIGN_MIDDLE);
}

/* ------------------------------------------------------------------ */
/* Екран: навчання                                                    */

static rect card_rect(void)
{
    int top = U * 10 + U * 7;
    int bottom = H - U * 21;
    return mk(M, top, W - 2 * M, bottom - top);
}

static void level_marks(int x_right, int y, int box)
{
    int sq = U * 14 / 10, step = U * 22 / 10;
    if (box == 0) {
        text(mk(x_right - U * 20, y - U, U * 20, sq + 2 * U), "нова", U * 24 / 10, ST_BOLD, BLACK,
             ALIGN_RIGHT | VALIGN_MIDDLE);
        return;
    }
    for (int i = 0; i < FC_MAX_BOX; i++) {
        int x = x_right - (FC_MAX_BOX - i) * step + (step - sq);
        if (i < box) FillArea(x, y, sq, sq, ACCENT);
        else { FillArea(x, y, sq, sq, LINE); FillArea(x + 2, y + 2, sq - 4, sq - 4, WHITE); }
    }
}

static void draw_study(void)
{
    ClearScreen();
    r_exit = r_pprev = r_pnext = r_btn3 = mk(0, 0, 0, 0);
    nrows = 0;

    int hh = header(decks[cur_deck].name, "‹ Колоди", "Меню");

    int rem = fc_session_remaining(&sess);
    int all = sess.done + rem;
    FillArea(0, hh, W, U, TRACK);
    if (all > 0) FillArea(0, hh, W * sess.done / all, U, ACCENT);

    char st[200];
    snprintf(st, sizeof st, "Залишилось %d  ·  Вивчено %d  ·  Помилок %d", rem, sess.done, sess.mistakes);
    text(mk(M, hh + U, W - 2 * M, U * 6), st, U * 26 / 10, ST_REGULAR, MUTED, ALIGN_CENTER | VALIGN_MIDDLE);

    int ci = fc_session_current(&sess);
    if (ci < 0) return;
    fc_card *c = &deck.cards[ci];
    const char *q = set_reverse ? c->back : c->front;
    const char *a = set_reverse ? c->front : c->back;
    const char *ql = set_reverse ? "ПЕРЕКЛАД" : "СЛОВО";
    const char *al = set_reverse ? "СЛОВО" : "ПЕРЕКЛАД";

    r_card = card_rect();
    frame_round(r_card, U * 3, 3, BLACK, WHITE);
    int P = U * 4;
    int ix = r_card.x + P, iw = r_card.w - 2 * P;
    int iy = r_card.y + P, ih = r_card.h - 2 * P;

    level_marks(ix + iw, iy + U / 2, c->box);

    if (!flipped) {
        text(mk(ix, iy, iw / 2, U * 3), ql, U * 24 / 10, ST_BOLD, SOFT, ALIGN_LEFT | VALIGN_MIDDLE);
        fit_text(mk(ix, iy + U * 6, iw, ih - U * 12), q, U * 9, U * 3, ST_BOLD, BLACK, ALIGN_CENTER);
        text(mk(ix, iy + ih - U * 4, iw, U * 4), "Торкніться картки, щоб перевернути",
             U * 24 / 10, ST_REGULAR, SOFT, ALIGN_CENTER | VALIGN_MIDDLE);
    } else {
        int top_h = ih * 30 / 100;
        text(mk(ix, iy, iw / 2, U * 3), ql, U * 24 / 10, ST_BOLD, SOFT, ALIGN_LEFT | VALIGN_MIDDLE);
        fit_text(mk(ix, iy + U * 4, iw, top_h - U * 5), q, U * 55 / 10, U * 26 / 10, ST_BOLD, MUTED, ALIGN_CENTER);

        int dy = iy + top_h;
        FillArea(ix, dy, iw, 2, LINE);

        const char *ex = set_reverse ? NULL : c->example;
        int ex_h = ex ? ih * 24 / 100 : 0;
        text(mk(ix, dy + U * 2, iw, U * 3), al, U * 24 / 10, ST_BOLD, SOFT, ALIGN_LEFT | VALIGN_MIDDLE);
        fit_text(mk(ix, dy + U * 6, iw, iy + ih - ex_h - (dy + U * 6)), a, U * 75 / 10, U * 3,
                 ST_BOLD, BLACK, ALIGN_CENTER);
        if (ex) {
            int ey = iy + ih - ex_h;
            FillArea(ix + iw / 3, ey, iw / 3, 1, LINE);
            fit_text(mk(ix, ey + U, iw, ex_h - U), ex, U * 32 / 10, U * 22 / 10, ST_ITALIC, MUTED, ALIGN_CENTER);
        }
    }

    int by = r_card.y + r_card.h + U * 3, bh = U * 12;
    if (!flipped) {
        r_btn1 = mk(M, by, W - 2 * M, bh);
        r_btn2 = mk(0, 0, 0, 0);
        button(r_btn1, "Показати відповідь", 1);
    } else {
        int gap = U * 3, bw = (W - 2 * M - gap) / 2;
        r_btn1 = mk(M, by, bw, bh);
        r_btn2 = mk(M + bw + gap, by, bw, bh);
        button(r_btn1, "Не знаю", 0);
        button(r_btn2, "Знаю", 1);
    }
    text(mk(M, by + bh + U, W - 2 * M, U * 4),
         flipped ? "Кнопки гортання:  ‹ не знаю   ·   знаю ›" : "Будь-яка кнопка гортання — перевернути",
         U * 23 / 10, ST_REGULAR, SOFT, ALIGN_CENTER | VALIGN_MIDDLE);
}

/* ------------------------------------------------------------------ */
/* Екран: підсумок                                                    */

static void days_text(char *buf, size_t n, int days)
{
    if (days <= 0) snprintf(buf, n, "сьогодні");
    else if (days == 1) snprintf(buf, n, "завтра");
    else snprintf(buf, n, "через %d %s", days, plural(days, "день", "дні", "днів"));
}

static void draw_done(void)
{
    ClearScreen();
    r_exit = r_pprev = r_pnext = r_card = mk(0, 0, 0, 0);
    nrows = 0;
    header(decks[cur_deck].name, "‹ Колоди", "Меню");

    fc_stats st;
    fc_deck_stats(&deck, today, &st);
    int y = U * 20;
    char line[200], when[64];

    if (session_size == 0) {
        text(mk(M, y, W - 2 * M, U * 10), "На сьогодні все!", U * 6, ST_BOLD, BLACK, ALIGN_CENTER | VALIGN_MIDDLE);
        int nd = fc_next_due(&deck);
        if (nd < 0) snprintf(line, sizeof line, "У колоді немає карток для повторення.");
        else {
            days_text(when, sizeof when, nd - today);
            int cnt = fc_due_by(&deck, nd);
            snprintf(line, sizeof line, "Наступне повторення %s: %d %s", when, cnt, plural(cnt, "картка", "картки", "карток"));
        }
        text(mk(M, y + U * 12, W - 2 * M, U * 8), line, U * 32 / 10, ST_REGULAR, MUTED, ALIGN_CENTER | VALIGN_MIDDLE);
    } else {
        int land = W > H;
        if (land) y = U * 14;
        int big_h = land ? U * 12 : U * 16;
        text(mk(M, y, W - 2 * M, U * 8), sess.cram ? "Повторення завершено" : "Сесію завершено",
             U * 5, ST_BOLD, BLACK, ALIGN_CENTER | VALIGN_MIDDLE);
        int pct = sess.unique ? sess.first_try * 100 / sess.unique : 0;
        snprintf(line, sizeof line, "%d%%", pct);
        text(mk(M, y + U * 9, W - 2 * M, big_h), line, land ? U * 11 : U * 14, ST_BOLD, BLACK,
             ALIGN_CENTER | VALIGN_MIDDLE);
        text(mk(M, y + U * 9 + big_h, W - 2 * M, U * 4), "правильно з першої спроби", U * 28 / 10,
             ST_REGULAR, MUTED, ALIGN_CENTER | VALIGN_MIDDLE);

        int ly = y + U * 9 + big_h + U * (land ? 7 : 10);
        int tomorrow = fc_due_by(&deck, today + 1);
        char rows[4][160];
        snprintf(rows[0], sizeof rows[0], "Карток у сесії: %d", sess.unique);
        snprintf(rows[1], sizeof rows[1], "Помилок: %d", sess.mistakes);
        snprintf(rows[2], sizeof rows[2], "Засвоєно в колоді: %d з %d", st.learned, st.total);
        snprintf(rows[3], sizeof rows[3], "Завтра до повторення: %d", tomorrow);
        for (int i = 0; i < 4; i++)
            text(mk(M, ly + i * U * 6, W - 2 * M, U * 6), rows[i], U * 32 / 10, ST_REGULAR, BLACK,
                 ALIGN_CENTER | VALIGN_MIDDLE);
    }

    int bh = U * 12, gap = U * 3;
    int nw = (set_new_limit > 0 && st.fresh > set_new_limit) ? set_new_limit : st.fresh;
    snprintf(line, sizeof line, W > H ? "Нові слова (%d)" : "Вчити нові слова (%d)", nw);
    r_btn3 = mk(0, 0, 0, 0);

    if (W > H) {
        /* альбомна орієнтація: кнопки в один ряд */
        int n = st.fresh > 0 ? 3 : 2;
        int bw = (W - 2 * M - (n - 1) * gap) / n, by = H - M - bh, x = M;
        if (n == 3) { r_btn3 = mk(x, by, bw, bh); button(r_btn3, line, 0); x += bw + gap; }
        r_btn1 = mk(x, by, bw, bh);
        button(r_btn1, "Повторити всі", 0);
        x += bw + gap;
        r_btn2 = mk(x, by, bw, bh);
        button(r_btn2, "До списку колод", 1);
        return;
    }

    int by = H - M - bh;
    r_btn2 = mk(M, by, W - 2 * M, bh);
    button(r_btn2, "До списку колод", 1);
    by -= bh + gap;
    r_btn1 = mk(M, by, W - 2 * M, bh);
    button(r_btn1, "Повторити всі картки", 0);
    if (st.fresh > 0) {
        by -= bh + gap;
        r_btn3 = mk(M, by, W - 2 * M, bh);
        button(r_btn3, line, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Перехід між станами                                                */

static void redraw(void)
{
    if (screen == SCR_DECKS) draw_decks();
    else if (screen == SCR_STUDY) draw_study();
    else draw_done();
}

static void redraw_full(void)
{
    redraw();
    FullUpdate();
}

static void save_progress(void)
{
    if (cur_deck >= 0 && deck.n > 0) fc_progress_save(&deck, decks[cur_deck].prog);
}

static void start_session(int cram, int only_new)
{
    fc_session_free(&sess);
    today = fc_today();
    if (only_new) {
        /* "Вчити нові": лише нові картки, без тих, що на повторення */
        fc_session_start(&sess, &deck, -1, set_new_limit, set_shuffle, 0, (unsigned)time(NULL));
    } else {
        fc_session_start(&sess, &deck, today, set_new_limit, set_shuffle, cram, (unsigned)time(NULL));
    }
    session_size = sess.len;
    flipped = 0;
    advances = 0;
    screen = session_size > 0 ? SCR_STUDY : SCR_DONE;
    redraw_full();
}

static void open_deck(int i)
{
    fc_session_free(&sess);
    fc_deck_free(&deck);
    if (fc_deck_load(&deck, decks[i].path) <= 0) {
        fc_deck_free(&deck);
        Message(ICON_WARNING, "Порожня колода",
                "У файлі не знайдено карток. Кожен рядок: слово, табуляція, переклад.", 4000);
        return;
    }
    fc_progress_load(&deck, decks[i].prog);
    cur_deck = i;
    start_session(0, 0);
}

static void go_decks(void)
{
    save_progress();
    fc_session_free(&sess);
    fc_deck_free(&deck);
    cur_deck = -1;
    scan_decks();
    screen = SCR_DECKS;
    redraw_full();
}

static void do_flip(void)
{
    flipped = !flipped;
    draw_study();
    PartialUpdate(0, r_card.y, W, H - r_card.y);
}

static void do_answer(int known)
{
    if (!flipped) { do_flip(); return; }
    fc_session_answer(&sess, &deck, today, known);
    save_progress();
    flipped = 0;
    advances++;
    if (fc_session_current(&sess) < 0) {
        screen = SCR_DONE;
        redraw_full();
        return;
    }
    draw_study();
    /* повне оновлення раз на кілька карток прибирає "привиди" e-ink */
    if (advances % 8 == 0) FullUpdate();
    else PartialUpdate(0, 0, W, H);
}

/* ------------------------------------------------------------------ */
/* Меню                                                               */

static imenu menu_items[12];
static char mi_dir[96], mi_new[96], mi_shuf[96];

static void reset_handler(int button)
{
    if (button == 1 && cur_deck >= 0) {
        fc_progress_reset(&deck);
        save_progress();
        start_session(0, 0);
    } else {
        redraw_full();
    }
}

static void menu_handler(int idx)
{
    static const int limits[] = { 10, 20, 30, 50, 0 };
    switch (idx) {
    case 1:
        set_reverse = !set_reverse;
        save_settings();
        break;
    case 2: {
        int k = 0;
        for (int i = 0; i < 5; i++) if (limits[i] == set_new_limit) k = i;
        set_new_limit = limits[(k + 1) % 5];
        save_settings();
        if (screen == SCR_DECKS) scan_decks();
        break;
    }
    case 3:
        set_shuffle = !set_shuffle;
        save_settings();
        break;
    case 4:
        Dialog(ICON_QUESTION, "Скинути прогрес?",
               "Усі картки цієї колоди знову стануть новими. Цю дію не можна скасувати.",
               "Скинути", "Скасувати", reset_handler);
        return;
    case 5:
        save_progress();
        CloseApp();
        return;
    default:
        break;
    }
    redraw_full();
}

static void open_menu(void)
{
    char lim[16];
    if (set_new_limit > 0) snprintf(lim, sizeof lim, "%d", set_new_limit);
    else snprintf(lim, sizeof lim, "усі");
    snprintf(mi_dir, sizeof mi_dir, "Напрям: %s", set_reverse ? "переклад → слово" : "слово → переклад");
    snprintf(mi_new, sizeof mi_new, "Нових слів за сесію: %s", lim);
    snprintf(mi_shuf, sizeof mi_shuf, "Перемішувати: %s", set_shuffle ? "так" : "ні");

    int k = 0;
    menu_items[k++] = (imenu){ ITEM_HEADER, 0, (char *)"Налаштування", NULL };
    menu_items[k++] = (imenu){ ITEM_ACTIVE, 1, mi_dir, NULL };
    menu_items[k++] = (imenu){ ITEM_ACTIVE, 2, mi_new, NULL };
    menu_items[k++] = (imenu){ ITEM_ACTIVE, 3, mi_shuf, NULL };
    menu_items[k++] = (imenu){ ITEM_SEPARATOR, 0, NULL, NULL };
    if (cur_deck >= 0) menu_items[k++] = (imenu){ ITEM_ACTIVE, 4, (char *)"Скинути прогрес колоди", NULL };
    menu_items[k++] = (imenu){ ITEM_ACTIVE, 5, (char *)"Вийти з програми", NULL };
    menu_items[k++] = (imenu){ 0, 0, NULL, NULL };
    OpenMenu(menu_items, 0, W - M, U * 10, menu_handler);
}

/* ------------------------------------------------------------------ */
/* Події                                                              */

static int is_prev_key(int k)
{
    return k == IV_KEY_PREV || k == IV_KEY_LEFT || k == IV_KEY_UP || k == IV_KEY_PREV2 || k == IV_KEY_MINUS;
}

static int is_next_key(int k)
{
    return k == IV_KEY_NEXT || k == IV_KEY_RIGHT || k == IV_KEY_DOWN || k == IV_KEY_NEXT2 || k == IV_KEY_PLUS;
}

static void on_key(int k)
{
    if (k == IV_KEY_MENU) { open_menu(); return; }
    if (k == IV_KEY_HOME) { save_progress(); CloseApp(); return; }

    if (screen == SCR_DECKS) {
        if (k == IV_KEY_BACK) { CloseApp(); return; }
        if (is_prev_key(k) && page > 0) { page--; redraw_full(); }
        else if (is_next_key(k) && page < pages - 1) { page++; redraw_full(); }
        else if (k == IV_KEY_OK && nrows > 0) open_deck(row_deck[0]);
    } else if (screen == SCR_STUDY) {
        if (k == IV_KEY_BACK) { go_decks(); return; }
        if (!flipped) {
            if (is_prev_key(k) || is_next_key(k) || k == IV_KEY_OK) do_flip();
        } else {
            if (is_prev_key(k)) do_answer(0);
            else if (is_next_key(k) || k == IV_KEY_OK) do_answer(1);
        }
    } else {
        if (k == IV_KEY_BACK || k == IV_KEY_OK || is_prev_key(k) || is_next_key(k)) go_decks();
    }
}

static void on_tap(int x, int y)
{
    if (hit(r_menu, x, y)) { open_menu(); return; }

    if (screen == SCR_DECKS) {
        if (hit(r_exit, x, y)) { CloseApp(); return; }
        for (int i = 0; i < nrows; i++)
            if (hit(r_rows[i], x, y)) { open_deck(row_deck[i]); return; }
        if (hit(r_pprev, x, y) && page > 0) { page--; redraw_full(); }
        else if (hit(r_pnext, x, y) && page < pages - 1) { page++; redraw_full(); }
    } else if (screen == SCR_STUDY) {
        if (hit(r_back, x, y)) { go_decks(); return; }
        if (!flipped) {
            if (hit(r_card, x, y) || hit(r_btn1, x, y)) do_flip();
        } else {
            if (hit(r_btn1, x, y)) do_answer(0);
            else if (hit(r_btn2, x, y)) do_answer(1);
            else if (hit(r_card, x, y)) do_flip();
        }
    } else {
        if (hit(r_back, x, y) || hit(r_btn2, x, y)) go_decks();
        else if (hit(r_btn1, x, y)) start_session(1, 0);
        else if (hit(r_btn3, x, y)) start_session(0, 1);
    }
}

static int main_handler(int type, int par1, int par2)
{
    switch (type) {
    case EVT_INIT:
        SetPanelType(0);
        metrics();
        ensure_dirs();
        load_settings();
        maybe_create_sample();
        scan_decks();
        break;
    case EVT_SHOW:
    case EVT_FOREGROUND:
        metrics();
        redraw_full();
        break;
    case EVT_ORIENTATION:
        SetOrientation(par1);
        metrics();
        redraw_full();
        break;
    case EVT_KEYPRESS:
        on_key(par1);
        return 1;
    case EVT_POINTERUP:
        on_tap(par1, par2);
        return 1;
    case EVT_EXIT:
        save_progress();
        fc_session_free(&sess);
        fc_deck_free(&deck);
        free_fonts();
        break;
    default:
        break;
    }
    return 0;
}

int main(void)
{
    InkViewMain(main_handler);
    return 0;
}
