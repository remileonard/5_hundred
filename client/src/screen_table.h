#pragma once
#include <SDL2/SDL.h>
#include "app.h"

void screen_table_handle_event(App *app, SDL_Event *e);
void screen_table_render(App *app);
