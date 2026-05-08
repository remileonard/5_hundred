package game

import "fmt"

// Suit represents a card suit.
type Suit int

const (
	Spades   Suit = iota // ♠ lowest
	Clubs                // ♣
	Diamonds             // ♦
	Hearts               // ♥
	NoSuit   Suit = -1   // used for Joker / NoTrumps
)

func (s Suit) String() string {
	switch s {
	case Spades:
		return "♠"
	case Clubs:
		return "♣"
	case Diamonds:
		return "♦"
	case Hearts:
		return "♥"
	default:
		return "?"
	}
}

// Rank represents a card rank (4-14, where 11=J, 12=Q, 13=K, 14=A).
// 0 is reserved for the Joker.
type Rank int

const (
	Joker Rank = 0
	Four  Rank = 4
	Five  Rank = 5
	Six   Rank = 6
	Seven Rank = 7
	Eight Rank = 8
	Nine  Rank = 9
	Ten   Rank = 10
	Jack  Rank = 11
	Queen Rank = 12
	King  Rank = 13
	Ace   Rank = 14
)

func (r Rank) String() string {
	switch r {
	case Joker:
		return "Jo"
	case Jack:
		return "J"
	case Queen:
		return "Q"
	case King:
		return "K"
	case Ace:
		return "A"
	default:
		return fmt.Sprintf("%d", int(r))
	}
}

// Card is an immutable playing card.
type Card struct {
	Rank Rank
	Suit Suit // NoSuit for Joker
}

var TheJoker = Card{Rank: Joker, Suit: NoSuit}

func (c Card) IsJoker() bool { return c.Rank == Joker }

func (c Card) String() string {
	if c.IsJoker() {
		return "Joker"
	}
	return c.Rank.String() + c.Suit.String()
}

// LeftBower returns true if this card is the left bower (same-colour Jack acting
// as trump) given the declared trump suit.
func (c Card) IsLeftBower(trump Suit) bool {
	if c.Rank != Jack {
		return false
	}
	return sisterSuit(c.Suit) == trump
}

// RightBower returns true if this card is the right bower (Jack of trump suit).
func (c Card) IsRightBower(trump Suit) bool {
	return c.Rank == Jack && c.Suit == trump
}

// sisterSuit returns the same-colour opposite suit.
func sisterSuit(s Suit) Suit {
	switch s {
	case Spades:
		return Clubs
	case Clubs:
		return Spades
	case Diamonds:
		return Hearts
	case Hearts:
		return Diamonds
	}
	return NoSuit
}

// EffectiveSuit returns the suit a card counts as for following-suit purposes,
// taking trump and the left-bower rule into account.
// For no-trumps or misère, trump == NoSuit and bowers don't apply.
func (c Card) EffectiveSuit(trump Suit) Suit {
	if c.IsJoker() {
		return trump // Joker always belongs to trumps
	}
	if trump != NoSuit && c.IsLeftBower(trump) {
		return trump
	}
	return c.Suit
}

// TrumpRank returns a numeric rank for comparing cards when trump == suit.
// Higher is better. Non-trump cards return 0 (caller should check EffectiveSuit first).
func (c Card) TrumpRank(trump Suit) int {
	if c.IsJoker() {
		return 100 // best card always
	}
	if c.IsRightBower(trump) {
		return 99
	}
	if trump != NoSuit && c.IsLeftBower(trump) {
		return 98
	}
	return int(c.Rank)
}

// Beats returns true if c beats other given the led suit and trump.
// Both cards are assumed to already be in the trick (caller supplies led suit).
func (c Card) Beats(other Card, ledSuit, trump Suit) bool {
	cTrump := c.EffectiveSuit(trump) == trump && trump != NoSuit
	oTrump := other.EffectiveSuit(trump) == trump && trump != NoSuit

	// Joker special: always beats in no-trumps/misère too
	if c.IsJoker() {
		return true
	}
	if other.IsJoker() {
		return false
	}

	switch {
	case cTrump && !oTrump:
		return true
	case !cTrump && oTrump:
		return false
	case cTrump && oTrump:
		return c.TrumpRank(trump) > other.TrumpRank(trump)
	default:
		// Neither is trump. c only wins if it followed the led suit.
		cLed := c.EffectiveSuit(trump) == ledSuit
		oLed := other.EffectiveSuit(trump) == ledSuit
		switch {
		case cLed && !oLed:
			return true
		case !cLed && oLed:
			return false
		default:
			return c.Rank > other.Rank
		}
	}
}
