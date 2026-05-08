#pragma once
#include <SDL2/SDL.h>
#include "app.h"

/* Called when the lobby screen is entered (screen transition). */
void screen_lobby_enter(App *app);

/* Clear the "pending" waiting overlay (call on server error or refresh). */
void screen_lobby_clear_pending(void);

void screen_lobby_handle_event(App *app, SDL_Event *e);
void screen_lobby_render(App *app);
