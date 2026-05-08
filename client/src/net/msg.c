/* msg.c — JSON protocol implementation (send helpers + receive dispatcher) */
#include "msg.h"
#include "net.h"
#include "../screen_lobby.h"
#include "game/card.h"

#include "cJSON.h"
#include <SDL2/SDL.h>
#include <string.h>
#include <stdio.h>

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Build the outer {"type":...} envelope, optionally with a payload object.
 * Caller owns the returned cJSON* and must call cJSON_Delete on it. */
static cJSON *make_msg(const char *type)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", type);
    return root;
}

/* Marshal root to JSON, send it, then free it. */
static void send_json(cJSON *root)
{
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s) return;
    net_send_text(s, (int)strlen(s));
    cJSON_free(s);
}

/* ── Send helpers ────────────────────────────────────────────────────────── */

void net_send_identify(const char *name)
{
    cJSON *root    = make_msg("identify");
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "name", name);
    cJSON_AddItemToObject(root, "payload", payload);
    send_json(root);
}

void net_send_room_list(void)
{
    send_json(make_msg("room.list"));
}

void net_send_room_create(const char *name, const char *variant)
{
    cJSON *root    = make_msg("room.create");
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "name",    name);
    cJSON_AddStringToObject(payload, "variant", variant);
    cJSON_AddItemToObject(root, "payload", payload);
    send_json(root);
}

void net_send_room_join(const char *room_id)
{
    cJSON *root    = make_msg("room.join");
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "room_id", room_id);
    cJSON_AddItemToObject(root, "payload", payload);
    send_json(root);
}

void net_send_room_leave(void)
{
    send_json(make_msg("room.leave"));
}

void net_send_room_start(void)
{
    send_json(make_msg("room.start"));
}

void net_send_bid(bool pass, int level, const char *suit)
{
    cJSON *root    = make_msg("game.bid");
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddBoolToObject(payload, "pass", pass);
    if (!pass) {
        cJSON_AddNumberToObject(payload, "level", level);
        cJSON_AddStringToObject(payload, "suit",  suit);
    }
    cJSON_AddItemToObject(root, "payload", payload);
    send_json(root);
}

void net_send_discard(Card cards[3])
{
    cJSON *root    = make_msg("game.discard");
    cJSON *payload = cJSON_CreateObject();
    cJSON *arr     = cJSON_CreateArray();
    for (int i = 0; i < 3; i++) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddNumberToObject(c, "rank", cards[i].rank);
        cJSON_AddNumberToObject(c, "suit", cards[i].suit);
        cJSON_AddItemToArray(arr, c);
    }
    cJSON_AddItemToObject(payload, "cards", arr);
    cJSON_AddItemToObject(root, "payload", payload);
    send_json(root);
}

void net_send_play(Card c)
{
    cJSON *root    = make_msg("game.play");
    cJSON *payload = cJSON_CreateObject();
    cJSON *card    = cJSON_CreateObject();
    cJSON_AddNumberToObject(card, "rank", c.rank);
    cJSON_AddNumberToObject(card, "suit", c.suit);
    cJSON_AddItemToObject(payload, "card", card);
    cJSON_AddItemToObject(root, "payload", payload);
    send_json(root);
}

/* ── Parse helpers ───────────────────────────────────────────────────────── */

static void parse_room_info(cJSON *obj, RoomInfoC *ri)
{
    memset(ri, 0, sizeof(*ri));
    cJSON *id      = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *name    = cJSON_GetObjectItemCaseSensitive(obj, "name");
    cJSON *variant = cJSON_GetObjectItemCaseSensitive(obj, "variant");
    cJSON *max     = cJSON_GetObjectItemCaseSensitive(obj, "max_seats");
    cJSON *players = cJSON_GetObjectItemCaseSensitive(obj, "players");

    if (cJSON_IsString(id))
        strncpy(ri->id,      id->valuestring,      sizeof(ri->id)      - 1);
    if (cJSON_IsString(name))
        strncpy(ri->name,    name->valuestring,    sizeof(ri->name)    - 1);
    if (cJSON_IsString(variant))
        strncpy(ri->variant, variant->valuestring, sizeof(ri->variant) - 1);
    if (cJSON_IsNumber(max))
        ri->max_seats = max->valueint;

    if (cJSON_IsArray(players)) {
        int i = 0;
        cJSON *p;
        cJSON_ArrayForEach(p, players) {
            if (i >= 4) break;
            if (cJSON_IsString(p))
                strncpy(ri->players[i], p->valuestring, 31);
            i++;
        }
        ri->player_count = i;
    }
}

