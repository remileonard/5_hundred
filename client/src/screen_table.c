/* screen_table.c — In-game table screen (rendering + events).
 *
 * Phase-aware layout:
 *  • No game yet      : waiting panel + "Démarrer avec bots" button
 *  • PHASE_BIDDING    : bid grid (my turn) or bid history + waiting indicator
 *  • PHASE_KITTY      : extended hand (hand + kitty) with discard selection
 *  • PHASE_PLAYING    : hand + trick + "Jouer" button (my turn)
 *  • PHASE_END        : result overlay + "Nouvelle partie" button
 */
#include "screen_table.h"
#include "ui/ui.h"
#include "render/card_render.h"
#include "net/msg.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

/* ── Layout constants ────────────────────────────────────────────────────────── */

/* Local player's hand: fanned along the bottom */
#define HAND_Y        (WINDOW_H - CARD_H - 30)
#define HAND_FAN_X    20
#define HAND_OVERLAP  52
#define CARD_SELECTED_LIFT 18

/* 2-player tableau rows */
#define TABLEAU_COL_W     (CARD_W + 10)   /* column pitch */
#define TABLEAU_LOCAL_Y   (HAND_Y - CARD_H - 16)
#define TABLEAU_OPP_Y     (30 + CARD_H + 14)

/* A zero Card{rank=0, suit=0} (Joker/Spades) is the empty-slot sentinel used
 * by the server. The real Joker has suit=-1 (stored as SUIT_NONE / -1 on
 * the wire, different from SUIT_SPADES=0). */
#define CARD_IS_EMPTY(c)  ((c).rank == RANK_JOKER && (c).suit == SUIT_SPADES)

/* Trick area: centred */
#define TRICK_CX  (WINDOW_W / 2)
#define TRICK_CY  (WINDOW_H / 2 - 20)
#define TRICK_SPREAD 90

/* Opponent card areas */
#define OPP_BACK_OVERLAP 22

/* Bid grid */
#define BID_BTN_W  62
#define BID_BTN_H  34
#define BID_GAP     4
#define BID_COLS    5
#define BID_ROWS    5

/* Bid panel dimensions (shared between centred and right-side layouts) */
#define BID_PANEL_W           436  /* total panel width including padding */
#define BID_PANEL_H           260  /* total panel height (4-player) */
#define BID_PANEL_PAD_X        55  /* horizontal padding between panel edge and grid */
#define BID_PANEL_PAD_TOP      36  /* vertical space from panel top to grid top (4-player) */
#define BID_PANEL_2P_PAD_TOP    6  /* smaller top pad for 2-player so panel fits in tableau gap */
#define BID_PANEL_MARGIN        6  /* gap between panel bottom and the hand row (4-player) */
/* Total span of the three special buttons (Misère + 8-px gap + Open Misère +
 * 8-px gap + Passer = 120 + 8 + 140 + 8 + 90). Wider than the bid grid (326)
 * so it must be centred separately. */
#define BID_SPECIALS_W    366

/* Bottom action panel Y */
#define ACTION_PANEL_Y  (HAND_Y - BID_PANEL_H)

/* ── Bid numeric value (mirrors server) ──────────────────────────────────────── */

static int bid_value(int level, int suit) { return (level - 6) * 5 * 10 + suit * 10; }
static int misere_value(void)             { return 250; }
static int open_misere_value(void)        { return 500; }

static int contract_value(ClientGameState *gs)
{
    if (gs->contract.pass || gs->contract.level == 0) return -1;
    const char *s = gs->contract.suit;
    int suit = 0;
    if      (strcmp(s, "C")          == 0) suit = 1;
    else if (strcmp(s, "D")          == 0) suit = 2;
    else if (strcmp(s, "H")          == 0) suit = 3;
    else if (strcmp(s, "NT")         == 0) suit = 4;
    else if (strcmp(s, "Misere")     == 0) return misere_value();
    else if (strcmp(s, "OpenMisere") == 0) return open_misere_value();
    return bid_value(gs->contract.level, suit);
}

/* ── Static buttons ──────────────────────────────────────────────────────────── */

static Button btn_lobby      = { {30, 30, 120, 44},   "\u2190 Lobby",          false };
static Button btn_start_bots = { {WINDOW_W/2-120, WINDOW_H/2+10, 240, 44},
                                  "D\u00e9marrer avec bots", false };
static Button btn_play       = { {WINDOW_W/2-60, HAND_Y-52, 120, 40},
                                  "Jouer \u25b6",             false };
static Button btn_discard    = { {WINDOW_W/2-70, HAND_Y-52, 140, 40},
                                  "D\u00e9fausser (0/3)",    false };
static Button btn_pass;
static Button btn_nouvelle   = { {WINDOW_W/2-100, WINDOW_H/2+60, 200, 44},
                                  "Nouvelle partie",         false };

static Button s_bid[BID_ROWS][BID_COLS];
static Button s_bid_misere;
static Button s_bid_open_misere;
static char   s_bid_labels[BID_ROWS][BID_COLS][8];
static SDL_Rect s_bid_panel_rect;

static const char *SUIT_LABELS[5] = { "\u2660", "\u2663", "\u2666", "\u2665", "NT" };

/* Recompute bid button positions every call.  The panel position depends on
 * gs->num_players which can change between games, so we cannot cache once. */
