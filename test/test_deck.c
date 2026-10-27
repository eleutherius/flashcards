/* Юніт-тести логіки колод: cc -I../src test_deck.c ../src/deck.c && ./a.out */
#include "deck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void write_file(const char *p, const char *s)
{
    FILE *f = fopen(p, "wb");
    fputs(s, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char deck_path[512], prog_path[512];
    snprintf(deck_path, sizeof deck_path, "%s/t.tsv", dir);
    snprintf(prog_path, sizeof prog_path, "%s/t.progress", dir);

    /* --- парсинг --- */
    write_file(deck_path,
        "\xEF\xBB\xBF" "# коментар\r\n"
        "reliable\tнадійний\tA reliable server.\r\n"
        "\r\n"
        "   \n"
        "to give up\tздатися\n"
        "although;хоча\n"
        "deadline - кінцевий термін\n"
        "без роздільника\n"
        "\tпорожній термін\n"
        "multi\tперший\\nдругий\n"
        "last line no newline\tостанній");
    fc_deck d;
    int n = fc_deck_load(&d, deck_path);
    CHECK(n == 6);
    CHECK(strcmp(d.cards[0].front, "reliable") == 0);
    CHECK(strcmp(d.cards[0].back, "надійний") == 0);
    CHECK(d.cards[0].example && strcmp(d.cards[0].example, "A reliable server.") == 0);
    CHECK(d.cards[1].example == NULL);
    CHECK(strcmp(d.cards[2].front, "although") == 0 && strcmp(d.cards[2].back, "хоча") == 0);
    CHECK(strcmp(d.cards[3].front, "deadline") == 0 && strcmp(d.cards[3].back, "кінцевий термін") == 0);
    CHECK(strcmp(d.cards[4].back, "перший\nдругий") == 0);
    CHECK(strcmp(d.cards[5].back, "останній") == 0);

    /* --- сесія: нові картки з лімітом --- */
    int today = 20000;
    fc_session s;
    CHECK(fc_session_start(&s, &d, today, 3, 0, 0, 42) == 3);
    CHECK(fc_session_current(&s) == 0);

    fc_session_answer(&s, &d, today, 1);           /* reliable: знаю */
    CHECK(d.cards[0].box == 1 && d.cards[0].due == today + 1);
    CHECK(fc_session_current(&s) == 1);

    fc_session_answer(&s, &d, today, 0);           /* to give up: не знаю */
    CHECK(d.cards[1].box == 1 && d.cards[1].due == today);
    CHECK(fc_session_remaining(&s) == 2);          /* although + повтор */
    CHECK(fc_session_current(&s) == 2);
    fc_session_answer(&s, &d, today, 1);           /* although: знаю */
    CHECK(fc_session_current(&s) == 1);            /* повернулась "to give up" */
    fc_session_answer(&s, &d, today, 1);
    CHECK(d.cards[1].box == 1 && d.cards[1].due == today + 1);
    CHECK(fc_session_current(&s) == -1);
    CHECK(s.unique == 3 && s.done == 3 && s.first_try == 2 && s.mistakes == 1);
    fc_session_free(&s);

    /* --- статистика --- */
    fc_stats st;
    fc_deck_stats(&d, today, &st);
    CHECK(st.total == 6 && st.fresh == 3 && st.due == 0);
    fc_deck_stats(&d, today + 1, &st);
    CHECK(st.due == 3);
    CHECK(fc_next_due(&d) == today + 1);

    /* --- наступного дня: картки на повторення йдуть першими, рівень росте --- */
    CHECK(fc_session_start(&s, &d, today + 1, 1, 0, 0, 1) == 4);
    CHECK(fc_session_current(&s) == 0);
    fc_session_answer(&s, &d, today + 1, 1);
    CHECK(d.cards[0].box == 2 && d.cards[0].due == today + 1 + 2);
    fc_session_free(&s);

    /* --- зубріння не змінює рівні правильних відповідей --- */
    CHECK(fc_session_start(&s, &d, today + 1, 0, 0, 1, 1) == 6);
    fc_session_answer(&s, &d, today + 1, 1);
    CHECK(d.cards[0].box == 2);
    fc_session_free(&s);

    /* --- збереження і завантаження прогресу після зміни порядку --- */
    CHECK(fc_progress_save(&d, prog_path) == 0);
    fc_deck_free(&d);
    write_file(deck_path,
        "although;хоча\n"
        "новий термін\tнове\n"
        "reliable\tнадійний\n"
        "to give up\tздатися\n");
    CHECK(fc_deck_load(&d, deck_path) == 4);
    CHECK(fc_progress_load(&d, prog_path) == 3);
    CHECK(d.cards[0].box == 1);                     /* although */
    CHECK(d.cards[1].box == 0);                     /* новий */
    CHECK(d.cards[2].box == 2 && d.cards[2].due == today + 3);
    CHECK(d.cards[3].box == 1);

    /* --- перемішування дає ту саму множину --- */
    CHECK(fc_session_start(&s, &d, today + 100, 0, 1, 0, 7) == 4);
    int seen[4] = { 0 };
    for (int i = 0; i < s.len; i++) seen[s.q[i]]++;
    CHECK(seen[0] == 1 && seen[1] == 1 && seen[2] == 1 && seen[3] == 1);
    fc_session_free(&s);

    /* --- відсутній файл --- */
    fc_deck e;
    CHECK(fc_deck_load(&e, "/nonexistent/x.tsv") == -1);
    fc_deck_free(&e);

    fc_deck_free(&d);
    printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
    return fails != 0;
}
