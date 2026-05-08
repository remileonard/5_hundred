#pragma once
#include "app.h"
#include "game/card.h"
#include <stdbool.h>

/* ── Send helpers ────────────────────────────────────────────────────────── */

void net_send_identify(const char *name);
void net_send_room_list(void);
void net_send_room_create(const char *name, const char *variant); /* "4p"|"2p" */
void net_send_room_join(const char *room_id);
void net_send_room_leave(void);
void net_send_room_start(void);                             /* fill bots + start */

/* Game actions */
void net_send_bid(bool pass, int level, const char *suit); /* suit: "S","C","D","H","NT","Misere","OpenMisere" */
void net_send_discard(Card cards[3]);
void net_send_play(Card c);
void net_send_choose_hand(int hand_type); /* 2-player: 0=private hand, 1=tableau */

/* ── Receive dispatcher ───────────────────────────────────────────────────── */

/* Parse one incoming JSON text frame and update app state accordingly. */
void net_handle_message(App *app, const char *data, int len);