static void init_action_buttons(App *app)
{
    int grid_x, grid_y;

    if (app->gs.num_players == 2) {
        /* 2-player: panel centred horizontally and vertically in the gap
         * between the two tableau rows.  Equal top/bottom padding
         * (BID_PANEL_2P_PAD_TOP each side) keeps the black border symmetric.
         * panel_h = 6 + 224 + 6 = 236 px, fits in the 246 px gap with 5 px
         * clearance above and below each tableau row. */
        int panel_h = 2 * BID_PANEL_2P_PAD_TOP + BID_ROWS * (BID_BTN_H + BID_GAP) + BID_BTN_H;
        int panel_y = (TABLEAU_OPP_Y + CARD_H + TABLEAU_LOCAL_Y) / 2 - panel_h / 2;
        s_bid_panel_rect = (SDL_Rect){ WINDOW_W / 2 - BID_PANEL_W / 2,
                                       panel_y,
                                       BID_PANEL_W, panel_h };
        grid_x = s_bid_panel_rect.x + BID_PANEL_PAD_X;
        grid_y = panel_y + BID_PANEL_2P_PAD_TOP;
    } else {
        /* 4-player (and default): centred at the bottom */
        s_bid_panel_rect = (SDL_Rect){ WINDOW_W/2 - BID_PANEL_W/2,
                                       ACTION_PANEL_Y - BID_PANEL_MARGIN,
                                       BID_PANEL_W, BID_PANEL_H };
        grid_x = s_bid_panel_rect.x + BID_PANEL_PAD_X;
        grid_y = s_bid_panel_rect.y + BID_PANEL_PAD_TOP;
    }

    for (int row = 0; row < BID_ROWS; row++) {
        for (int col = 0; col < BID_COLS; col++) {
            snprintf(s_bid_labels[row][col], sizeof(s_bid_labels[0][0]),
                     "%d%s", row + 6, SUIT_LABELS[col]);
            s_bid[row][col] = (Button){
                { grid_x + col * (BID_BTN_W + BID_GAP),
                  grid_y + row * (BID_BTN_H + BID_GAP),
                  BID_BTN_W, BID_BTN_H },
                s_bid_labels[row][col], false
            };
        }
    }
    /* specials_y: immediately below the grid (no extra gap to avoid overflowing
     * the panel background).  specials_x: centred within the panel independent
     * of grid_x, because the three buttons together (366 px) are wider than the
     * bid grid (326 px). */
    int specials_y = grid_y + BID_ROWS * (BID_BTN_H + BID_GAP);
    int specials_x = s_bid_panel_rect.x + (BID_PANEL_W - BID_SPECIALS_W) / 2;
    s_bid_misere      = (Button){ {specials_x,       specials_y, 120, BID_BTN_H}, "Mis\u00e8re",      false };
    s_bid_open_misere = (Button){ {specials_x + 128, specials_y, 140, BID_BTN_H}, "Open Mis\u00e8re", false };
    btn_pass          = (Button){ {specials_x + 276, specials_y,  90, BID_BTN_H}, "Passer",           false };
}

/* ── Discard selection ───────────────────────────────────────────────────────── */

static int  s_discard_sel   = 0;   /* bitmask: bits 0..9 = hand, 10..12 = kitty */
static int  s_discard_count = 0;

static void discard_reset(void)  { s_discard_sel = 0; s_discard_count = 0; }
static bool discard_has(int i)   { return (s_discard_sel >> i) & 1; }
static void discard_toggle(int i)
{
    if (discard_has(i)) {
        s_discard_sel &= ~(1 << i);
        s_discard_count--;
    } else if (s_discard_count < 3) {
        s_discard_sel |= (1 << i);
        s_discard_count++;
    }
}

/* ── Drawing helpers ─────────────────────────────────────────────────────────── */

static void draw_card_backs_h(SDL_Renderer *r, int cx, int cy, int n)
{
    if (n <= 0) return;
    int total_w = CARD_W + (n - 1) * OPP_BACK_OVERLAP;
    int sx = cx - total_w / 2;
    for (int i = 0; i < n; i++)
        card_draw_back(r, sx + i * OPP_BACK_OVERLAP, cy - CARD_H / 2);
}

static void draw_card_backs_v(SDL_Renderer *r, int cx, int cy, int n)
{
    if (n <= 0) return;
    int total_h = CARD_H + (n - 1) * OPP_BACK_OVERLAP;
    int sy = cy - total_h / 2;
    for (int i = 0; i < n; i++)
        card_draw_back(r, cx - CARD_W / 2, sy + i * OPP_BACK_OVERLAP);
}

static void draw_score(App *app)
{
    ClientGameState *gs = &app->gs;
    char buf[64];
    snprintf(buf, sizeof(buf), "\u00c9quipe A: %d   \u00c9quipe B: %d",
             gs->scores[0], gs->scores[1]);
    /* Top-right corner, away from opponent cards and the lobby button */
    SDL_Rect r = {WINDOW_W - 280, 8, 272, 20};
    ui_text_centered(app->renderer, app->font_sm, buf, r, COL_WHITE);

    /* During play, show tricks won this round for each team */
    if (gs->phase == PHASE_PLAYING && gs->num_players > 0) {
        int tricks_a, tricks_b;
        if (gs->num_players == 4) {
            tricks_a = gs->players[0].tricks_won + gs->players[2].tricks_won;
            tricks_b = gs->players[1].tricks_won + gs->players[3].tricks_won;
        } else {
            tricks_a = gs->players[0].tricks_won;
            tricks_b = gs->players[1].tricks_won;
        }
        char tw[64];
        snprintf(tw, sizeof(tw), "Plis A: %d   Plis B: %d", tricks_a, tricks_b);
        SDL_Rect tr = {WINDOW_W - 280, 28, 272, 20};
        ui_text_centered(app->renderer, app->font_sm, tw, tr,
                         (SDL_Color){180, 200, 230, 255});
    }
}

static void draw_trick(App *app)
{
    ClientGameState *gs = &app->gs;
    if (gs->num_players == 0) return;

    /* Prefer the in-progress trick; fall back to the last completed trick so
     * all four cards remain visible between turns. */
    const Card *cards;
    int         count;
    int         leader;
    if (gs->trick_count > 0) {
        cards  = gs->trick;
        count  = gs->trick_count;
        leader = gs->trick_leader;
    } else if (gs->last_trick_count > 0) {
        cards  = gs->last_trick;
        count  = gs->last_trick_count;
        leader = gs->last_trick_leader;
    } else {
        return;
    }

    static const int off_x[4] = { 0,  0,  1, -1 };
    static const int off_y[4] = { 1, -1,  0,  0 };
    for (int i = 0; i < count; i++) {
        int virt_seat;
        if (gs->num_players == 2) {
            /* In 2-player, a combined trick has up to 4 plays (2 sub-plays).
             * Map each play index to a virtual 4-player position so cards
             * spread across all four directions and never overlap:
             *   play 0 (sub1 leader)   → bottom (virtual seat 0)
             *   play 1 (sub1 follower) → top    (virtual seat 1)
             *   play 2 (sub2 leader)   → right  (virtual seat 2)
             *   play 3 (sub2 follower) → left   (virtual seat 3)
             * For the local player's seat adjust the mapping so their card
             * lands at the bottom (virtual seat 0 = local). */
            int local = gs->local_seat;
            int first_play_is_local = (leader == local) ? 1 : 0;
            if (first_play_is_local) {
                /* leader is local: plays 0,2 are local (bottom/right), 1,3 are opponent (top/left) */
                static const int virt_local_first[4] = {0, 1, 2, 3};
                virt_seat = virt_local_first[i];
            } else {
                /* leader is opponent: plays 0,2 are opponent, 1,3 are local
                 * swap so local always lands at bottom */
                static const int virt_opp_first[4] = {1, 0, 3, 2};
                virt_seat = virt_opp_first[i];
            }
        } else {
            virt_seat = (leader + i) % gs->num_players;
        }
        int tx = TRICK_CX + off_x[virt_seat] * TRICK_SPREAD - CARD_W / 2;
        int ty = TRICK_CY + off_y[virt_seat] * (TRICK_SPREAD * 2 / 3) - CARD_H / 2;
        card_draw(app->renderer, app->font_md, app->font_sm, cards[i], tx, ty);
    }
}

