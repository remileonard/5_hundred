package game

import (
	"errors"
	"math/rand"
)

// Phase represents the current stage of the game.
type Phase int

const (
	PhaseBidding    Phase = iota
	PhaseKitty            // winner picks up kitty and discards
	PhaseChooseHand       // 2-player only: contractor chooses which hand type to play first
	PhasePlaying
	PhaseScoring
	PhaseEnd
)

// Team identifies a team in the 4-player variant.
// In 2-player, each player is their own "team": Team0 = player 0, Team1 = player 1.
type Team int

const (
	Team0 Team = 0
	Team1 Team = 1
)

// PlayerState tracks per-player mutable state.
type PlayerState struct {
	Hand    []Card
	Tableau []Card // 2-player variant only; [0:5]=face-down, [5:10]=face-up
	Tricks  int    // tricks won this hand
}

// Trick holds the cards played in a single trick.
type Trick struct {
	Leader int    // index of the player who led
	Cards  []Card // Cards[i] played by (Leader+i) % numPlayers
}

// winner returns the index offset (into Cards) of the winning card.
func (t Trick) winner(trump Suit) int {
	ledSuit := t.Cards[0].EffectiveSuit(trump)
	best := 0
	for i := 1; i < len(t.Cards); i++ {
		if t.Cards[i].Beats(t.Cards[best], ledSuit, trump) {
			best = i
		}
	}
	return best
}

// Game holds the complete state of one hand.
type Game struct {
	Variant    Variant
	Phase      Phase
	Players    []PlayerState
	Kitty      []Card
	Bids       []Bid    // Bids[i] = bid made by player i (zero-value means not yet bid)
	BidsMade   int      // total number of bids (including passes) so far
	Contract   Bid      // winning bid
	Contractor int      // player index of the winning bidder
	Trump      Suit     // NoSuit for NT/Misère
	Tricks     []Trick  // completed tricks
	Current    Trick    // trick in progress
	ToAct      int      // index of the player who must act next
	Scores     [2]int   // cumulative team scores (Team0, Team1)

	// 2-player variant: which hand type is active in the current sub-game.
	// 0 = private hand, 1 = tableau (open hand).
	// The contractor sets this via ChooseHand. After 10 tricks it flips automatically.
	TwoPlayerHandType int
}

// New creates and deals a new Game. rng is used for shuffling.
func New(v Variant, rng *rand.Rand) *Game {
	deck := Shuffle(buildDeck(), rng)
	deal := Deal(deck, v)

	numPlayers := 4
	if v == TwoPlayer {
		numPlayers = 2
	}

	players := make([]PlayerState, numPlayers)
	for i := range players {
		players[i].Hand = deal.Hands[i]
		if v == TwoPlayer {
			players[i].Tableau = deal.Tableau[i]
		}
	}

	return &Game{
		Variant: v,
		Phase:   PhaseBidding,
		Players: players,
		Kitty:   deal.Kitty,
		Bids:    make([]Bid, numPlayers),
		ToAct:   0,
	}
}

// team returns the team for a player index.
// 4-player: 0&2 → Team0, 1&3 → Team1.
// 2-player: player index IS team index.
func (g *Game) team(playerIdx int) Team {
	if g.Variant == TwoPlayer {
		return Team(playerIdx)
	}
	return Team(playerIdx % 2)
}

// numPlayers returns the number of players.
func (g *Game) numPlayers() int { return len(g.Players) }

// ── Bidding ──────────────────────────────────────────────────────────────────

// PlaceBid records a bid from the current player.
func (g *Game) PlaceBid(playerIdx int, b Bid) error {
	if g.Phase != PhaseBidding {
		return errors.New("not in bidding phase")
	}
	if playerIdx != g.ToAct {
		return errors.New("not your turn to bid")
	}

	if !b.Pass {
		// Find the current highest non-pass bid.
		current := g.HighestBid()
		if !b.HigherThan(current) {
			return errors.New("bid must be higher than the current highest bid")
		}
	}

	g.Bids[playerIdx] = b
	g.BidsMade++
	g.ToAct = (playerIdx + 1) % g.numPlayers()

	g.checkBiddingEnd()
	return nil
}

// HighestBid returns the current highest non-pass bid, or a zero Bid if none.
func (g *Game) HighestBid() Bid {
	best := Bid{Pass: true}
	for _, b := range g.Bids {
		if !b.Pass && b.HigherThan(best) {
			best = b
		}
	}
	return best
}

// checkBiddingEnd determines if bidding is over.
func (g *Game) checkBiddingEnd() {
	n := g.numPlayers()
	if g.BidsMade < n {
		return // not everyone has bid yet
	}

	// Count consecutive passes from the last non-pass bidder.
	// Bidding ends when all players but the contractor have passed.
	passes := 0
	contractor := -1
	for i := 0; i < n; i++ {
		if g.Bids[i].Pass {
			passes++
		} else {
			contractor = i
		}
	}

	if passes == n {
		// Everyone passed — redeal (not implemented here; caller handles).
		g.Phase = PhaseEnd
		return
	}

	if passes == n-1 && contractor >= 0 {
		// One bidder remains — they win the contract.
		g.Contract = g.Bids[contractor]
		g.Contractor = contractor
		g.Trump = g.Contract.Suit.ToSuit()
		g.Phase = PhaseKitty
		g.ToAct = contractor
		return
	}

	// More bidding needed if not all-but-one have passed.
	// Already set g.ToAct in PlaceBid.
}