static int suit_from_str(const char *s)
{
    if (!s || s[0] == '\0') return SUIT_NONE;
    if (strcmp(s, "S")  == 0) return SUIT_SPADES;
    if (strcmp(s, "C")  == 0) return SUIT_CLUBS;
    if (strcmp(s, "D")  == 0) return SUIT_DIAMONDS;
    if (strcmp(s, "H")  == 0) return SUIT_HEARTS;
    return SUIT_NONE;
}

static GamePhase phase_from_str(const char *s)
{
    if (!s) return PHASE_BIDDING;
    if (strcmp(s, "bidding")     == 0) return PHASE_BIDDING;
    if (strcmp(s, "kitty")       == 0) return PHASE_KITTY;
    if (strcmp(s, "playing")     == 0) return PHASE_PLAYING;
    if (strcmp(s, "scoring")     == 0) return PHASE_SCORING;
    if (strcmp(s, "end")         == 0) return PHASE_END;
    return PHASE_BIDDING;
}

static Card card_from_dto(cJSON *dto)
{
    Card c = {RANK_JOKER, SUIT_NONE};
    if (!dto) return c;
    cJSON *rank = cJSON_GetObjectItemCaseSensitive(dto, "rank");
    cJSON *suit = cJSON_GetObjectItemCaseSensitive(dto, "suit");
    if (cJSON_IsNumber(rank)) c.rank = (Rank)rank->valueint;
    if (cJSON_IsNumber(suit)) c.suit = (Suit)suit->valueint;
    return c;
}

static void bid_from_dto(cJSON *dto, BidInfo *bi)
{
    memset(bi, 0, sizeof(*bi));
    if (!dto) return;
    cJSON *pass  = cJSON_GetObjectItemCaseSensitive(dto, "pass");
    cJSON *level = cJSON_GetObjectItemCaseSensitive(dto, "level");
    cJSON *suit  = cJSON_GetObjectItemCaseSensitive(dto, "suit");
    bi->pass = cJSON_IsTrue(pass);
    if (cJSON_IsNumber(level))
        bi->level = level->valueint;
    if (cJSON_IsString(suit))
        strncpy(bi->suit, suit->valuestring, sizeof(bi->suit) - 1);
}

/* ── Message handlers ────────────────────────────────────────────────────── */

static void handle_welcome(App *app, cJSON *payload)
{
    if (!payload) return;
    cJSON *pid  = cJSON_GetObjectItemCaseSensitive(payload, "player_id");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(payload, "name");
    if (cJSON_IsString(pid))
        strncpy(app->net.player_id,   pid->valuestring,  sizeof(app->net.player_id)   - 1);
    if (cJSON_IsString(name))
        strncpy(app->net.player_name, name->valuestring, sizeof(app->net.player_name) - 1);
    /* Immediately request the room list */
    net_send_room_list();
}

static void handle_room_listed(App *app, cJSON *payload)
{
    screen_lobby_clear_pending();
    if (!payload) return;
    cJSON *rooms = cJSON_GetObjectItemCaseSensitive(payload, "rooms");
    if (!cJSON_IsArray(rooms)) return;
    app->net.room_count = 0;
    cJSON *r;
    cJSON_ArrayForEach(r, rooms) {
        if (app->net.room_count >= MAX_LOBBY_ROOMS) break;
        parse_room_info(r, &app->net.rooms[app->net.room_count++]);
    }
}

static void handle_room_joined(App *app, cJSON *payload)
{
    if (!payload) return;
    cJSON *room = cJSON_GetObjectItemCaseSensitive(payload, "room");
    cJSON *seat = cJSON_GetObjectItemCaseSensitive(payload, "your_seat");
    if (room) parse_room_info(room, &app->net.current_room);
    if (cJSON_IsNumber(seat)) app->net.my_seat = seat->valueint;
    app->net.in_room = true;

    /* Reset game state before entering the table screen */
    memset(&app->gs, 0, sizeof(app->gs));
    app->gs.selected_card = -1;
    app->gs.local_seat    = app->net.my_seat;

    app->screen = SCREEN_TABLE;
}