/* Effective suit of a card given trump (int). Returns int. */
static int card_eff_suit(Card c, int trump)
{
    static const int sister[4] = { SUIT_CLUBS, SUIT_SPADES, SUIT_HEARTS, SUIT_DIAMONDS };
    if ((int)c.rank == RANK_JOKER) return trump;
    if (trump >= 0 && trump < 4
            && (int)c.rank == RANK_J
            && (int)c.suit != trump
            && (int)c.suit == sister[trump])
        return trump;  /* left bower */
    return (int)c.suit;
}

/* Determine if a card in the local hand is legally playable right now.
 * Mirrors the server follow-suit rule on the client for visual feedback.
 *
 * 2-player combined-trick steps (trick_count = cards played in current trick):
 *   step 0 (trick_count==0): leader plays from forced source → free within that source.
 *     Exception: first trick ever (tricks_completed==0) → both sources free.
 *   step 1 (trick_count==1): follower, same source as step 0; must follow suit of trick[0].
 *   step 2 (trick_count==2): SAME leader leads the other source → free play (no follow-suit).
 *   step 3 (trick_count==3): follower of step 2; must follow suit of trick[2]. */
static bool is_playable(ClientGameState *gs, int card_idx)
{
    if (gs->phase != PHASE_PLAYING) return true;
    if (gs->to_act != gs->local_seat) return false;

    PlayerSlot *p = &gs->players[gs->local_seat];

    /* The Joker is always playable regardless of follow-suit obligations. */
    if ((int)p->hand[card_idx].rank == RANK_JOKER) return true;

    /* In 2-player, when tableau is forced and it's not a free-choice trick, hand is off. */
    bool free_choice = (gs->trick_count == 0 && gs->tricks_completed == 0);
    if (gs->num_players == 2 && gs->two_player_hand_type == 1 && !free_choice) return false;
    if (gs->trick_count == 0) return true;  /* leading at step 0 — any hand card */

    /* Step 2 in 2-player: same combined-trick leader leads the other (hand) source.
     * Detected by trick_count==2 and to_act==trick_leader (same leader as step 0).
     * No follow-suit constraint: the player is leading, not following. */
    if (gs->num_players == 2 && gs->trick_count == 2
            && gs->to_act == gs->trick_leader)
        return true;

    int n = p->hand_count;
    int trump = gs->trump_suit;
    /* Follow-suit rule: always reference trick[0] (the card led at step 0).
     * The combined trick is one unit — no sub-tricks — so the suit led at the
     * start governs all follow-suit obligations (steps 1 and 3). */
    int led_idx = 0;
    int led_eff = card_eff_suit(gs->trick[led_idx], trump);

    /* Check if player has any non-Joker card of the led effective suit (hand only now) */
    bool has_led_suit = false;
    for (int i = 0; i < n; i++) {
        if ((int)p->hand[i].rank == RANK_JOKER) continue; /* Joker never satisfies suit */
        if (card_eff_suit(p->hand[i], trump) == led_eff) { has_led_suit = true; break; }
    }
    if (!has_led_suit) return true;  /* no matching suit — can play anything */

    return (card_eff_suit(p->hand[card_idx], trump) == led_eff);
}

static void draw_local_hand(App *app)
{
    ClientGameState *gs = &app->gs;
    PlayerSlot *p = &gs->players[gs->local_seat];
    int n = p->hand_count;
    if (n == 0) return;
    int total_w = CARD_W + (n - 1) * HAND_OVERLAP;
    int sx = WINDOW_W / 2 - total_w / 2;
    bool my_turn = (gs->phase == PHASE_PLAYING && gs->to_act == gs->local_seat);
    for (int i = 0; i < n; i++) {
        int x = sx + i * HAND_OVERLAP;
        int y = HAND_Y - (i == gs->selected_card ? CARD_SELECTED_LIFT : 0);
        card_draw(app->renderer, app->font_md, app->font_sm, p->hand[i], x, y);
        /* Dim unplayable cards when it's our turn */
        if (my_turn && !is_playable(gs, i)) {
            SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 140);
            SDL_Rect dim = {x, HAND_Y, CARD_W, CARD_H};
            SDL_RenderFillRect(app->renderer, &dim);
            SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_NONE);
        }
        if (i == gs->selected_card) {
            SDL_SetRenderDrawColor(app->renderer, 255, 220, 50, 180);
            SDL_Rect sel = {x - 2, y - 2, CARD_W + 4, CARD_H + 4};
            SDL_RenderDrawRect(app->renderer, &sel);
        }
    }
}

/* Draw combined hand + kitty for discard selection. */
static void draw_kitty_hand(App *app)
{
    ClientGameState *gs = &app->gs;
    PlayerSlot *p = &gs->players[gs->local_seat];
    int hand_n  = p->hand_count;
    int kitty_n = gs->kitty_count;
    int total   = hand_n + kitty_n;
    if (total == 0) return;
    int overlap = (total > 10) ? 48 : HAND_OVERLAP;
    int total_w = CARD_W + (total - 1) * overlap;
    int sx = WINDOW_W / 2 - total_w / 2;
    for (int i = 0; i < total; i++) {
        Card c = (i < hand_n) ? p->hand[i] : gs->kitty[i - hand_n];
        bool selected = discard_has(i);
        int x = sx + i * overlap;
        int y = HAND_Y - (selected ? CARD_SELECTED_LIFT : 0);
        if (i >= hand_n) {  /* kitty highlight */
            SDL_SetRenderDrawColor(app->renderer, 30, 160, 160, 80);
            SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
            SDL_Rect bg = {x - 2, y - 2, CARD_W + 4, CARD_H + 4};
            SDL_RenderFillRect(app->renderer, &bg);
            SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_NONE);
        }
        card_draw(app->renderer, app->font_md, app->font_sm, c, x, y);
        if (selected) {
            SDL_SetRenderDrawColor(app->renderer, 255, 60, 60, 200);
            SDL_Rect sel = {x - 2, y - 2, CARD_W + 4, CARD_H + 4};
            SDL_RenderDrawRect(app->renderer, &sel);
        }
    }
}

