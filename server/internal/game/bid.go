package game

import "fmt"

// BidLevel is the number of tricks the bidder contracts to win (6–10).
type BidLevel int

// BidSuit is the trump suit declared in the bid (or NT/Misère).
type BidSuit int

const (
	BidSpades   BidSuit = iota // lowest rank
	BidClubs
	BidDiamonds
	BidHearts
	BidNoTrumps
	BidMisere     // Misère (no partner, no trump, every trick to opponent)
	BidOpenMisere // Open Misère (hand exposed)
)

func (bs BidSuit) String() string {
	switch bs {
	case BidSpades:
		return "♠"
	case BidClubs:
		return "♣"
	case BidDiamonds:
		return "♦"
	case BidHearts:
		return "♥"
	case BidNoTrumps:
		return "NT"
	case BidMisere:
		return "Misère"
	case BidOpenMisere:
		return "Open Misère"
	}
	return "?"
}

// ToSuit converts a BidSuit to a Suit for play purposes.
// Returns NoSuit for NT and Misère bids.
func (bs BidSuit) ToSuit() Suit {
	switch bs {
	case BidSpades:
		return Spades
	case BidClubs:
		return Clubs
	case BidDiamonds:
		return Diamonds
	case BidHearts:
		return Hearts
	}
	return NoSuit
}

// Bid represents a single bid action.
type Bid struct {
	// Pass == true means the player passed; all other fields are ignored.
	Pass bool
	// Level is 6–10. Ignored for Misère/OpenMisère (treated as special levels).
	Level  BidLevel
	Suit   BidSuit
}

func (b Bid) String() string {
	if b.Pass {
		return "Pass"
	}
	if b.Suit == BidMisere {
		return "Misère"
	}
	if b.Suit == BidOpenMisere {
		return "Open Misère"
	}
	return fmt.Sprintf("%d%s", b.Level, b.Suit)
}

// numericValue returns a monotonically increasing integer so bids can be
// compared for "higher than" semantics.
// Open Misère > Misère. The table follows the official 500 bidding order.
func (b Bid) numericValue() int {
	if b.Pass {
		return -1
	}
	switch b.Suit {
	case BidMisere:
		return 250 // between 8NT (240) and 9♠ (260) per standard ordering
	case BidOpenMisere:
		return 500 // between 10NT (490) and would-be 11NT
	}
	// Normal bids: base = (level - 6) * 5 + suit_rank, scaled by 10.
	// Order: 6♠(0) 6♣(1) 6♦(2) 6♥(3) 6NT(4) 7♠(5) … 10NT(24)
	pos := (int(b.Level)-6)*5 + int(b.Suit)
	return pos * 10
}

// HigherThan returns true if b is strictly higher than other.
func (b Bid) HigherThan(other Bid) bool {
	return b.numericValue() > other.numericValue()
}

// ValidBids returns the list of bids that are strictly higher than current,
// excluding Pass (which is always available separately).
func ValidBids(current Bid) []Bid {
	var bids []Bid
	for level := BidLevel(6); level <= 10; level++ {
		for suit := BidSpades; suit <= BidNoTrumps; suit++ {
			b := Bid{Level: level, Suit: suit}
			if b.HigherThan(current) {
				bids = append(bids, b)
			}
		}
	}
	if (Bid{Suit: BidMisere}).HigherThan(current) {
		bids = append(bids, Bid{Suit: BidMisere})
	}
	if (Bid{Suit: BidOpenMisere}).HigherThan(current) {
		bids = append(bids, Bid{Suit: BidOpenMisere})
	}
	return bids
}
