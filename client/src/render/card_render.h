#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "game/card.h"

#define CARD_W   71
#define CARD_H   96
#define CARD_R    6  /* corner rounding approximation (not real rounded rect) */

/* Draw a face-up card at (x, y) using procedural rendering (no sprites). */
void card_draw(SDL_Renderer *r, TTF_Font *font_md, TTF_Font *font_sm,
               Card card, int x, int y);

/* Draw a card back at (x, y). */
void card_draw_back(SDL_Renderer *r, int x, int y);
