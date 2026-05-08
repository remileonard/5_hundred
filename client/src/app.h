#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>
#include "game/game_state.h"

#define WINDOW_TITLE "5 Hundred"
#define WINDOW_W     1280
#define WINDOW_H      720
#define TARGET_FPS     60

/* ── Network / lobby types ───────────────────────────────────────────────── */

#define MAX_LOBBY_ROOMS 16

typedef struct {
    char id[16];
    char name[64];
    char variant[4];       /* "4p" or "2p" */
    char players[4][32];
    int  player_count;
    int  max_seats;
} RoomInfoC;

typedef struct {
    char      player_id[64];
    char      player_name[64];
    char      server_host[128];
    int       server_port;
    RoomInfoC rooms[MAX_LOBBY_ROOMS];
    int       room_count;
    RoomInfoC current_room;
    int       my_seat;
    bool      in_room;
} NetState;

/* ── Screen ──────────────────────────────────────────────────────────────── */

typedef enum {
    SCREEN_MENU = 0,
    SCREEN_LOBBY,
    SCREEN_TABLE,
} Screen;

typedef struct {
    SDL_Window      *window;
    SDL_Renderer    *renderer;
    bool             running;
    Screen           screen;
    TTF_Font        *font_lg;   /* ~36pt — titles   */
    TTF_Font        *font_md;   /* ~20pt — UI text  */
    TTF_Font        *font_sm;   /* ~14pt — card text */
    ClientGameState  gs;        /* client-side game state */
    NetState         net;       /* network + lobby state  */
} App;

extern App g_app;
