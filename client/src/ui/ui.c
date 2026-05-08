#include "ui.h"
#include <SDL2/SDL.h>

/* ── Internal helpers ───────────────────────────────────────────────────────── */

static void set_color(SDL_Renderer *r, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

/* Render UTF-8 text using SDL_ttf, returning the rendered texture.
   Caller must SDL_DestroyTexture the result (or it may be NULL on error). */
static SDL_Texture *make_text_texture(SDL_Renderer *r, TTF_Font *font,
                                      const char *text, SDL_Color color)
{
    if (!font || !text || text[0] == '\0') return NULL;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return NULL;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    return tex;
}

/* ── Text ───────────────────────────────────────────────────────────────────── */

void ui_text(SDL_Renderer *r, TTF_Font *font, const char *text,
             int x, int y, SDL_Color color)
{
    SDL_Texture *tex = make_text_texture(r, font, text, color);
    if (!tex) return;
    int w, h;
    SDL_QueryTexture(tex, NULL, NULL, &w, &h);
    SDL_Rect dst = {x, y, w, h};
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

void ui_text_centered(SDL_Renderer *r, TTF_Font *font, const char *text,
                      SDL_Rect bounds, SDL_Color color)
{
    SDL_Texture *tex = make_text_texture(r, font, text, color);
    if (!tex) return;
    int w, h;
    SDL_QueryTexture(tex, NULL, NULL, &w, &h);
    SDL_Rect dst = {
        bounds.x + (bounds.w - w) / 2,
        bounds.y + (bounds.h - h) / 2,
        w, h
    };
    SDL_RenderCopy(r, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

/* ── Button ─────────────────────────────────────────────────────────────────── */

void ui_button_draw(SDL_Renderer *r, TTF_Font *font, const Button *btn)
{
    SDL_Color bg = btn->hovered ? COL_BTN_HOVER : COL_BTN_NORMAL;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    set_color(r, bg);
    SDL_RenderFillRect(r, &btn->rect);

    set_color(r, COL_BTN_BORDER);
    SDL_RenderDrawRect(r, &btn->rect);

    /* inner highlight (top line) */
    SDL_Color hi = {200, 200, 220, 80};
    set_color(r, hi);
    SDL_RenderDrawLine(r,
        btn->rect.x + 1, btn->rect.y + 1,
        btn->rect.x + btn->rect.w - 2, btn->rect.y + 1);

    ui_text_centered(r, font, btn->label, btn->rect, COL_WHITE);
}

bool ui_button_hit(const Button *btn, int mx, int my)
{
    return mx >= btn->rect.x && mx < btn->rect.x + btn->rect.w &&
           my >= btn->rect.y && my < btn->rect.y + btn->rect.h;
}

void ui_button_update(Button *btn, int mx, int my)
{
    btn->hovered = ui_button_hit(btn, mx, my);
}
