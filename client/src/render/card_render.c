#include "card_render.h"
#include "../ui/ui.h"
#include <stdio.h>
#include <string.h>

/* ── Helpers ─────────────────────────────────────────────────────────────────── */

static void set_color(SDL_Renderer *r, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

/* Approximate rounded rect using one filled rect + four corner overdraw rects. */
static void fill_rounded_rect(SDL_Renderer *r, SDL_Rect rect, int radius)
{
    /* Fill the cross */
    SDL_Rect h = {rect.x,          rect.y + radius,
                  rect.w,          rect.h - 2 * radius};
    SDL_Rect v = {rect.x + radius, rect.y,
                  rect.w - 2 * radius, rect.h};
    SDL_RenderFillRect(r, &h);
    SDL_RenderFillRect(r, &v);
    (void)radius;
}

static void draw_rounded_rect_border(SDL_Renderer *r, SDL_Rect rect, int radius)
{
    (void)radius;
    SDL_RenderDrawRect(r, &rect);
}

/* ── Face-up card ────────────────────────────────────────────────────────────── */

void card_draw(SDL_Renderer *r, TTF_Font *font_md, TTF_Font *font_sm,
               Card card, int x, int y)
{
    SDL_Rect rect = {x, y, CARD_W, CARD_H};

    /* Card face — white */
    set_color(r, (SDL_Color){252, 252, 252, 255});
    fill_rounded_rect(r, rect, CARD_R);

    /* Border */
    set_color(r, (SDL_Color){100, 100, 100, 255});
    draw_rounded_rect_border(r, rect, CARD_R);

    if (card.rank == RANK_JOKER) {
        /* Joker: colourful diagonal stripe */
        set_color(r, (SDL_Color){220, 160, 0, 255});
        SDL_Rect stripe = {x + 8, y + 8, CARD_W - 16, CARD_H - 16};
        SDL_RenderFillRect(r, &stripe);
        ui_text_centered(r, font_md, "Jo", rect, COL_BLACK);
        return;
    }

    SDL_Color ink = card_suit_is_red(card.suit) ? COL_RED : COL_BLACK;
    const char *rank = card_rank_str(card.rank);
    const char *suit = card_suit_str(card.suit);

    /* Build "rank+suit" label, e.g. "A♥" */
    char label[8];
    snprintf(label, sizeof(label), "%s%s", rank, suit);

    /* Top-left small label */
    ui_text(r, font_sm, label, x + 3, y + 2, ink);

    /* Centre suit symbol (larger) */
    int cx = x + CARD_W / 2;
    int cy = y + CARD_H / 2;
    SDL_Rect centre_bounds = {cx - 20, cy - 14, 40, 28};
    ui_text_centered(r, font_md, suit, centre_bounds, ink);
}

/* ── Card back ───────────────────────────────────────────────────────────────── */

void card_draw_back(SDL_Renderer *r, int x, int y)
{
    SDL_Rect rect = {x, y, CARD_W, CARD_H};

    /* Dark blue back */
    set_color(r, (SDL_Color){30, 50, 140, 255});
    fill_rounded_rect(r, rect, CARD_R);

    /* Inner white border */
    SDL_Rect inner = {x + 4, y + 4, CARD_W - 8, CARD_H - 8};
    set_color(r, (SDL_Color){200, 210, 240, 180});
    SDL_RenderDrawRect(r, &inner);

    /* Outer border */
    set_color(r, (SDL_Color){60, 80, 180, 255});
    draw_rounded_rect_border(r, rect, CARD_R);

    /* Simple diagonal cross pattern — clipped to card bounds */
    SDL_RenderSetClipRect(r, &rect);
    set_color(r, (SDL_Color){50, 70, 160, 255});
    for (int i = -CARD_H; i < CARD_W; i += 8) {
        SDL_RenderDrawLine(r,
            x + i,          y,
            x + i + CARD_H, y + CARD_H);
        SDL_RenderDrawLine(r,
            x + i + CARD_H, y,
            x + i,          y + CARD_H);
    }
    SDL_RenderSetClipRect(r, NULL);

    /* Re-draw inner border on top of pattern */
    set_color(r, (SDL_Color){200, 210, 240, 180});
    SDL_RenderDrawRect(r, &inner);
}