/* ── 2-player tableau helpers ───────────────────────────────────────────────── */

/* Draw a 2-player tableau row for `seat` at vertical position `row_y`.
 * `is_local` enables selection highlighting.
 * Tableau layout (server): [0..4] = face-down (only sent to owner), [5..9] = face-up (public). */
static void draw_tableau(App *app, int seat, int row_y, bool is_local)
{
    ClientGameState *gs = &app->gs;
    PlayerSlot *p = &gs->players[seat];
    if (p->tableau_count == 0) return;

    int total_w = 5 * TABLEAU_COL_W - 10;
    int sx = WINDOW_W / 2 - total_w / 2;

    for (int col = 0; col < 5; col++) {
        int x = sx + col * TABLEAU_COL_W;
        Card facedown = p->tableau[col];       /* [0..4] */
        Card faceup   = p->tableau[5 + col];   /* [5..9] */
        bool fd_empty = CARD_IS_EMPTY(facedown);
        bool fu_empty = CARD_IS_EMPTY(faceup);

        /* Draw face-down indicator (back, slightly offset) */
        bool show_fd = is_local ? !fd_empty
                                : !fu_empty;  /* opponent: assume fd exists if fu exists */
        if (show_fd)
            card_draw_back(app->renderer, x + 3, row_y + 3);

        if (!fu_empty) {
            bool sel = (is_local && gs->selected_card == 100 + col);
            int y = row_y - (sel ? CARD_SELECTED_LIFT : 0);
            card_draw(app->renderer, app->font_md, app->font_sm, faceup, x, y);
            if (sel) {
                SDL_SetRenderDrawColor(app->renderer, 255, 220, 50, 180);
                SDL_Rect sr = {x - 2, y - 2, CARD_W + 4, CARD_H + 4};
                SDL_RenderDrawRect(app->renderer, &sr);
            }
        } else if (fd_empty) {
            /* Empty column — faint frame */
            SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(app->renderer, 80, 100, 80, 80);
            SDL_Rect fr = {x, row_y, CARD_W, CARD_H};
            SDL_RenderDrawRect(app->renderer, &fr);
            SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_NONE);
        }
    }
}

/* Hit-test the local player's tableau face-up cards.
 * Returns column index 0-4, or -1 if no hit. */
static int local_tableau_hit(ClientGameState *gs, int mx, int my)
{
    PlayerSlot *p = &gs->players[gs->local_seat];
    if (p->tableau_count == 0) return -1;
    int hit_y_min = TABLEAU_LOCAL_Y - CARD_SELECTED_LIFT;
    int total_w = 5 * TABLEAU_COL_W - 10;
    int sx = WINDOW_W / 2 - total_w / 2;
    for (int col = 0; col < 5; col++) {
        Card fu = p->tableau[5 + col];
        if (CARD_IS_EMPTY(fu)) continue;  /* empty column not clickable */
        int x = sx + col * TABLEAU_COL_W;
        if (mx >= x && mx < x + CARD_W && my >= hit_y_min && my < TABLEAU_LOCAL_Y + CARD_H)
            return col;
    }
    return -1;
}

/* Draw an amber glow/border around the active source (hand or tableau row)
 * when it's the local player's turn in 2-player mode.
 * Call this BEFORE drawing the hand/tableau so the glow appears behind the cards. */
static void draw_source_highlight_2p(App *app)
{
    ClientGameState *gs = &app->gs;
    if (gs->num_players != 2) return;
    if (gs->phase != PHASE_PLAYING) return;
    if (gs->to_act != gs->local_seat) return;

    bool free_choice    = (gs->trick_count == 0 && gs->tricks_completed == 0);
    bool hand_active    = (free_choice || gs->two_player_hand_type == 0);
    bool tableau_active = (free_choice || gs->two_player_hand_type == 1);

    PlayerSlot *p = &gs->players[gs->local_seat];
    SDL_Renderer *r = app->renderer;

    /* Semi-transparent amber fill behind the active area */
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 255, 200, 30, 55);

    if (hand_active && p->hand_count > 0) {
        int total_w = CARD_W + (p->hand_count - 1) * HAND_OVERLAP;
        int sx = WINDOW_W / 2 - total_w / 2;
        SDL_Rect hr = {sx - 6, HAND_Y - 6, total_w + 12, CARD_H + 12};
        SDL_RenderFillRect(r, &hr);
    }
    if (tableau_active && p->tableau_count > 0) {
        int tw = 5 * TABLEAU_COL_W - 10;
        int sx = WINDOW_W / 2 - tw / 2;
        SDL_Rect tr = {sx - 6, TABLEAU_LOCAL_Y - 6, tw + CARD_W + 12, CARD_H + 12};
        SDL_RenderFillRect(r, &tr);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    /* Amber outline border */
    SDL_SetRenderDrawColor(r, 255, 200, 30, 255);
    if (hand_active && p->hand_count > 0) {
        int total_w = CARD_W + (p->hand_count - 1) * HAND_OVERLAP;
        int sx = WINDOW_W / 2 - total_w / 2;
        SDL_Rect hr = {sx - 6, HAND_Y - 6, total_w + 12, CARD_H + 12};
        SDL_RenderDrawRect(r, &hr);
    }
    if (tableau_active && p->tableau_count > 0) {
        int tw = 5 * TABLEAU_COL_W - 10;
        int sx = WINDOW_W / 2 - tw / 2;
        SDL_Rect tr = {sx - 6, TABLEAU_LOCAL_Y - 6, tw + CARD_W + 12, CARD_H + 12};
        SDL_RenderDrawRect(r, &tr);
    }
}

/* Draw source/step instruction and trick-winner notification for 2-player mode.
 * Placed in the gap between the trick area and the local tableau/hand. */