static void handle_room_updated(App *app, cJSON *payload)
{
    if (!payload) return;
    cJSON *room = cJSON_GetObjectItemCaseSensitive(payload, "room");
    if (!room) return;
    RoomInfoC updated;
    parse_room_info(room, &updated);
    /* Also refresh current_room if we're sitting in this room (waiting panel) */
    if (app->net.in_room && strcmp(app->net.current_room.id, updated.id) == 0)
        app->net.current_room = updated;
    /* Update the room in the list if present, otherwise append */
    for (int i = 0; i < app->net.room_count; i++) {
        if (strcmp(app->net.rooms[i].id, updated.id) == 0) {
            app->net.rooms[i] = updated;
            return;
        }
    }
    if (app->net.room_count < MAX_LOBBY_ROOMS)
        app->net.rooms[app->net.room_count++] = updated;
}

static void handle_game_state(App *app, cJSON *payload)
{
    if (!payload) return;
    ClientGameState *gs = &app->gs;

    /* Phase */
    cJSON *phase = cJSON_GetObjectItemCaseSensitive(payload, "phase");
    if (cJSON_IsString(phase))
        gs->phase = phase_from_str(phase->valuestring);

    /* to_act, contractor */
    cJSON *to_act     = cJSON_GetObjectItemCaseSensitive(payload, "to_act");
    cJSON *contractor = cJSON_GetObjectItemCaseSensitive(payload, "contractor");
    if (cJSON_IsNumber(to_act))     gs->to_act     = to_act->valueint;
    if (cJSON_IsNumber(contractor)) gs->contractor = contractor->valueint;

    /* trump */
    cJSON *trump = cJSON_GetObjectItemCaseSensitive(payload, "trump");
    if (cJSON_IsString(trump))
        gs->trump_suit = suit_from_str(trump->valuestring);

    /* contract */
    cJSON *contract = cJSON_GetObjectItemCaseSensitive(payload, "contract");
    bid_from_dto(contract, &gs->contract);

    /* scores */
    cJSON *scores = cJSON_GetObjectItemCaseSensitive(payload, "scores");
    if (cJSON_IsArray(scores)) {
        int idx = 0;
        cJSON *s;
        cJSON_ArrayForEach(s, scores) {
            if (idx < 2 && cJSON_IsNumber(s))
                gs->scores[idx++] = s->valueint;
        }
    }

    /* players */
    cJSON *players = cJSON_GetObjectItemCaseSensitive(payload, "players");
    if (cJSON_IsArray(players)) {
        int i = 0;
        cJSON *p;
        cJSON_ArrayForEach(p, players) {
            if (i >= MAX_PLAYERS) break;
            PlayerSlot *slot = &gs->players[i];
            cJSON *pname  = cJSON_GetObjectItemCaseSensitive(p, "name");
            cJSON *hcount = cJSON_GetObjectItemCaseSensitive(p, "hand_count");
            cJSON *tricks = cJSON_GetObjectItemCaseSensitive(p, "tricks_won");
            cJSON *hand   = cJSON_GetObjectItemCaseSensitive(p, "hand");
            cJSON *tab    = cJSON_GetObjectItemCaseSensitive(p, "tableau");
            cJSON *tcount = cJSON_GetObjectItemCaseSensitive(p, "tableau_count");

            if (cJSON_IsString(pname))
                strncpy(slot->name, pname->valuestring, sizeof(slot->name) - 1);
            if (cJSON_IsNumber(hcount))
                slot->hand_count = hcount->valueint;
            if (cJSON_IsNumber(tricks))
                slot->tricks_won = tricks->valueint;

            /* Own hand (server only sends hand[] for the local seat) */
            if (cJSON_IsArray(hand)) {
                slot->hand_count = 0;
                cJSON *c;
                cJSON_ArrayForEach(c, hand) {
                    if (slot->hand_count >= MAX_HAND) break;
                    slot->hand[slot->hand_count++] = card_from_dto(c);
                }
            }

            /* Tableau */
            if (cJSON_IsArray(tab)) {
                slot->tableau_count = 0;
                cJSON *c;
                cJSON_ArrayForEach(c, tab) {
                    if (slot->tableau_count >= 10) break;
                    slot->tableau[slot->tableau_count++] = card_from_dto(c);
                }
            } else if (cJSON_IsNumber(tcount)) {
                slot->tableau_count = tcount->valueint;
            }
            i++;
        }
        gs->num_players = i;
    }

    /* Current trick */
    cJSON *trick        = cJSON_GetObjectItemCaseSensitive(payload, "trick");
    cJSON *trick_leader = cJSON_GetObjectItemCaseSensitive(payload, "trick_leader");
    if (cJSON_IsNumber(trick_leader))
        gs->trick_leader = trick_leader->valueint;
    if (cJSON_IsArray(trick)) {
        gs->trick_count = 0;
        cJSON *c;
        cJSON_ArrayForEach(c, trick) {
            if (gs->trick_count >= MAX_TRICK) break;
            gs->trick[gs->trick_count++] = card_from_dto(c);
        }
    }

    /* Last completed trick — shown between turns when trick_count == 0 */
    cJSON *last_trick        = cJSON_GetObjectItemCaseSensitive(payload, "last_trick");
    cJSON *last_trick_leader = cJSON_GetObjectItemCaseSensitive(payload, "last_trick_leader");
    if (cJSON_IsNumber(last_trick_leader))
        gs->last_trick_leader = last_trick_leader->valueint;
    if (cJSON_IsArray(last_trick)) {
        gs->last_trick_count = 0;
        cJSON *c;
        cJSON_ArrayForEach(c, last_trick) {
            if (gs->last_trick_count >= MAX_TRICK) break;
            gs->last_trick[gs->last_trick_count++] = card_from_dto(c);
        }
    } else {
        gs->last_trick_count = 0;
    }

    /* Bid history */
    cJSON *bids = cJSON_GetObjectItemCaseSensitive(payload, "bids");
    if (cJSON_IsArray(bids)) {
        gs->bid_count = 0;
        cJSON *b;
        cJSON_ArrayForEach(b, bids) {
            if (gs->bid_count >= MAX_BIDS) break;
            bid_from_dto(b, &gs->bids[gs->bid_count++]);
        }
    }

    /* Kitty (only sent to contractor during kitty phase) */
    cJSON *kitty = cJSON_GetObjectItemCaseSensitive(payload, "kitty");
    if (cJSON_IsArray(kitty)) {
        gs->kitty_count = 0;
        cJSON *c;
        cJSON_ArrayForEach(c, kitty) {
            if (gs->kitty_count >= 3) break;
            gs->kitty[gs->kitty_count++] = card_from_dto(c);
        }
    } else {
        gs->kitty_count = 0;
    }

    /* 2-player hand type (0=hand, 1=tableau) */
    cJSON *ht = cJSON_GetObjectItemCaseSensitive(payload, "two_player_hand_type");
    if (cJSON_IsNumber(ht))
        gs->two_player_hand_type = ht->valueint;

    /* Preserve card selection when it's still our turn in playing phase
     * (e.g. a bot played in another seat but it's back to us). */
    if (gs->phase != PHASE_PLAYING || gs->to_act != gs->local_seat)
        gs->selected_card = -1;
}

