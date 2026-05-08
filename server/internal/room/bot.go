package room

import (
	"fmt"
	"math/rand"
	"time"

	"github.com/remi/5_hundred/internal/game"
	"github.com/remi/5_hundred/internal/protocol"
	"github.com/remi/5_hundred/internal/ws"
)

// ── Bot client creation ───────────────────────────────────────────────────────

// AddBots fills all empty seats of r with bot pseudo-clients and returns their IDs.
func AddBots(r *Room) []string {
	r.mu.Lock()
	defer r.mu.Unlock()

	var ids []string
	botNum := 1
	for i := 0; i < r.maxSeat; i++ {
		if r.seats[i] == nil {
			id := fmt.Sprintf("bot-%s-%d", r.ID, botNum)
			name := fmt.Sprintf("Bot %d", botNum)
			bot := ws.NewBotClient(id, name)
			r.seats[i] = bot
			ids = append(ids, id)
			botNum++
		}
	}
	return ids
}

// ── Bot runner ────────────────────────────────────────────────────────────────

// RunBot drives a single bot seat in a background goroutine.
// It wakes every ~600 ms, checks if it's the bot's turn, and makes a random legal move.
func RunBot(r *Room, seat int, rng *rand.Rand, broadcast func()) {
	go func() {
		for {
			time.Sleep(600 * time.Millisecond)
			if done := botStep(r, seat, rng, broadcast); done {
				return
			}
		}
	}()
}

type stepResult int

const (
	stepWait  stepResult = iota // not this bot's turn yet
	stepActed                   // made a move
	stepDone                    // game over, bot should stop
)

func botStep(r *Room, seat int, rng *rand.Rand, broadcast func()) bool {
	r.mu.Lock()
	g := r.game
	if g == nil {
		r.mu.Unlock()
		return false
	}
	if g.Phase == game.PhaseEnd {
		r.mu.Unlock()
		return true
	}
	if g.ToAct != seat {
		r.mu.Unlock()
		return false
	}

	// Make a move while holding the lock.
	var err error
	switch g.Phase {
	case game.PhaseBidding:
		err = botBid(g, seat, rng)
	case game.PhaseKitty:
		if seat == g.Contractor {
			err = botKitty(g, seat, rng)
		}
	case game.PhasePlaying:
		err = botPlay(g, seat, rng)
	}
	done := g.Phase == game.PhaseEnd
	r.mu.Unlock()

	if err == nil {
		broadcast()
	}
	return done
}

// botBid makes a random bid: mostly pass, occasionally real bid.
func botBid(g *game.Game, seat int, rng *rand.Rand) error {
	current := g.Bids
	_ = current

	valid := game.ValidBids(g.HighestBid())
	// 60% chance to pass, unless no valid bids exist anyway.
	if len(valid) == 0 || rng.Intn(10) < 6 {
		return g.PlaceBid(seat, game.Bid{Pass: true})
	}
	// Pick a low bid from the valid list.
	pick := valid[rng.Intn(min(3, len(valid)))]
	return g.PlaceBid(seat, pick)
}

// botKitty picks up the kitty and discards random cards.
func botKitty(g *game.Game, seat int, rng *rand.Rand) error {
	if err := g.PickUpKitty(seat); err != nil {
		return err
	}
	// Shuffle hand and discard first 3.
	hand := make([]game.Card, len(g.Players[seat].Hand))
	copy(hand, g.Players[seat].Hand)
	rng.Shuffle(len(hand), func(i, j int) { hand[i], hand[j] = hand[j], hand[i] })
	return g.Discard(seat, hand[:3])
}

// botPlay plays a random available card.
func botPlay(g *game.Game, seat int, rng *rand.Rand) error {
	avail := g.AvailableCards(seat)
	if len(avail) == 0 {
		return nil
	}
	return g.PlayCard(seat, avail[rng.Intn(len(avail))])
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// ── StartWithBots ─────────────────────────────────────────────────────────────

// StartWithBots fills empty seats with bots, starts the game, and launches bot goroutines.
// broadcast is called after each bot move to push game.state to all real players.
func StartWithBots(r *Room, broadcast func()) error {
	botIDs := AddBots(r)

	r.mu.Lock()
	if r.game != nil {
		r.mu.Unlock()
		return fmt.Errorf("game already started")
	}
	for i := 0; i < r.maxSeat; i++ {
		if r.seats[i] == nil {
			r.mu.Unlock()
			return fmt.Errorf("seat %d still empty after AddBots", i)
		}
	}
	g := game.New(r.Variant, r.rng)
	r.game = g
	rng := r.rng
	_ = botIDs
	r.mu.Unlock()

	// Launch bot goroutines for every bot seat.
	r.mu.Lock()
	for i := 0; i < r.maxSeat; i++ {
		if r.seats[i] != nil && r.seats[i].IsBot() {
			seat := i
			localRng := rand.New(rand.NewSource(rng.Int63()))
			go RunBot(r, seat, localRng, broadcast)
		}
	}
	r.mu.Unlock()

	broadcast()

	// Notify all real players (game.event "game_start").
	r.Broadcast(protocol.TypeGameEvent, protocol.GameEventPayload{
		Event:   "game_start",
		Message: "La partie commence !",
	})
	BroadcastGameState(g, r)
	return nil
}