static void draw_2p_play_hint(App *app)
{
    ClientGameState *gs = &app->gs;
    if (gs->num_players != 2) return;
    if (gs->phase != PHASE_PLAYING) return;

    SDL_Renderer *r = app->renderer;
    bool my_turn = (gs->to_act == gs->local_seat);

    /* ── Trick winner notification ─────────────────────────────────────────── */
    /* When trick_count==0 and there have been completed tricks, the current
     * trick_leader is the winner of the last combined trick (they lead next). */
    if (gs->trick_count == 0 && gs->tricks_completed > 0) {
        bool i_won = (gs->trick_leader == gs->local_seat);
        char wbuf[80];
        if (i_won)
            snprintf(wbuf, sizeof(wbuf),
                     "Pli %d : Vous avez remport\u00e9 !", gs->tricks_completed);
        else
            snprintf(wbuf, sizeof(wbuf),
                     "Pli %d : %s a remport\u00e9",
                     gs->tricks_completed,
                     gs->players[gs->trick_leader].name);
        SDL_Color wc = i_won ? (SDL_Color){100, 230, 100, 255}
                             : (SDL_Color){220, 160, 60, 255};
        SDL_Rect wr = {0, TABLEAU_LOCAL_Y - 48, WINDOW_W, 20};
        ui_text_centered(r, app->font_sm, wbuf, wr, wc);
    }

    /* ── Source + step instruction ─────────────────────────────────────────── */
    if (!my_turn) return;

    bool free_choice = (gs->trick_count == 0 && gs->tricks_completed == 0);
    const char *src_name = (gs->two_player_hand_type == 0) ? "votre main" : "votre tableau";
    char hint[128];
    if (free_choice) {
        snprintf(hint, sizeof(hint),
                 "1er pli \u2014 choisissez librement depuis votre main ou le tableau");
    } else {
        bool sub2    = (gs->trick_count >= 2);
        bool is_lead = (gs->to_act == gs->trick_leader);
        const char *sub  = sub2    ? "Sous-pli 2" : "Sous-pli 1";
        const char *role = is_lead ? "ouvrez"      : "suivez";
        snprintf(hint, sizeof(hint), "%s \u2014 %s depuis %s", sub, role, src_name);
    }
    SDL_Rect hr = {0, TABLEAU_LOCAL_Y - 26, WINDOW_W, 20};
    ui_text_centered(r, app->font_sm, hint, hr, (SDL_Color){255, 220, 60, 255});
}

/* Info bar: turn + contract */
static void draw_info_bar(App *app)
{
    ClientGameState *gs = &app->gs;
    if (gs->num_players == 0) return;

    /* Contract */
    if (gs->phase >= PHASE_PLAYING && !gs->contract.pass && gs->contract.level > 0) {
        char cstr[32];
        const char *s = gs->contract.suit;
        if (strcmp(s, "Misere") == 0 || strcmp(s, "OpenMisere") == 0)
            snprintf(cstr, sizeof(cstr), "%s", s);
        else
            snprintf(cstr, sizeof(cstr), "%d%s", gs->contract.level, s);
        ui_text(app->renderer, app->font_sm, cstr, 200, 8, (SDL_Color){220, 180, 60, 255});
    }

    /* Turn */
    bool my_turn = (gs->to_act == gs->local_seat);
    char buf[96];
    if (gs->phase == PHASE_BIDDING || gs->phase == PHASE_PLAYING) {
        snprintf(buf, sizeof(buf), my_turn ? "C'est votre tour"
                                           : "Tour de %s", gs->players[gs->to_act].name);
        SDL_Color col = my_turn ? (SDL_Color){80, 220, 120, 255}
                                : (SDL_Color){160, 170, 200, 255};
        SDL_Rect r = {0, 30, WINDOW_W, 26};
        ui_text_centered(app->renderer, app->font_sm, buf, r, col);
    }
    if (gs->phase == PHASE_KITTY) {
        bool mine = (gs->contractor == gs->local_seat);
        const char *ktxt = mine
            ? "Choisissez 3 cartes \u00e0 d\u00e9fausser"
            : "Le preneur choisit ses d\u00e9fausses\u2026";
        SDL_Color col = mine ? (SDL_Color){220, 180, 60, 255}
                             : (SDL_Color){160, 170, 200, 255};
        SDL_Rect r = {0, 30, WINDOW_W, 26};
        ui_text_centered(app->renderer, app->font_sm, ktxt, r, col);
    }
}

/* Bid panel */
static void draw_bid_panel(App *app)
{
    init_action_buttons(app);
    ClientGameState *gs = &app->gs;
    int cv = contract_value(gs);
    SDL_Renderer *r = app->renderer;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 10, 14, 30, 210);
    SDL_RenderFillRect(r, &s_bid_panel_rect);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    for (int row = 0; row < BID_ROWS; row++) {
        for (int col = 0; col < BID_COLS; col++) {
            bool valid = (bid_value(row + 6, col) > cv);
            SDL_SetRenderDrawColor(r, valid ? 50 : 25, valid ? 50 : 25, valid ? 65 : 35, 255);
            SDL_RenderFillRect(r, &s_bid[row][col].rect);
            SDL_SetRenderDrawColor(r, 100, 100, 130, 255);
            SDL_RenderDrawRect(r, &s_bid[row][col].rect);
            SDL_Color lc;
            if      (col == 2 || col == 3) lc = (SDL_Color){220, 60, 60, 255};
            else if (col == 4)             lc = (SDL_Color){180, 180, 255, 255};
            else                           lc = (SDL_Color){200, 200, 220, 255};
            if (!valid) { lc.r /= 2; lc.g /= 2; lc.b /= 2; }
            ui_text_centered(r, app->font_sm, s_bid[row][col].label,
                             s_bid[row][col].rect, lc);
        }
    }
    bool mis_v  = (misere_value()      > cv);
    bool omis_v = (open_misere_value() > cv);
    SDL_Color mc  = mis_v  ? COL_WHITE : (SDL_Color){80, 80, 80, 255};
    SDL_Color omc = omis_v ? COL_WHITE : (SDL_Color){80, 80, 80, 255};
    SDL_SetRenderDrawColor(r, mis_v  ? 50:25, mis_v  ? 50:25, mis_v  ? 65:35, 255);
    SDL_RenderFillRect(r, &s_bid_misere.rect);
    SDL_SetRenderDrawColor(r, 100, 100, 130, 255);
    SDL_RenderDrawRect(r, &s_bid_misere.rect);
    ui_text_centered(r, app->font_sm, s_bid_misere.label, s_bid_misere.rect, mc);
    SDL_SetRenderDrawColor(r, omis_v ? 50:25, omis_v ? 50:25, omis_v ? 65:35, 255);
    SDL_RenderFillRect(r, &s_bid_open_misere.rect);
    SDL_SetRenderDrawColor(r, 100, 100, 130, 255);
    SDL_RenderDrawRect(r, &s_bid_open_misere.rect);
    ui_text_centered(r, app->font_sm, s_bid_open_misere.label, s_bid_open_misere.rect, omc);
    ui_button_draw(r, app->font_sm, &btn_pass);
}

