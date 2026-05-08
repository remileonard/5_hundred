#include "card.h"

const char *card_rank_str(Rank r)
{
    switch (r) {
    case RANK_JOKER: return "Jo";
    case RANK_A:     return "A";
    case RANK_K:     return "K";
    case RANK_Q:     return "Q";
    case RANK_J:     return "J";
    case RANK_10:    return "10";
    case RANK_9:     return "9";
    case RANK_8:     return "8";
    case RANK_7:     return "7";
    case RANK_6:     return "6";
    case RANK_5:     return "5";
    case RANK_4:     return "4";
    default:         return "?";
    }
}

const char *card_suit_str(Suit s)
{
    switch (s) {
    case SUIT_SPADES:   return "\xe2\x99\xa0"; /* ♠ */
    case SUIT_CLUBS:    return "\xe2\x99\xa3"; /* ♣ */
    case SUIT_DIAMONDS: return "\xe2\x99\xa6"; /* ♦ */
    case SUIT_HEARTS:   return "\xe2\x99\xa5"; /* ♥ */
    default:            return "";
    }
}

int card_suit_is_red(Suit s)
{
    return s == SUIT_DIAMONDS || s == SUIT_HEARTS;
}
