#include "screen_lobby.h"
#include "ui/ui.h"
#include "net/net.h"
#include "net/msg.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ── Layout constants ────────────────────────────────────────────────────── */

#define COL_BG_R  20
#define COL_BG_G  25
#define COL_BG_B  45

#define ROOM_ROW_H   52
#define ROOM_LIST_Y 220
#define ROOM_LIST_X  60

/* ── Static state ────────────────────────────────────────────────────────── */

static Button s_btn_back;
static Button s_btn_connect;
static Button s_btn_create_4p;
static Button s_btn_create_2p;
static Button s_btn_join[MAX_LOBBY_ROOMS];
static Button s_btn_refresh;
static int    s_room_create_counter = 0;
static bool   s_pending = false; /* waiting for a server response after create/join */

static void init_static_buttons(void)
{
    static int done = 0;
    if (done) return;
    done = 1;
    s_btn_back    = (Button){ {30, 30, 120, 44},       "← Retour",        false };
    s_btn_connect = (Button){ {WINDOW_W/2 - 100, 160, 200, 44}, "Se connecter", false };
    s_btn_create_4p = (Button){ {WINDOW_W - 320, 30, 140, 44}, "Nouvelle 4j",  false };
    s_btn_create_2p = (Button){ {WINDOW_W - 170, 30, 140, 44}, "Nouvelle 2j",  false };
    s_btn_refresh   = (Button){ {WINDOW_W - 170, ROOM_LIST_Y - 50, 140, 36}, "Rafraîchir", false };
}

/* Rebuild per-room join buttons based on current room list. */
static void rebuild_join_buttons(App *app)
{
    for (int i = 0; i < app->net.room_count && i < MAX_LOBBY_ROOMS; i++) {
        SDL_Rect r = {
            ROOM_LIST_X + WINDOW_W - 260,
            ROOM_LIST_Y + i * ROOM_ROW_H + 6,
            120, 36
        };
        s_btn_join[i] = (Button){ r, "Rejoindre", false };
    }
}

/* ── screen_lobby_enter ──────────────────────────────────────────────────── */

void screen_lobby_clear_pending(void) { s_pending = false; }

void screen_lobby_enter(App *app)
{
    init_static_buttons();
    s_pending = false;
    if (net_get_status() == NET_DISCONNECTED) {
        if (app->net.server_host[0] == '\0')
            strncpy(app->net.server_host, "localhost", sizeof(app->net.server_host) - 1);
        if (app->net.server_port == 0)
            app->net.server_port = 8080;
        if (app->net.player_name[0] == '\0')
            strncpy(app->net.player_name, "Joueur", sizeof(app->net.player_name) - 1);
        net_connect(app->net.server_host, app->net.server_port, "/ws");
    } else if (net_get_status() == NET_CONNECTED) {
        /* Coming back from table or re-entering lobby: refresh room list */
        net_send_room_list();
    }
}

/* ── Event handling ──────────────────────────────────────────────────────── */

void screen_lobby_handle_event(App *app, SDL_Event *e)
{
    init_static_buttons();
    NetStatus status = net_get_status();

    int mx = 0, my = 0;
    if (e->type == SDL_MOUSEMOTION) {
        mx = e->motion.x; my = e->motion.y;
        ui_button_update(&s_btn_back, mx, my);
        if (status == NET_DISCONNECTED)
            ui_button_update(&s_btn_connect, mx, my);
        if (status == NET_CONNECTED) {
            ui_button_update(&s_btn_create_4p, mx, my);
            ui_button_update(&s_btn_create_2p, mx, my);
            ui_button_update(&s_btn_refresh,   mx, my);
            for (int i = 0; i < app->net.room_count; i++)
                ui_button_update(&s_btn_join[i], mx, my);
        }
    }

    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        mx = e->button.x; my = e->button.y;

        if (ui_button_hit(&s_btn_back, mx, my)) {
            if (status == NET_CONNECTED) {
                net_send_room_leave();
                net_close();
            }
            app->net.room_count = 0;
            app->net.in_room    = false;
            app->screen = SCREEN_MENU;
            return;
        }

        if (status == NET_DISCONNECTED && ui_button_hit(&s_btn_connect, mx, my)) {
            net_connect(app->net.server_host, app->net.server_port, "/ws");
            return;
        }

        if (status == NET_CONNECTED) {
            if (ui_button_hit(&s_btn_refresh, mx, my)) {
                net_send_room_list();
                return;
            }
            if (ui_button_hit(&s_btn_create_4p, mx, my)) {
                char name[64];
                snprintf(name, sizeof(name), "Partie %d", ++s_room_create_counter);
                net_send_room_create(name, "4p");
                s_pending = true;
                return;
            }
            if (ui_button_hit(&s_btn_create_2p, mx, my)) {
                char name[64];
                snprintf(name, sizeof(name), "Partie %d", ++s_room_create_counter);
                net_send_room_create(name, "2p");
                s_pending = true;
                return;
            }
            for (int i = 0; i < app->net.room_count; i++) {
                if (ui_button_hit(&s_btn_join[i], mx, my)) {
                    net_send_room_join(app->net.rooms[i].id);
                    s_pending = true;
                    return;
                }
            }
        }
    }
}