/* Bid history (left panel) */
static void draw_bid_history(App *app)
{
    ClientGameState *gs = &app->gs;
    if (gs->bid_count == 0) return;
    SDL_Renderer *r = app->renderer;
    int x = 160, y = 80;
    SDL_Color col_hdr = {140, 150, 190, 255};
    ui_text(r, app->font_sm, "Ench\u00e8res :", x, y, col_hdr);
    y += 20;
    int shown = 0;
    for (int i = 0; i < gs->bid_count && shown < 8; i++) {
        BidInfo *b = &gs->bids[i];
        /* Skip unplaced bid slots: server sends zero-value {pass:false,level:0,suit:"S"}
         * for seats that haven't bid yet. Valid bids have level >= 6,
         * except Misere/OpenMisere which have level=0 but suit starts with 'M'. */
        if (!b->pass && b->level == 0 && b->suit[0] != 'M') continue;
        char buf[48];
        if (b->pass)
            snprintf(buf, sizeof(buf), "%s: Passe",
                     gs->players[i % gs->num_players].name);
        else if (strcmp(b->suit, "Misere") == 0 || strcmp(b->suit, "OpenMisere") == 0)
            snprintf(buf, sizeof(buf), "%s: %s",
                     gs->players[i % gs->num_players].name, b->suit);
        else
            snprintf(buf, sizeof(buf), "%s: %d%s",
                     gs->players[i % gs->num_players].name, b->level, b->suit);
        SDL_Color col = b->pass ? (SDL_Color){120, 120, 140, 255}
                                : (SDL_Color){220, 200, 80, 255};
        ui_text(r, app->font_sm, buf, x, y, col);
        y += 18;
        shown++;
    }
}

/* Choose-hand panel (2-player only, PhaseChooseHand) */
/* Discard button */
static void draw_discard_button(App *app)
{
    char label[32];
    snprintf(label, sizeof(label), "D\u00e9fausser (%d/3)", s_discard_count);
    btn_discard.label = label;
    SDL_Renderer *r = app->renderer;
    SDL_Color bg = (s_discard_count == 3)
        ? (SDL_Color){180, 60, 60, 255}
        : (SDL_Color){60, 60, 80, 200};
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(r, &btn_discard.rect);
    SDL_SetRenderDrawColor(r, 180, 180, 200, 255);
    SDL_RenderDrawRect(r, &btn_discard.rect);
    SDL_Color tc = (s_discard_count == 3) ? COL_WHITE : (SDL_Color){140, 140, 160, 255};
    ui_text_centered(r, app->font_sm, label, btn_discard.rect, tc);
}

/* Waiting panel (before game start) */
static void draw_waiting_panel(App *app)
{
    SDL_Renderer *r = app->renderer;
    char buf[128];
    snprintf(buf, sizeof(buf), "Salon : %s", app->net.current_room.name);
    SDL_Rect tr = {0, WINDOW_H/2 - 80, WINDOW_W, 32};
    ui_text_centered(r, app->font_md, buf, tr, COL_WHITE);
    for (int i = 0; i < app->net.current_room.player_count; i++) {
        char pb[64];
        snprintf(pb, sizeof(pb), "Si\u00e8ge %d : %s",
                 i + 1, app->net.current_room.players[i]);
        SDL_Rect pr = {0, WINDOW_H/2 - 40 + i * 22, WINDOW_W, 20};
        bool mine = (strcmp(app->net.current_room.players[i], app->net.player_name) == 0);
        SDL_Color col = mine ? (SDL_Color){80, 220, 120, 255}
                             : (SDL_Color){160, 170, 200, 255};
        ui_text_centered(r, app->font_sm, pb, pr, col);
    }
    int missing = app->net.current_room.max_seats - app->net.current_room.player_count;
    if (missing > 0) {
        ui_button_draw(r, app->font_md, &btn_start_bots);
        char hbuf[64];
        snprintf(hbuf, sizeof(hbuf), "(%d si\u00e8ge%s vide%s)",
                 missing, missing > 1 ? "s" : "", missing > 1 ? "s" : "");
        SDL_Rect hint = {0, WINDOW_H/2 + 62, WINDOW_W, 20};
        ui_text_centered(r, app->font_sm, hbuf, hint, (SDL_Color){120, 130, 160, 255});
    }
}

/* End-of-game overlay */
static void draw_end_overlay(App *app)
{
    ClientGameState *gs = &app->gs;
    SDL_Renderer *r = app->renderer;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 170);
    SDL_Rect full = {0, 0, WINDOW_W, WINDOW_H};
    SDL_RenderFillRect(r, &full);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

    SDL_Rect box = {WINDOW_W/2 - 200, WINDOW_H/2 - 110, 400, 240};
    SDL_SetRenderDrawColor(r, 20, 26, 50, 255);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 120, 130, 180, 255);
    SDL_RenderDrawRect(r, &box);

    SDL_Rect title_r = {box.x, box.y + 12, box.w, 36};
    ui_text_centered(r, app->font_lg, "Fin de partie", title_r, COL_WHITE);

    char sa[64], sb[64];
    snprintf(sa, sizeof(sa), "\u00c9quipe A : %d pts", gs->scores[0]);
    snprintf(sb, sizeof(sb), "\u00c9quipe B : %d pts", gs->scores[1]);
    SDL_Color winner_col = {220, 200, 60, 255};
    SDL_Color loser_col  = {140, 150, 180, 255};
    SDL_Color col_a = (gs->scores[0] >= gs->scores[1]) ? winner_col : loser_col;
    SDL_Color col_b = (gs->scores[1] >  gs->scores[0]) ? winner_col : loser_col;
    SDL_Rect ra = {box.x, box.y + 60, box.w, 28};
    SDL_Rect rb = {box.x, box.y + 92, box.w, 28};
    ui_text_centered(r, app->font_md, sa, ra, col_a);
    ui_text_centered(r, app->font_md, sb, rb, col_b);

    btn_nouvelle.rect.x = box.x + (box.w - 200) / 2;
    btn_nouvelle.rect.y = box.y + box.h - 56;
    btn_nouvelle.rect.w = 200;
    btn_nouvelle.rect.h = 40;
    ui_button_draw(r, app->font_sm, &btn_nouvelle);
}