static void handle_error(App *app, cJSON *payload)
{
    screen_lobby_clear_pending();
    if (!payload) return;
    cJSON *msg = cJSON_GetObjectItemCaseSensitive(payload, "message");
    if (cJSON_IsString(msg)) {
        fprintf(stderr, "server error: %s\n", msg->valuestring);
        /* Show as on-screen toast for 3 seconds */
        strncpy(app->gs.error_msg, msg->valuestring, sizeof(app->gs.error_msg) - 1);
        app->gs.error_until = SDL_GetTicks() + 3000;
    }
}

static void handle_game_event(App *app, cJSON *payload)
{
    (void)app;
    if (!payload) return;
    cJSON *event = cJSON_GetObjectItemCaseSensitive(payload, "event");
    if (!cJSON_IsString(event)) return;
    /* game_over and game_start are handled via the accompanying game.state */
    /* Additional event types (chat, etc.) can be dispatched here. */
    fprintf(stderr, "game event: %s\n", event->valuestring);
}

/* ── Receive dispatcher ──────────────────────────────────────────────────── */

void net_handle_message(App *app, const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, (size_t)len);
    if (!root) {
        fprintf(stderr, "net: failed to parse JSON message\n");
        return;
    }

    cJSON *type    = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");

    if (!cJSON_IsString(type)) {
        cJSON_Delete(root);
        return;
    }

    const char *t = type->valuestring;
    if      (strcmp(t, "welcome")      == 0) handle_welcome(app, payload);
    else if (strcmp(t, "room.listed")  == 0) handle_room_listed(app, payload);
    else if (strcmp(t, "room.joined")  == 0) handle_room_joined(app, payload);
    else if (strcmp(t, "room.updated") == 0) handle_room_updated(app, payload);
    else if (strcmp(t, "game.state")   == 0) handle_game_state(app, payload);
    else if (strcmp(t, "game.event")   == 0) handle_game_event(app, payload);
    else if (strcmp(t, "error")        == 0) handle_error(app, payload);

    cJSON_Delete(root);
}
