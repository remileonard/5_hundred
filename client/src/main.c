#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "app.h"
#include "screen_menu.h"
#include "screen_lobby.h"
#include "screen_table.h"
#include "net/net.h"
#include "net/msg.h"

App g_app;

/* ── Network callbacks ─────────────────────────────────────────────────────── */

static void on_net_connect(void)
{
    net_send_identify(g_app.net.player_name);
}

static void on_net_message(const char *data, int len)
{
    net_handle_message(&g_app, data, len);
}

static void on_net_disconnect(int code, const char *reason)
{
    (void)code; (void)reason;
    g_app.net.room_count = 0;
    g_app.net.in_room    = false;
}

/* ── Font loading ──────────────────────────────────────────────────────────── */

/* Try a list of paths and return the first TTF_Font that loads, or NULL. */
static TTF_Font *load_font_any(int ptsize)
{
    const char *paths[] = {
        "assets/fonts/main.ttf",
        /* macOS system fonts */
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        /* Linux */
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        /* Windows */
        "C:/Windows/Fonts/arial.ttf",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        TTF_Font *f = TTF_OpenFont(paths[i], ptsize);
        if (f) return f;
    }
    fprintf(stderr, "Warning: could not load any font at %dpt. "
                    "Run scripts/fetch-assets.sh to download one.\n", ptsize);
    return NULL;
}

/* ── Per-frame dispatch ────────────────────────────────────────────────────── */

static void handle_events(App *app)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            app->running = false;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
            app->running = false;

        switch (app->screen) {
        case SCREEN_MENU:  screen_menu_handle_event(app, &e);  break;
        case SCREEN_LOBBY: screen_lobby_handle_event(app, &e); break;
        case SCREEN_TABLE: screen_table_handle_event(app, &e); break;
        }
    }
}

static void render_frame(App *app);

static void tick(App *app)
{
    static Screen prev_screen = (Screen)-1;

    /* Fire enter-hook on screen transition */
    if (app->screen != prev_screen) {
        if (app->screen == SCREEN_LOBBY)
            screen_lobby_enter(app);
        prev_screen = app->screen;
    }

    net_poll();
    handle_events(app);
    render_frame(app);
}

static void render_frame(App *app)
{
    switch (app->screen) {
    case SCREEN_MENU:  screen_menu_render(app);  break;
    case SCREEN_LOBBY: screen_lobby_render(app); break;
    case SCREEN_TABLE: screen_table_render(app); break;
    }
    SDL_RenderPresent(app->renderer);
}

/* ── Main loop ─────────────────────────────────────────────────────────────── */

#ifdef __EMSCRIPTEN__
static void emscripten_loop(void *arg)
{
    App *app = (App *)arg;
    tick(app);
    if (!app->running)
        emscripten_cancel_main_loop();
}
#endif

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    g_app.window = SDL_CreateWindow(
        WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!g_app.window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        TTF_Quit(); SDL_Quit();
        return 1;
    }

    g_app.renderer = SDL_CreateRenderer(
        g_app.window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!g_app.renderer) {
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_app.window);
        TTF_Quit(); SDL_Quit();
        return 1;
    }
    /* Keep the game centred and correctly sized when the window is resized. */
    SDL_RenderSetLogicalSize(g_app.renderer, WINDOW_W, WINDOW_H);

    g_app.font_lg = load_font_any(36);
    g_app.font_md = load_font_any(20);
    g_app.font_sm = load_font_any(14);

    /* Network defaults */
    strncpy(g_app.net.server_host, "localhost", sizeof(g_app.net.server_host) - 1);
    g_app.net.server_port = 8080;
    strncpy(g_app.net.player_name, "Joueur", sizeof(g_app.net.player_name) - 1);
    net_init(on_net_connect, on_net_message, on_net_disconnect);

    g_app.running = true;
    g_app.screen  = SCREEN_MENU;

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(emscripten_loop, &g_app, TARGET_FPS, 1);
#else
    const int frame_delay = 1000 / TARGET_FPS;
    while (g_app.running) {
        Uint32 frame_start = SDL_GetTicks();
        tick(&g_app);
        Uint32 elapsed = SDL_GetTicks() - frame_start;
        if (elapsed < (Uint32)frame_delay)
            SDL_Delay(frame_delay - elapsed);
    }
#endif

    net_shutdown();

    if (g_app.font_lg) TTF_CloseFont(g_app.font_lg);
    if (g_app.font_md) TTF_CloseFont(g_app.font_md);
    if (g_app.font_sm) TTF_CloseFont(g_app.font_sm);
    SDL_DestroyRenderer(g_app.renderer);
    SDL_DestroyWindow(g_app.window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
