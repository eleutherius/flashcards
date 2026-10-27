/*
 * Хостовий мок InkView: малює в RGB-буфер через FreeType і зберігає PNG.
 * Потрібен лише для перегляду макета на ПК — на книжці не використовується.
 *
 * Сценарій подій задається змінною MOCK_SCRIPT, команди через ';':
 *   INIT | SHOW | TAP x y | KEY code | MENU idx | DIALOG btn | SHOT file.png
 * MOCK_W / MOCK_H — розмір екрана (типово 1404x1872, як у InkPad Color).
 */
#include <inkview.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <png.h>

static int SW = 1404, SH = 1872;
static unsigned char *fb;
static FT_Library ft;
static ifont *cur_font;
static int cur_color;
static iv_menuhandler menu_h;
static iv_dialoghandler dialog_h;
static int closed;

typedef struct { ifont *f; FT_Face face; } face_map;
static face_map faces[64];

static FT_Face face_of(const ifont *f)
{
    for (int i = 0; i < 64; i++) if (faces[i].f == f) return faces[i].face;
    return NULL;
}

char *iv_get_default_font(FONT_TYPE t)
{
    switch (t) {
    case FONT_BOLD: return (char *)"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
    case FONT_ITALIC: return (char *)"/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf";
    default: return (char *)"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    }
}

ifont *OpenFont(const char *name, int size, int aa)
{
    ifont *f = calloc(1, sizeof(ifont));
    f->name = strdup(name);
    f->size = size;
    FT_Face face;
    if (FT_New_Face(ft, name, 0, &face)) { fprintf(stderr, "font %s?\n", name); exit(1); }
    FT_Set_Pixel_Sizes(face, 0, (FT_UInt)size);
    for (int i = 0; i < 64; i++) if (!faces[i].f) { faces[i].f = f; faces[i].face = face; break; }
    return f;
}

void CloseFont(ifont *f)
{
    for (int i = 0; i < 64; i++)
        if (faces[i].f == f) { FT_Done_Face(faces[i].face); faces[i].f = NULL; }
    free(f->name);
    free(f);
}

void SetFont(const ifont *font, int color) { cur_font = (ifont *)font; cur_color = color; }

int ScreenWidth() { return SW; }
int ScreenHeight() { return SH; }
void SetOrientation(int n) {}
void SetPanelType(int t) {}
void FullUpdate() { printf("  [FullUpdate]\n"); }
void SoftUpdate() {}
void PartialUpdate(int x, int y, int w, int h) { printf("  [PartialUpdate %d,%d %dx%d]\n", x, y, w, h); }
void CloseApp() { printf("  [CloseApp]\n"); closed = 1; }
void Message(int icon, const char *title, const char *text, int timeout) { printf("  [Message] %s: %s\n", title, text); }

void Dialog(int icon, const char *title, const char *text, const char *b1, const char *b2, iv_dialoghandler h)
{
    printf("  [Dialog] %s | %s | [%s] [%s]\n", title, text, b1, b2 ? b2 : "");
    dialog_h = h;
}

void OpenMenu(imenu *menu, int pos, int x, int y, iv_menuhandler h)
{
    printf("  [Menu]\n");
    for (imenu *m = menu; m->type; m++)
        printf("    %d: %s\n", m->index, m->text ? m->text : "------");
    menu_h = h;
}

static void put(int x, int y, int color, int alpha)
{
    if (x < 0 || y < 0 || x >= SW || y >= SH || alpha <= 0) return;
    unsigned char *p = fb + ((size_t)y * SW + x) * 3;
    int c[3] = { (color >> 16) & 255, (color >> 8) & 255, color & 255 };
    for (int i = 0; i < 3; i++) p[i] = (unsigned char)((p[i] * (255 - alpha) + c[i] * alpha) / 255);
}

void FillArea(int x, int y, int w, int h, int color)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) put(i, j, color, 255);
}

void DrawRect(int x, int y, int w, int h, int color)
{
    FillArea(x, y, w, 1, color); FillArea(x, y + h - 1, w, 1, color);
    FillArea(x, y, 1, h, color); FillArea(x + w - 1, y, 1, h, color);
}

void ClearScreen() { memset(fb, 255, (size_t)SW * SH * 3); }

/* --- текст --- */