/* ── Hit-test helpers ────────────────────────────────────────────────────────── */

static int hand_card_hit(ClientGameState *gs, int mx, int my)
{
    PlayerSlot *p = &gs->players[gs->local_seat];
    int n = p->hand_count;
    if (n == 0) return -1;
    int total_w = CARD_W + (n - 1) * HAND_OVERLAP;
    int sx = WINDOW_W / 2 - total_w / 2;
    for (int i = n - 1; i >= 0; i--) {
        int x = sx + i * HAND_OVERLAP;
        int hit_w = (i < n - 1) ? HAND_OVERLAP : CARD_W;
        if (mx >= x && mx < x + hit_w && my >= HAND_Y && my < HAND_Y + CARD_H)
            return i;
    }
    return -1;
}

static int kitty_card_hit(ClientGameState *gs, int mx, int my)
{
    PlayerSlot *p = &gs->players[gs->local_seat];
    int hand_n = p->hand_count;
    int total  = hand_n + gs->kitty_count;
    if (total == 0) return -1;
    int overlap = (total > 10) ? 48 : HAND_OVERLAP;
    int total_w = CARD_W + (total - 1) * overlap;
    int sx = WINDOW_W / 2 - total_w / 2;
    /* Allow clicks on lifted (selected) cards — they render CARD_SELECTED_LIFT px above HAND_Y */
    int hit_y_min = HAND_Y - CARD_SELECTED_LIFT;
    for (int i = total - 1; i >= 0; i--) {
        int x = sx + i * overlap;
        int hit_w = (i < total - 1) ? overlap : CARD_W;
        if (mx >= x && mx < x + hit_w && my >= hit_y_min && my < HAND_Y + CARD_H)
            return i;
    }
    return -1;
}

/* ── Event ───────────────────────────────────────────────────────────────────── */

void screen_table_handle_event(App *app, SDL_Event *e)
{
    ClientGameState *gs = &app->gs;
    init_action_buttons(app);

    if (e->type == SDL_MOUSEMOTION) {
        int mx = e->motion.x, my = e->motion.y;
        ui_button_update(&btn_lobby,      mx, my);
        ui_button_update(&btn_start_bots, mx, my);
        ui_button_update(&btn_play,       mx, my);
        ui_button_update(&btn_nouvelle,   mx, my);
        if (gs->phase == PHASE_BIDDING && gs->to_act == gs->local_seat) {
            for (int row = 0; row < BID_ROWS; row++)
                for (int col = 0; col < BID_COLS; col++)
                    ui_button_update(&s_bid[row][col], mx, my);
            ui_button_update(&s_bid_misere,      mx, my);
            ui_button_update(&s_bid_open_misere, mx, my);
            ui_button_update(&btn_pass,          mx, my);
        }
    }

    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
        int mx = e->button.x, my = e->button.y;

        /* ← Lobby */
        if (ui_button_hit(&btn_lobby, mx, my)) {
            if (app->net.in_room) {
                net_send_room_leave();
                app->net.in_room = false;
            }
            memset(gs, 0, sizeof(*gs));
            gs->selected_card = -1;
            discard_reset();
            app->screen = SCREEN_LOBBY;
            return;
        }

        /* Waiting: Démarrer avec bots */
        if (gs->num_players == 0 && ui_button_hit(&btn_start_bots, mx, my)) {
            net_send_room_start();
            return;
        }

        /* End overlay: Nouvelle partie → lobby */
        if (gs->phase == PHASE_END && ui_button_hit(&btn_nouvelle, mx, my)) {
            if (app->net.in_room) {
                net_send_room_leave();
                app->net.in_room = false;
            }
            memset(gs, 0, sizeof(*gs));
            gs->selected_card = -1;
            discard_reset();
            app->screen = SCREEN_LOBBY;
            return;
        }

        /* Bidding: my turn */
        if (gs->phase == PHASE_BIDDING && gs->to_act == gs->local_seat) {
            int cv = contract_value(gs);
            const char *suits[5] = {"S", "C", "D", "H", "NT"};
            for (int row = 0; row < BID_ROWS; row++) {
                for (int col = 0; col < BID_COLS; col++) {
                    if (!ui_button_hit(&s_bid[row][col], mx, my)) continue;
                    if (bid_value(row + 6, col) <= cv) return;
                    net_send_bid(false, row + 6, suits[col]);
                    return;
                }
            }
            if (ui_button_hit(&s_bid_misere, mx, my) && misere_value() > cv) {
                net_send_bid(false, 0, "Misere");
                return;
            }
            if (ui_button_hit(&s_bid_open_misere, mx, my) && open_misere_value() > cv) {
                net_send_bid(false, 0, "OpenMisere");
                return;
            }
            if (ui_button_hit(&btn_pass, mx, my)) {
                net_send_bid(true, 0, "");
                return;
            }
        }

        /* Kitty: contractor selects 3 to discard */
        if (gs->phase == PHASE_KITTY && gs->contractor == gs->local_seat) {
            int ci = kitty_card_hit(gs, mx, my);
            if (ci >= 0) { discard_toggle(ci); return; }
            if (s_discard_count == 3 && ui_button_hit(&btn_discard, mx, my)) {
                PlayerSlot *p = &gs->players[gs->local_seat];
                Card chosen[3];
                int ci2 = 0;
                for (int i = 0; i < p->hand_count + gs->kitty_count && ci2 < 3; i++) {
                    if (!discard_has(i)) continue;
                    chosen[ci2++] = (i < p->hand_count)
                        ? p->hand[i]
                        : gs->kitty[i - p->hand_count];
                }
                if (ci2 == 3)
                    net_send_discard(chosen);
                discard_reset();
                return;
            }
        }

        /* Playing: select + play */
        if (gs->phase == PHASE_PLAYING) {
            PlayerSlot *p = &gs->players[gs->local_seat];
            /* Free choice: first trick, both sources available; otherwise forced. */
            bool free_choice = (gs->trick_count == 0 && gs->tricks_completed == 0);
            if (gs->to_act == gs->local_seat && ui_button_hit(&btn_play, mx, my)) {
                if (gs->selected_card >= 100) {
                    /* Tableau card selected */
                    int col = gs->selected_card - 100;
                    fprintf(stderr, "[CLIENT-BTN-PLAY] tableau col=%d card=%s%s\n",
                            col,
                            card_rank_str(p->tableau[5 + col].rank),
                            card_suit_str(p->tableau[5 + col].suit));
                    net_send_play(p->tableau[5 + col]);
                    gs->selected_card = -1;
                    return;
                } else if (gs->selected_card >= 0
                           && is_playable(gs, gs->selected_card)) {
                    fprintf(stderr, "[CLIENT-BTN-PLAY] hand[%d]=%s%s step=%d handType=%d\n",
                            gs->selected_card,
                            card_rank_str(p->hand[gs->selected_card].rank),
                            card_suit_str(p->hand[gs->selected_card].suit),
                            gs->trick_count, gs->two_player_hand_type);
                    net_send_play(p->hand[gs->selected_card]);
                    gs->selected_card = -1;
                    return;
                }
            }
            /* Tableau click: available when tableau is the active/forced source OR
             * on the first trick where the leader has free source choice. */
            if (gs->num_players == 2
                && (gs->two_player_hand_type == 1 || free_choice)) {
                int tcol = local_tableau_hit(gs, mx, my);
                if (tcol >= 0) {
                    gs->selected_card = (gs->selected_card == 100 + tcol) ? -1 : 100 + tcol;
                    return;
                }
            }
            /* Hand card click: available when hand is the active/forced source OR
             * on the first trick (free choice), OR in 4-player (always).
             * Exception: the Joker is always selectable from hand regardless of
             * the forced source, because it is always legally playable. */
            bool hand_free = (gs->num_players != 2
                              || gs->two_player_hand_type == 0
                              || free_choice);
            {
                int ci = hand_card_hit(gs, mx, my);
                if (ci >= 0
                    && (hand_free
                        || (int)p->hand[ci].rank == RANK_JOKER)) {
                    gs->selected_card = (gs->selected_card == ci) ? -1 : ci;
                    return;
                }
            }
        }
    }
}