// ── Kitty ────────────────────────────────────────────────────────────────────

// PickUpKitty adds the kitty cards to the contractor's hand.
func (g *Game) PickUpKitty(playerIdx int) error {
	if g.Phase != PhaseKitty {
		return errors.New("not in kitty phase")
	}
	if playerIdx != g.Contractor {
		return errors.New("only the contractor can pick up the kitty")
	}
	g.Players[playerIdx].Hand = append(g.Players[playerIdx].Hand, g.Kitty...)
	g.Kitty = nil
	return nil
}

// Discard removes cards from the contractor's hand (must discard exactly 3).
func (g *Game) Discard(playerIdx int, cards []Card) error {
	if g.Phase != PhaseKitty {
		return errors.New("not in kitty phase")
	}
	if playerIdx != g.Contractor {
		return errors.New("only the contractor can discard")
	}
	if len(cards) != 3 {
		return errors.New("must discard exactly 3 cards")
	}

	hand := g.Players[playerIdx].Hand
	for _, c := range cards {
		idx := findCard(hand, c)
		if idx < 0 {
			return errors.New("card not in hand: " + c.String())
		}
		hand = removeAt(hand, idx)
	}
	g.Players[playerIdx].Hand = hand

	if g.Variant == TwoPlayer {
		// 2-player: contractor must choose which hand type to play first.
		g.Phase = PhaseChooseHand
		g.ToAct = g.Contractor
	} else {
		g.Phase = PhasePlaying
		g.ToAct = g.Contractor
		g.Current = Trick{Leader: g.Contractor}
	}
	return nil
}

// ChooseHand is called by the contractor in the 2-player variant during
// PhaseChooseHand to select which hand type to play first.
// handType: 0 = private hand, 1 = tableau (open hand).
// After 10 tricks the game will automatically switch to the other type.
func (g *Game) ChooseHand(playerIdx int, handType int) error {
	if g.Phase != PhaseChooseHand {
		return errors.New("not in choose-hand phase")
	}
	if playerIdx != g.Contractor {
		return errors.New("only the contractor can choose the hand type")
	}
	if handType != 0 && handType != 1 {
		return errors.New("invalid hand type: must be 0 (hand) or 1 (tableau)")
	}
	g.TwoPlayerHandType = handType
	g.Phase = PhasePlaying
	g.ToAct = g.Contractor
	g.Current = Trick{Leader: g.Contractor}
	return nil
}

// ── Playing ──────────────────────────────────────────────────────────────────

// PlayCard plays a card from playerIdx's hand (or tableau in 2-player).
func (g *Game) PlayCard(playerIdx int, c Card) error {
	if g.Phase != PhasePlaying {
		return errors.New("not in playing phase")
	}
	if playerIdx != g.ToAct {
		return errors.New("not your turn to play")
	}

	// Validate the card is legally playable.
	if err := g.validatePlay(playerIdx, c); err != nil {
		return err
	}

	// Remove from hand or tableau.
	if err := g.removeFromPlayerCards(playerIdx, c); err != nil {
		return err
	}

	g.Current.Cards = append(g.Current.Cards, c)

	if len(g.Current.Cards) == g.numPlayers() {
		g.completeTrick()
	} else {
		g.ToAct = (playerIdx + 1) % g.numPlayers()
	}
	return nil
}

func (g *Game) validatePlay(playerIdx int, c Card) error {
	// Collect all cards the player can play (hand + visible tableau cards).
	available := g.AvailableCards(playerIdx)
	if findCard(available, c) < 0 {
		return errors.New("card not available to play: " + c.String())
	}

	// If not leading, must follow suit if possible.
	if len(g.Current.Cards) > 0 {
		ledSuit := g.Current.Cards[0].EffectiveSuit(g.Trump)
		// Check if player has any card of the led suit.
		hasSuit := false
		for _, a := range available {
			if a.EffectiveSuit(g.Trump) == ledSuit {
				hasSuit = true
				break
			}
		}
		if hasSuit && c.EffectiveSuit(g.Trump) != ledSuit {
			return errors.New("must follow suit")
		}
	}
	return nil
}