static unsigned next_cp(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    unsigned c = *p++;
    if (c >= 0xF0) { c = ((c & 7) << 18) | ((p[0] & 63) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); p += 3; }
    else if (c >= 0xE0) { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
    else if (c >= 0xC0) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
    *s = (const char *)p;
    return c;
}

static int adv(FT_Face f, unsigned cp)
{
    if (FT_Load_Char(f, cp, FT_LOAD_DEFAULT)) return 0;
    return (int)(f->glyph->advance.x >> 6);
}

static int width_of(FT_Face f, const char *s, const char *e)
{
    int w = 0;
    while (s < e && *s) w += adv(f, next_cp(&s));
    return w;
}

#define MAXL 64
typedef struct { const char *s, *e; } span;

static int layout(FT_Face f, int w, const char *text, span *out)
{
    int n = 0;
    const char *p = text;
    while (*p && n < MAXL) {
        const char *ls = p, *last_break = NULL, *q = p;
        while (*q && *q != '\n') {
            const char *nq = q;
            next_cp(&nq);
            if (width_of(f, ls, nq) > w && q > ls) break;
            if (*q == ' ') last_break = q;
            q = nq;
        }
        const char *le = q;
        if (*q && *q != '\n' && last_break) le = last_break;
        out[n].s = ls; out[n].e = le; n++;
        p = le;
        if (*p == '\n' || *p == ' ') p++;
    }
    return n;
}

int TextRectHeight(int width, const char *s, int flags)
{
    FT_Face f = face_of(cur_font);
    span l[MAXL];
    int n = layout(f, width, s, l);
    return n * (cur_font->size * 6 / 5);
}

int StringWidth(const char *s) { return width_of(face_of(cur_font), s, s + strlen(s)); }

static void draw_run(FT_Face f, int x, int base, const char *s, const char *e)
{
    while (s < e && *s) {
        unsigned cp = next_cp(&s);
        if (FT_Load_Char(f, cp, FT_LOAD_RENDER)) continue;
        FT_GlyphSlot g = f->glyph;
        for (unsigned r = 0; r < g->bitmap.rows; r++)
            for (unsigned c = 0; c < g->bitmap.width; c++)
                put(x + g->bitmap_left + (int)c, base - g->bitmap_top + (int)r, cur_color,
                    g->bitmap.buffer[r * g->bitmap.pitch + c]);
        x += (int)(g->advance.x >> 6);
    }
}

char *DrawTextRect(int x, int y, int w, int h, const char *s, int flags)
{
    FT_Face f = face_of(cur_font);
    int lh = cur_font->size * 6 / 5;
    span l[MAXL];
    int n = layout(f, w, s, l);
    int maxl = h / lh;
    if (maxl < 1) maxl = 1;
    int trunc = 0;
    if (n > maxl) { n = maxl; trunc = 1; }
    int th = n * lh, ty = y;
    if (flags & VALIGN_MIDDLE) ty = y + (h - th) / 2;
    else if (flags & VALIGN_BOTTOM) ty = y + h - th;
    for (int i = 0; i < n; i++) {
        char buf[2048];
        int len = (int)(l[i].e - l[i].s);
        if (len > 2000) len = 2000;
        memcpy(buf, l[i].s, (size_t)len);
        buf[len] = 0;
        if (trunc && i == n - 1 && (flags & DOTS)) strcat(buf, "…");
        int lw = width_of(f, buf, buf + strlen(buf));
        int lx = x;
        if (flags & ALIGN_CENTER) lx = x + (w - lw) / 2;
        else if (flags & ALIGN_RIGHT) lx = x + w - lw;
        int base = ty + i * lh + (int)(f->size->metrics.ascender >> 6) + (lh - cur_font->size) / 2;
        draw_run(f, lx, base, buf, buf + strlen(buf));
    }
    return NULL;
}

/* --- PNG і цикл подій --- */

static void shot(const char *path)
{
    FILE *fp = fopen(path, "wb");
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    png_init_io(png, fp);
    png_set_IHDR(png, info, (png_uint_32)SW, (png_uint_32)SH, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < SH; y++) png_write_row(png, fb + (size_t)y * SW * 3);
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    printf("  [shot] %s\n", path);
}

void InkViewMain(iv_handler h)
{
    if (getenv("MOCK_W")) SW = atoi(getenv("MOCK_W"));
    if (getenv("MOCK_H")) SH = atoi(getenv("MOCK_H"));
    fb = malloc((size_t)SW * SH * 3);
    memset(fb, 255, (size_t)SW * SH * 3);
    FT_Init_FreeType(&ft);

    const char *sc = getenv("MOCK_SCRIPT");
    if (!sc) sc = "INIT;SHOW;SHOT build/screen.png";
    char *script = strdup(sc), *save = NULL;
    for (char *cmd = strtok_r(script, ";", &save); cmd && !closed; cmd = strtok_r(NULL, ";", &save)) {
        while (*cmd == ' ') cmd++;
        int a = 0, b = 0;
        char path[512];
        printf("> %s\n", cmd);
        if (!strcmp(cmd, "INIT")) h(EVT_INIT, 0, 0);
        else if (!strcmp(cmd, "SHOW")) h(EVT_SHOW, 0, 0);
        else if (sscanf(cmd, "TAP %d %d", &a, &b) == 2) h(EVT_POINTERUP, a, b);
        else if (sscanf(cmd, "KEY %d", &a) == 1) h(EVT_KEYPRESS, a, 0);
        else if (sscanf(cmd, "MENU %d", &a) == 1 && menu_h) menu_h(a);
        else if (sscanf(cmd, "DIALOG %d", &a) == 1 && dialog_h) dialog_h(a);
        else if (sscanf(cmd, "SHOT %511s", path) == 1) shot(path);
    }
    h(EVT_EXIT, 0, 0);
    free(script);
}
