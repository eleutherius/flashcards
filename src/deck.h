/*
 * deck.h — колоди карток, прогрес і інтервальне повторення (система Лейтнера).
 * Чиста логіка без InkView: компілюється і тестується на звичайному Linux.
 */
#ifndef FC_DECK_H
#define FC_DECK_H

#define FC_MAX_BOX 6

typedef struct {
    char *front;    /* термін (слово) */
    char *back;     /* визначення (переклад) */
    char *example;  /* необов'язковий приклад, може бути NULL */
    int box;        /* 0 = нова, 1..FC_MAX_BOX = рівень засвоєння */
    int due;        /* день наступного повторення (днів від епохи) */
} fc_card;

typedef struct {
    fc_card *cards;
    int n, cap;
    char *buf;      /* увесь файл; поля карток вказують всередину нього */
} fc_deck;

typedef struct {
    int total;      /* усього карток */
    int fresh;      /* нових (ще не вивчались) */
    int due;        /* потрібно повторити сьогодні */
    int learned;    /* добре засвоєних (рівень >= 4) */
} fc_stats;

/* Завантажує TSV (формат експорту Quizlet: термін<TAB>визначення[<TAB>приклад]).
 * Також підтримує роздільник ';' або " - ", якщо в рядку немає табуляції.
 * Повертає кількість карток або -1, якщо файл не відкрився. */
int  fc_deck_load(fc_deck *d, const char *path);
void fc_deck_free(fc_deck *d);

/* Прогрес зберігається окремим файлом і прив'язаний до тексту терміна,
 * тож редагування чи сортування колоди його не губить. */
int  fc_progress_load(fc_deck *d, const char *path);
int  fc_progress_save(const fc_deck *d, const char *path);
void fc_progress_reset(fc_deck *d);

void fc_deck_stats(const fc_deck *d, int today, fc_stats *s);
/* Скільки карток стане до повторення до дня day включно (без нових). */
int  fc_due_by(const fc_deck *d, int day);
/* Найближчий день повторення серед вивчених карток або -1. */
int  fc_next_due(const fc_deck *d);

/* Інтервал (у днях) для рівня 1..FC_MAX_BOX. */
int  fc_interval(int box);

/* Номер поточного дня за місцевим часом. */
int  fc_today(void);

/* ---- Сесія навчання ---- */
typedef struct {
    int *q;             /* черга індексів карток */
    int len, cap, pos;
    unsigned char *failed;  /* картка була з помилкою в цій сесії */
    int unique;         /* скільки різних карток у сесії */
    int done;           /* скільки вже засвоєно в сесії */
    int first_try;      /* правильно з першої спроби */
    int mistakes;       /* усього відповідей "не знаю" */
    int cram;           /* режим "повторити все" — не просуває рівні */
    unsigned rng;
} fc_session;

/* Будує чергу: спочатку картки "на сьогодні", потім до new_limit нових
 * (new_limit <= 0 — без обмеження). cram=1 — усі картки колоди.
 * Повертає кількість карток у сесії. */
int  fc_session_start(fc_session *s, const fc_deck *d, int today,
                      int new_limit, int shuffle, int cram, unsigned seed);
int  fc_session_current(const fc_session *s);    /* -1 — сесію завершено */
int  fc_session_remaining(const fc_session *s);
/* known=1 — "Знаю", 0 — "Не знаю" (картка повернеться через кілька карток). */
void fc_session_answer(fc_session *s, fc_deck *d, int today, int known);
void fc_session_free(fc_session *s);

#endif
