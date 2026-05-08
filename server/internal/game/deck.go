package game

import "math/rand"

// Variant selects the game mode.
type Variant int

const (
	FourPlayer Variant = iota // classic 4-player game
	TwoPlayer                 // head-to-head 2-player game
)

// buildDeck builds the 43-card 500 deck.
// Both variants use the same 43-card pack:
//   ♠ ♣: 4 – A  (11 cards each, starting at 4) → but only A-K-Q-J-10-9-8-7-6-5-4 = 11 cards
//   ♦ ♥: 5 – A  (10 cards each, starting at 5) → A-K-Q-J-10-9-8-7-6-5 = 10 cards
//   + Joker  → total = 11+11+10+10+1 = 43 ✓
func buildDeck() []Card {
	deck := make([]Card, 0, 43)

	for _, suit := range []Suit{Spades, Clubs} {
		for r := Four; r <= Ace; r++ {
			deck = append(deck, Card{Rank: r, Suit: suit})
		}
	}
	for _, suit := range []Suit{Diamonds, Hearts} {
		for r := Five; r <= Ace; r++ {
			deck = append(deck, Card{Rank: r, Suit: suit})
		}
	}
	deck = append(deck, TheJoker)
	return deck
}

// Shuffle returns a shuffled copy of the deck using the provided *rand.Rand.
// Pass a seeded rand for deterministic behaviour.
func Shuffle(deck []Card, rng *rand.Rand) []Card {
	out := make([]Card, len(deck))
	copy(out, deck)
	rng.Shuffle(len(out), func(i, j int) { out[i], out[j] = out[j], out[i] })
	return out
}

// DealResult holds the outcome of a deal.
type DealResult struct {
	// Hands[i] is the 10-card main hand for player i.
	Hands [][]Card
	// Tableau[i] is the 10-card tableau for player i (2-player variant only).
	// Each sub-slice has [0:5] = face-down (bottom), [5:10] = face-up (top).
	Tableau [][]Card
	// Kitty holds the 3 cards set aside.
	Kitty []Card
}

// Deal distributes a shuffled deck according to the variant rules.
func Deal(deck []Card, v Variant) DealResult {
	if len(deck) != 43 {
		panic("deck must have exactly 43 cards")
	}

	pos := 0
	take := func(n int) []Card {
		cards := deck[pos : pos+n]
		pos += n
		return cards
	}

	switch v {
	case FourPlayer:
		res := DealResult{
			Hands:   make([][]Card, 4),
			Tableau: nil,
		}
		for i := 0; i < 4; i++ {
			h := make([]Card, 10)
			copy(h, take(10))
			res.Hands[i] = h
		}
		k := make([]Card, 3)
		copy(k, take(3))
		res.Kitty = k
		return res

	case TwoPlayer:
		// Each player gets main hand (10) + tableau (10). Kitty last (3).
		// Deal order: P0-hand, P0-tableau, P1-hand, P1-tableau, kitty.
		res := DealResult{
			Hands:   make([][]Card, 2),
			Tableau: make([][]Card, 2),
		}
		for i := 0; i < 2; i++ {
			h := make([]Card, 10)
			copy(h, take(10))
			res.Hands[i] = h

			t := make([]Card, 10)
			copy(t, take(10))
			// [0:5] = face-down cards (bottom), [5:10] = face-up cards (top).
			res.Tableau[i] = t
		}
		k := make([]Card, 3)
		copy(k, take(3))
		res.Kitty = k
		return res

	default:
		panic("unknown variant")
	}
}
