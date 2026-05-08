#include "game_state.h"
#include <string.h>
#include <stdio.h>

void game_state_init_demo(ClientGameState *gs)
{
    memset(gs, 0, sizeof(*gs));
    gs->num_players   = 4;
    gs->local_seat    = 0;
    gs->selected_card = -1;
    gs->trump_suit    = SUIT_HEARTS;
    gs->scores[0]     = 120;
    gs->scores[1]     = 40;
    gs->phase         = PHASE_PLAYING;
    gs->to_act        = 0;
    gs->contractor    = 0;
    gs->contract.level = 7;
    snprintf(gs->contract.suit, sizeof(gs->contract.suit), "H");

    /* Seat 0 — local player (10 cards) */
    snprintf(gs->players[0].name, sizeof(gs->players[0].name), "You");
    Card hand0[] = {
        {RANK_A, SUIT_HEARTS},   {RANK_K, SUIT_HEARTS},
        {RANK_J, SUIT_HEARTS},   {RANK_10, SUIT_HEARTS},
        {RANK_A, SUIT_SPADES},   {RANK_Q, SUIT_SPADES},
        {RANK_9, SUIT_CLUBS},    {RANK_J,  SUIT_CLUBS},
        {RANK_7, SUIT_DIAMONDS}, {RANK_JOKER, SUIT_NONE},
    };
    gs->players[0].hand_count = 10;
    memcpy(gs->players[0].hand, hand0, sizeof(hand0));

    /* Seat 1 — top */
    snprintf(gs->players[1].name, sizeof(gs->players[1].name), "Alice");
    gs->players[1].hand_count = 8;

    /* Seat 2 — right */
    snprintf(gs->players[2].name, sizeof(gs->players[2].name), "Bob");
    gs->players[2].hand_count = 9;

    /* Seat 3 — left */
    snprintf(gs->players[3].name, sizeof(gs->players[3].name), "Carol");
    gs->players[3].hand_count = 9;

    /* A partial trick in progress */
    gs->trick[0]     = (Card){RANK_Q,  SUIT_HEARTS};
    gs->trick[1]     = (Card){RANK_6,  SUIT_HEARTS};
    gs->trick_count  = 2;
    gs->trick_leader = 3;
}