// AvailableCards returns all cards a player may legally play.
// In the 2-player variant, only cards from the active hand type are available:
//   - TwoPlayerHandType == 0 → private hand only
//   - TwoPlayerHandType == 1 → visible tableau cards only
//
// Tableau layout (2-player): Tableau[i] = face-down of column i,
// Tableau[5+i] = face-up of column i, for i in 0..4.
// An empty slot is represented by Card{} (zero value).
func (g *Game) AvailableCards(playerIdx int) []Card {
	p := &g.Players[playerIdx]
	if g.Variant == TwoPlayer {
		if g.TwoPlayerHandType == 0 {
			// Sub-game 1 type: private hand only.
			cards := make([]Card, len(p.Hand))
			copy(cards, p.Hand)
			return cards
		}
		// Sub-game 2 type: visible tableau cards only.
		var cards []Card
		for i := 0; i < 5; i++ {
			if p.Tableau[5+i] != (Card{}) {
				cards = append(cards, p.Tableau[5+i])
			}
		}
		return cards
	}
	cards := make([]Card, len(p.Hand))
	copy(cards, p.Hand)
	return cards
}

func (g *Game) removeFromPlayerCards(playerIdx int, c Card) error {
	p := &g.Players[playerIdx]
	if idx := findCard(p.Hand, c); idx >= 0 {
		p.Hand = removeAt(p.Hand, idx)
		return nil
	}
	if g.Variant == TwoPlayer {
		// Tableau column layout: Tableau[i] = face-down of column i (i=0..4),
		//                        Tableau[5+i] = face-up of column i.
		// Card{} is the empty-slot sentinel.
		for i := 0; i < 5; i++ {
			if p.Tableau[5+i] == c {
				// Promote the face-down card (if any) to face-up position.
				p.Tableau[5+i] = p.Tableau[i]
				p.Tableau[i] = Card{} // empty face-down slot
				return nil
			}
		}
	}
	return errors.New("card not found on player")
}

func (g *Game) completeTrick() {
	winOffset := g.Current.winner(g.Trump)
	winner := (g.Current.Leader + winOffset) % g.numPlayers()
	g.Players[winner].Tricks++
	g.Tricks = append(g.Tricks, g.Current)

	totalTricks := g.totalTricksForVariant()
	if len(g.Tricks) == totalTricks {
		g.Phase = PhaseScoring
		g.computeScore()
		g.Phase = PhaseEnd
		return
	}

	// 2-player: after the first 10 tricks, switch to the other hand type.
	if g.Variant == TwoPlayer && len(g.Tricks) == 10 {
		if g.TwoPlayerHandType == 0 {
			g.TwoPlayerHandType = 1
		} else {
			g.TwoPlayerHandType = 0
		}
	}

	g.Current = Trick{Leader: winner}
	g.ToAct = winner
}

func (g *Game) totalTricksForVariant() int {
	if g.Variant == TwoPlayer {
		return 20 // each player has 20 cards
	}
	return 10
}

// ── Scoring ──────────────────────────────────────────────────────────────────

// scoreTable maps (level, suit) → points for making the contract.
// Rows: levels 6–10. Cols: ♠ ♣ ♦ ♥ NT.
var scoreTable = [5][5]int{
	{40, 60, 80, 100, 120},   // level 6
	{140, 160, 180, 200, 220}, // level 7
	{240, 260, 280, 300, 320}, // level 8
	{340, 360, 380, 400, 420}, // level 9
	{440, 460, 480, 500, 520}, // level 10
}

// overtrickBonus is the extra points per trick beyond the contract.
const overtrickBonus = 10

func (g *Game) computeScore() {
	contractorTeam := g.team(g.Contractor)
	defenderTeam := Team(1 - int(contractorTeam))

	contractorTricks := g.tricksForTeam(contractorTeam)

	switch g.Contract.Suit {
	case BidMisere:
		// Contractor wins if they take 0 tricks.
		if contractorTricks == 0 {
			g.Scores[contractorTeam] += 250
		} else {
			g.Scores[contractorTeam] -= 250
		}
	case BidOpenMisere:
		if contractorTricks == 0 {
			g.Scores[contractorTeam] += 500
		} else {
			g.Scores[contractorTeam] -= 500
		}
	default:
		target := int(g.Contract.Level)
		base := scoreTable[int(g.Contract.Level)-6][int(g.Contract.Suit)]
		if contractorTricks >= target {
			over := contractorTricks - target
			g.Scores[contractorTeam] += base + over*overtrickBonus
		} else {
			g.Scores[contractorTeam] -= base
		}
		// Defenders score 10 per trick.
		defTricks := g.tricksForTeam(defenderTeam)
		g.Scores[defenderTeam] += defTricks * overtrickBonus
	}
}

func (g *Game) tricksForTeam(t Team) int {
	total := 0
	for i, p := range g.Players {
		if g.team(i) == t {
			total += p.Tricks
		}
	}
	return total
}

// ── Helpers ──────────────────────────────────────────────────────────────────

func findCard(hand []Card, c Card) int {
	for i, h := range hand {
		if h == c {
			return i
		}
	}
	return -1
}

func removeAt(s []Card, i int) []Card {
	out := make([]Card, len(s)-1)
	copy(out, s[:i])
	copy(out[i:], s[i+1:])
	return out
}
