#pragma once

typedef enum {
    SUIT_SPADES   = 0,
    SUIT_CLUBS    = 1,
    SUIT_DIAMONDS = 2,
    SUIT_HEARTS   = 3,
    SUIT_NONE     = 4, /* Joker */
} Suit;

typedef enum {
    RANK_JOKER = 0,
    RANK_4  =  4,
    RANK_5  =  5,
    RANK_6  =  6,
    RANK_7  =  7,
    RANK_8  =  8,
    RANK_9  =  9,
    RANK_10 = 10,
    RANK_J  = 11,
    RANK_Q  = 12,
    RANK_K  = 13,
    RANK_A  = 14,
} Rank;

typedef struct {
    Rank rank;
    Suit suit;
} Card;

/* Returns short rank string: "A","K","Q","J","10","9"… or "Jo" */
const char *card_rank_str(Rank r);

/* Returns UTF-8 suit symbol: ♠ ♣ ♦ ♥ or "" for Joker */
const char *card_suit_str(Suit s);

/* 1 for red suits (Diamonds, Hearts) */
int card_suit_is_red(Suit s);