/* ── Render ──────────────────────────────────────────────────────────────── */

static void draw_room_row(SDL_Renderer *r, TTF_Font *font_md, TTF_Font *font_sm,
                          App *app, int idx, int y)
{
    RoomInfoC *room = &app->net.rooms[idx];
    SDL_Color col_text = COL_WHITE;
    SDL_Color col_dim  = {160, 170, 200, 255};

    /* Row background */
    SDL_SetRenderDrawColor(r, 35, 42, 70, 255);
    SDL_Rect row = {ROOM_LIST_X, y, WINDOW_W - ROOM_LIST_X * 2, ROOM_ROW_H - 4};
    SDL_RenderFillRect(r, &row);

    /* Name */
    ui_text(r, font_md, room->name, ROOM_LIST_X + 12, y + 10, col_text);

    /* Variant + player count */
    char info[32];
    snprintf(info, sizeof(info), "[%s]  %d/%d joueurs",
             room->variant, room->player_count, room->max_seats);
    ui_text(r, font_sm, info, ROOM_LIST_X + 220, y + 14, col_dim);

    /* Join button */
    bool full = (room->player_count >= room->max_seats);
    if (!full)
        ui_button_draw(r, font_sm, &s_btn_join[idx]);
    else {
        SDL_Color col_full = {100, 100, 120, 255};
        ui_text(r, font_sm, "Complet", s_btn_join[idx].rect.x + 10,
                s_btn_join[idx].rect.y + 10, col_full);
    }
}

void screen_lobby_render(App *app)
{
    init_static_buttons();
    rebuild_join_buttons(app);

    SDL_Renderer *r = app->renderer;
    NetStatus status = net_get_status();

    /* Background */
    SDL_SetRenderDrawColor(r, COL_BG_R, COL_BG_G, COL_BG_B, 255);
    SDL_RenderClear(r);

    /* Title */
    SDL_Rect title_rect = {0, 55, WINDOW_W, 56};
    ui_text_centered(r, app->font_lg, "Lobby", title_rect, COL_WHITE);

    /* Back button (always visible) */
    ui_button_draw(r, app->font_sm, &s_btn_back);

    /* Connection status bar */
    {
        SDL_Color status_col;
        const char *status_str;
        switch (status) {
        case NET_CONNECTED:
            status_col = (SDL_Color){80, 200, 120, 255};
            status_str = "Connecté";
            break;
        case NET_CONNECTING:
            status_col = (SDL_Color){220, 180, 60, 255};
            status_str = "Connexion en cours…";
            break;
        default:
            status_col = (SDL_Color){200, 80, 80, 255};
            status_str = "Déconnecté";
            break;
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "%s  (%s:%d)",
                 status_str, app->net.server_host, app->net.server_port);
        SDL_Rect sr = {0, 120, WINDOW_W, 30};
        ui_text_centered(r, app->font_sm, buf, sr, status_col);
    }

    if (status == NET_DISCONNECTED) {
        ui_button_draw(r, app->font_md, &s_btn_connect);
        return;
    }

    if (status == NET_CONNECTING) return;

    /* Connected: show player name + create buttons */
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "Connecté en tant que : %s", app->net.player_name);
        SDL_Rect nr = {0, 160, WINDOW_W, 28};
        SDL_Color col_name = {180, 190, 230, 255};
        ui_text_centered(r, app->font_sm, buf, nr, col_name);
    }

    /* Pending overlay: waiting for server response */
    if (s_pending) {
        SDL_Color col_wait = {220, 180, 60, 255};
        SDL_Rect wr = {0, WINDOW_H / 2 - 20, WINDOW_W, 40};
        ui_text_centered(r, app->font_md, "En attente du serveur…", wr, col_wait);
        return;
    }

    ui_button_draw(r, app->font_sm, &s_btn_create_4p);
    ui_button_draw(r, app->font_sm, &s_btn_create_2p);

    /* Room list header */
    {
        SDL_Color col_hdr = {140, 150, 190, 255};
        char hbuf[32];
        snprintf(hbuf, sizeof(hbuf), "Salons disponibles (%d)", app->net.room_count);
        ui_text(r, app->font_md, hbuf, ROOM_LIST_X, ROOM_LIST_Y - 34, col_hdr);
        ui_button_draw(r, app->font_sm, &s_btn_refresh);
    }

    if (app->net.room_count == 0) {
        SDL_Color col_dim = {120, 130, 160, 255};
        SDL_Rect er = {0, ROOM_LIST_Y + 20, WINDOW_W, 30};
        ui_text_centered(r, app->font_sm, "Aucune partie disponible — créez-en une !", er, col_dim);
    } else {
        int max_visible = (WINDOW_H - ROOM_LIST_Y - 20) / ROOM_ROW_H;
        int count = app->net.room_count < max_visible ? app->net.room_count : max_visible;
        for (int i = 0; i < count; i++)
            draw_room_row(r, app->font_md, app->font_sm, app,
                          i, ROOM_LIST_Y + i * ROOM_ROW_H);
    }
}

