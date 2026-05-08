#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>

/* ── Button ────────────────────────────────────────────────────────────────── */

typedef struct {
    SDL_Rect    rect;
    const char *label;
    bool        hovered;
} Button;

/* Draw a button (filled rect + border + centred label). */
void ui_button_draw(SDL_Renderer *r, TTF_Font *font, const Button *btn);

/* Returns true if point (mx, my) is inside the button. */
bool ui_button_hit(const Button *btn, int mx, int my);

/* Update hovered state from current mouse position. */
void ui_button_update(Button *btn, int mx, int my);

/* ── Text helpers ──────────────────────────────────────────────────────────── */

/* Render text at (x, y) — top-left origin. */
void ui_text(SDL_Renderer *r, TTF_Font *font, const char *text,
             int x, int y, SDL_Color color);

/* Render text centred inside rect. */
void ui_text_centered(SDL_Renderer *r, TTF_Font *font, const char *text,
                      SDL_Rect bounds, SDL_Color color);

/* ── Colours ───────────────────────────────────────────────────────────────── */

#define COL_WHITE       ((SDL_Color){255, 255, 255, 255})
#define COL_BLACK       ((SDL_Color){  0,   0,   0, 255})
#define COL_RED         ((SDL_Color){200,  30,  30, 255})
#define COL_BTN_NORMAL  ((SDL_Color){ 50,  50,  60, 230})
#define COL_BTN_HOVER   ((SDL_Color){ 80,  80, 110, 230})
#define COL_BTN_BORDER  ((SDL_Color){150, 150, 180, 255})
#define COL_TABLE       ((SDL_Color){ 20,  90,  40, 255})
