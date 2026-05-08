#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "game/card.h"

#define MAX_HAND    13
#define MAX_PLAYERS  4
#define MAX_TRICK    4
#define MAX_BIDS    48

/* ── Game phase ─────────────────────────────────────────────────────────── */

typedef enum {
    PHASE_BIDDING = 0,
    PHASE_KITTY,
    PHASE_PLAYING,
    PHASE_SCORING,
    PHASE_END,
} GamePhase;

/* ── Bid info ────────────────────────────────────────────────────────────── */

typedef struct {
    bool pass;
    int  level;      /* 6–10 */
    char suit[16];   /* "S","C","D","H","NT","Misere","OpenMisere" */
} BidInfo;

/* ── Player slot ─────────────────────────────────────────────────────────── */

typedef struct {
    char name[32];
    Card hand[MAX_HAND];
    int  hand_count;
    /* 2-player tableau: [0..4]=face-down, [5..9]=face-up */
    Card tableau[10];
    int  tableau_count; /* 0 or 10 */
    int  tricks_won;
} PlayerSlot;

/* ── Full client game state ──────────────────────────────────────────────── */

typedef struct {
    int        num_players;  /* 2 or 4 */
    PlayerSlot players[MAX_PLAYERS];
    int        local_seat;   /* which seat is the local player (0-based) */

    GamePhase  phase;
    int        to_act;       /* seat whose turn it is */
    BidInfo    contract;     /* the contract being played */
    int        contractor;   /* seat of the contractor */
    BidInfo    bids[MAX_BIDS];
    int        bid_count;

    Card trick[MAX_TRICK];
    int  trick_count;        /* cards in the current (in-progress) trick */
    int  trick_leader;       /* seat index of trick leader */

    /* Most recently completed trick — shown between turns when trick_count == 0. */
    Card last_trick[MAX_TRICK];
    int  last_trick_count;
    int  last_trick_leader;

    Card kitty[3];           /* kitty cards, visible to contractor during PHASE_KITTY */
    int  kitty_count;        /* 0 or 3 */

    int  scores[2];          /* Team 0, Team 1 */
    int  selected_card;      /* index in local hand, -1 = none */
    int  trump_suit;         /* Suit value, SUIT_NONE = no-trumps */

    /* 2-player variant: which source is currently active within the combined trick.
     * 0 = private hand, 1 = tableau.
     * When trick_count == 0 (leader's first play), both sources are available. */
    int  two_player_hand_type;

    /* UI feedback */
    char     error_msg[128];   /* last server error, empty = none */
    uint32_t error_until;      /* SDL_GetTicks() value after which toast clears */
} ClientGameState;

/* Populate with a fake demo hand for visual testing */
void game_state_init_demo(ClientGameState *gs);