/* ── Render ──────────────────────────────────────────────────────────────────── */

void screen_table_render(App *app)
{
    SDL_Renderer *r = app->renderer;
    ClientGameState *gs = &app->gs;
    init_action_buttons(app);

    SDL_SetRenderDrawColor(r, COL_TABLE.r, COL_TABLE.g, COL_TABLE.b, 255);
    SDL_RenderClear(r);

    draw_score(app);
    ui_button_draw(r, app->font_sm, &btn_lobby);

    /* ── Game not started yet ── */
    if (gs->num_players == 0) {
        draw_waiting_panel(app);
        return;
    }

    /* ── Active game ── */
    draw_info_bar(app);

    if (gs->num_players >= 2) {
        draw_card_backs_h(r, WINDOW_W/2, 60, gs->players[1].hand_count);
        ui_text(r, app->font_sm, gs->players[1].name,
                WINDOW_W/2 - 30, 60 + CARD_H + 4, COL_WHITE);
        /* 2-player: show opponent tableau below their hand */
        if (gs->num_players == 2)
            draw_tableau(app, 1, TABLEAU_OPP_Y, false);
    }
    if (gs->num_players >= 3) {
        draw_card_backs_v(r, WINDOW_W - 60, WINDOW_H/2, gs->players[2].hand_count);
        ui_text(r, app->font_sm, gs->players[2].name,
                WINDOW_W - 100, WINDOW_H/2 + CARD_H/2 + 8, COL_WHITE);
    }
    if (gs->num_players >= 4) {
        draw_card_backs_v(r, 60, WINDOW_H/2, gs->players[3].hand_count);
        ui_text(r, app->font_sm, gs->players[3].name,
                10, WINDOW_H/2 + CARD_H/2 + 8, COL_WHITE);
    }

    if (gs->phase == PHASE_PLAYING || gs->phase == PHASE_END)
        draw_trick(app);

    /* Local player name */
    ui_text(r, app->font_sm, gs->players[gs->local_seat].name,
            WINDOW_W/2 - 20, HAND_Y - 22, COL_WHITE);

    /* Phase-specific bottom area */
    switch (gs->phase) {
    case PHASE_BIDDING:
        if (gs->num_players == 2)
            draw_tableau(app, gs->local_seat, TABLEAU_LOCAL_Y, true);
        draw_local_hand(app);
        draw_bid_history(app);
        if (gs->to_act == gs->local_seat) draw_bid_panel(app);
        break;
    case PHASE_KITTY:
        if (gs->contractor == gs->local_seat) {
            draw_kitty_hand(app);
            draw_discard_button(app);
        } else {
            draw_local_hand(app);
        }
        break;
    case PHASE_PLAYING:
        if (gs->num_players == 2)
            draw_source_highlight_2p(app);
        draw_local_hand(app);
        if (gs->num_players == 2) {
            draw_tableau(app, gs->local_seat, TABLEAU_LOCAL_Y, true);
            draw_2p_play_hint(app);
        }
        {
            bool tableau_sel = (gs->selected_card >= 100);
            bool hand_sel    = (gs->selected_card >= 0 && gs->selected_card < gs->players[gs->local_seat].hand_count);
            bool show_play   = gs->to_act == gs->local_seat
                && ((tableau_sel) || (hand_sel && is_playable(gs, gs->selected_card)));
            if (show_play)
                ui_button_draw(r, app->font_sm, &btn_play);
        }
        break;
    case PHASE_SCORING:
    case PHASE_END:
        draw_local_hand(app);
        draw_end_overlay(app);
        break;
    default:
        draw_local_hand(app);
        break;
    }

    /* ── Error toast ── */
    if (gs->error_msg[0] != '\0') {
        if (SDL_GetTicks() < (Uint32)gs->error_until) {
            SDL_Rect toast = {WINDOW_W/2 - 200, HAND_Y - 80, 400, 36};
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 180, 30, 30, 210);
            SDL_RenderFillRect(r, &toast);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(r, 255, 120, 120, 255);
            SDL_RenderDrawRect(r, &toast);
            ui_text_centered(r, app->font_sm, gs->error_msg, toast,
                             (SDL_Color){255, 220, 220, 255});
        } else {
            gs->error_msg[0] = '\0';
        }
    }
}
