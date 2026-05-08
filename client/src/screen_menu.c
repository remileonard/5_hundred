#include "screen_menu.h"
#include "ui/ui.h"
#include <SDL2/SDL.h>

/* ── Layout ──────────────────────────────────────────────────────────────────── */

#define BTN_W 260
#define BTN_H  52
#define BTN_GAP 18

static Button btns[3];
static int btns_init = 0;

static void init_buttons(void)
{
    if (btns_init) return;
    int cx = WINDOW_W / 2 - BTN_W / 2;
    int cy = WINDOW_H / 2 - 30;

    btns[0] = (Button){ {cx, cy,               BTN_W, BTN_H}, "Nouvelle partie", false };
    btns[1] = (Button){ {cx, cy + BTN_H + BTN_GAP, BTN_W, BTN_H}, "Rejoindre",   false };
    btns[2] = (Button){ {cx, cy + 2*(BTN_H+BTN_GAP), BTN_W, BTN_H}, "Quitter",   false };
    btns_init = 1;
}

/* ── Event ───────────────────────────────────────────────────────────────────── */

void screen_menu_handle_event(App *app, SDL_Event *e)
{
    init_buttons();

    if (e->type == SDL_MOUSEMOTION) {
        for (int i = 0; i < 3; i++)
            ui_button_update(&btns[i], e->motion.x, e->motion.y);
    }

    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        int mx = e->button.x, my = e->button.y;
        if (ui_button_hit(&btns[0], mx, my)) app->screen = SCREEN_LOBBY;
        if (ui_button_hit(&btns[1], mx, my)) app->screen = SCREEN_LOBBY;
        if (ui_button_hit(&btns[2], mx, my)) app->running = false;
    }
}

/* ── Render ──────────────────────────────────────────────────────────────────── */

void screen_menu_render(App *app)
{
    init_buttons();
    SDL_Renderer *r = app->renderer;

    /* Dark background */
    SDL_SetRenderDrawColor(r, 15, 20, 35, 255);
    SDL_RenderClear(r);

    /* Title */
    SDL_Rect title_bounds = {0, WINDOW_H / 4 - 40, WINDOW_W, 80};
    ui_text_centered(r, app->font_lg, "5 Hundred", title_bounds, COL_WHITE);

    /* Subtitle */
    SDL_Rect sub_bounds = {0, WINDOW_H / 4 + 48, WINDOW_W, 36};
    SDL_Color grey = {160, 160, 180, 255};
    ui_text_centered(r, app->font_md, "Jeu de cartes multijoueur", sub_bounds, grey);

    /* Buttons */
    for (int i = 0; i < 3; i++)
        ui_button_draw(r, app->font_md, &btns[i]);
}
