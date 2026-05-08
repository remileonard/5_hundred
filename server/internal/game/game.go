package game

import (
	"errors"
	"math/rand"
)

// Phase represents the current stage of the game.
type Phase int

const (
	PhaseBidding Phase = iota
	PhaseKitty         // winner picks up kitty and discards
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
	Tricks     []Trick  // completed tricks (2-player: each combined trick stores 2 sub-trick entries)
	Current    Trick    // trick in progress
	ToAct      int      // index of the player who must act next
	Scores     [2]int   // cumulative team scores (Team0, Team1)

	// 2-player variant fields (only used during PhasePlaying in TwoPlayer variant).
	// TwoPlayerHandType reflects which source is currently active:
	//   0 = private hand, 1 = tableau (open hand).
	// It is updated as each card is played within the combined trick.
	TwoPlayerHandType int
	// TwoPlayerFirstSource is the source chosen by the leader for the first
	// sub-play of the current combined trick (0=hand, 1=tableau).
	// Set when the leader plays their first card.
	TwoPlayerFirstSource int
	// TwoPlayerSecondLeader is the player who leads the second sub-play of the
	// current combined trick. Set to the winner of the first sub-play.
	TwoPlayerSecondLeader int
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

	// Transition to playing; contractor leads the first trick.
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

	// Determine the source before removing (for 2-player combined-trick tracking).
	source := 0
	if g.Variant == TwoPlayer && findCard(g.Players[playerIdx].Hand, c) < 0 {
		source = 1 // not in hand → tableau
	}

	// Remove from hand or tableau.
	if err := g.removeFromPlayerCards(playerIdx, c); err != nil {
		return err
	}

	g.Current.Cards = append(g.Current.Cards, c)

	if g.Variant == TwoPlayer {
		n := len(g.Current.Cards)
		switch n {
		case 1:
			// Leader just played their first card of the combined trick.
			// Record the source they chose and pass to the follower.
			g.TwoPlayerFirstSource = source
			g.TwoPlayerHandType = source
			g.ToAct = 1 - playerIdx
		case 2:
			// Follower has responded to the first sub-play.
			// Determine first-sub-play winner; they lead the second sub-play.
			w1Off := twoCardWinner(g.Current.Cards[0], g.Current.Cards[1], g.Trump)
			g.TwoPlayerSecondLeader = (g.Current.Leader + w1Off) % 2
			g.TwoPlayerHandType = 1 - g.TwoPlayerFirstSource // switch source for display
			g.ToAct = g.TwoPlayerSecondLeader
		case 3:
			// Second-sub-play leader has played; follower responds.
			g.ToAct = 1 - playerIdx
		case 4:
			// Both sub-plays complete: combined trick is done.
			g.completeCombinedTrick()
		}
	} else {
		if len(g.Current.Cards) == g.numPlayers() {
			g.completeTrick()
		} else {
			g.ToAct = (playerIdx + 1) % g.numPlayers()
		}
	}
	return nil
}

func (g *Game) validatePlay(playerIdx int, c Card) error {
	// Collect all cards the player can play given the current trick step.
	available := g.AvailableCards(playerIdx)
	if findCard(available, c) < 0 {
		return errors.New("card not available to play: " + c.String())
	}

	if g.Variant == TwoPlayer {
		n := len(g.Current.Cards)
		// Follower must follow suit: step 1 (responding to Cards[0]) and
		// step 3 (responding to Cards[2]).
		if n == 1 || n == 3 {
			ledCard := g.Current.Cards[n-1] // Cards[0] or Cards[2]
			ledSuit := ledCard.EffectiveSuit(g.Trump)
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
		// Steps 0 and 2 are the leaders of their respective sub-plays;
		// they may play any available card from the appropriate source.
		return nil
	}

	// 4-player: must follow suit if possible.
	if len(g.Current.Cards) > 0 {
		ledSuit := g.Current.Cards[0].EffectiveSuit(g.Trump)
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

// AvailableCards returns all cards a player may legally play at this step.
//
// In the 2-player variant each combined trick has four plays:
//
//	Step 0 (n=0): leader plays their first card — both hand and face-up tableau cards
//	              are available; the choice of source is implicit in which card is played.
//	Step 1 (n=1): follower responds to the first sub-play — same source as step 0.
//	Step 2 (n=2): winner of first sub-play leads the second source — other source only.
//	Step 3 (n=3): follower responds to the second sub-play — same other source.
//
// Tableau layout (2-player): Tableau[i] = face-down of column i (i=0..4),
// Tableau[5+i] = face-up of column i.  Card{} is the empty-slot sentinel.
func (g *Game) AvailableCards(playerIdx int) []Card {
	p := &g.Players[playerIdx]
	if g.Variant == TwoPlayer {
		n := len(g.Current.Cards)
		switch n {
		case 0:
			// Leader: both hand and face-up tableau cards are available.
			cards := make([]Card, len(p.Hand))
			copy(cards, p.Hand)
			for i := 0; i < 5; i++ {
				if p.Tableau[5+i] != (Card{}) {
					cards = append(cards, p.Tableau[5+i])
				}
			}
			return cards
		case 1:
			// Follower in first sub-play: same source as the leader's card.
			return g.cardsFromSource(playerIdx, g.TwoPlayerFirstSource)
		case 2, 3:
			// Second sub-play (leader at step 2, follower at step 3): other source.
			return g.cardsFromSource(playerIdx, 1-g.TwoPlayerFirstSource)
		}
		return nil
	}
	cards := make([]Card, len(p.Hand))
	copy(cards, p.Hand)
	return cards
}

// cardsFromSource returns a player's available cards from a specific source.
// source 0 = private hand, source 1 = face-up tableau cards only.
func (g *Game) cardsFromSource(playerIdx, source int) []Card {
	p := &g.Players[playerIdx]
	if source == 0 {
		cards := make([]Card, len(p.Hand))
		copy(cards, p.Hand)
		return cards
	}
	var cards []Card
	for i := 0; i < 5; i++ {
		if p.Tableau[5+i] != (Card{}) {
			cards = append(cards, p.Tableau[5+i])
		}
	}
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

// twoCardWinner evaluates a 2-card sub-play and returns the winning offset:
// 0 if the first card (led by the sub-play leader) wins, 1 if the second wins.
func twoCardWinner(c0, c1 Card, trump Suit) int {
	ledSuit := c0.EffectiveSuit(trump)
	if c1.Beats(c0, ledSuit, trump) {
		return 1
	}
	return 0
}

// completeCombinedTrick resolves a finished 2-player combined trick.
//
// Each combined trick consists of four cards — two sub-plays of two cards each.
// Sub-play 1 (Cards[0:2]): first source chosen by the combined-trick leader.
// Sub-play 2 (Cards[2:4]): other source, led by the winner of sub-play 1.
//
// Both sub-play winners receive a trick credit, and each sub-play is stored
// as a separate two-card Trick entry so that the total across a full game
// remains 20 (10 combined tricks × 2 sub-plays).
// The winner of sub-play 2 leads the next combined trick.
func (g *Game) completeCombinedTrick() {
	cards := g.Current.Cards

	// Sub-play 1.
	cards1 := []Card{cards[0], cards[1]}
	t1 := Trick{Leader: g.Current.Leader, Cards: cards1}
	w1Off := twoCardWinner(cards[0], cards[1], g.Trump)
	w1 := (g.Current.Leader + w1Off) % 2
	g.Players[w1].Tricks++
	g.Tricks = append(g.Tricks, t1)

	// Sub-play 2.
	cards2 := []Card{cards[2], cards[3]}
	t2 := Trick{Leader: g.TwoPlayerSecondLeader, Cards: cards2}
	w2Off := twoCardWinner(cards[2], cards[3], g.Trump)
	w2 := (g.TwoPlayerSecondLeader + w2Off) % 2
	g.Players[w2].Tricks++
	g.Tricks = append(g.Tricks, t2)

	// 20 sub-trick entries = 10 combined tricks = game over.
	if len(g.Tricks) == g.totalTricksForVariant() {
		g.Phase = PhaseScoring
		g.computeScore()
		g.Phase = PhaseEnd
		return
	}

	// Winner of sub-play 2 leads the next combined trick.
	g.TwoPlayerHandType = 0
	g.TwoPlayerFirstSource = 0
	g.TwoPlayerSecondLeader = 0
	g.Current = Trick{Leader: w2}
	g.ToAct = w2
}

func (g *Game) completeTrick() {
	winOffset := g.Current.winner(g.Trump)
	winner := (g.Current.Leader + winOffset) % g.numPlayers()
	g.Players[winner].Tricks++
	g.Tricks = append(g.Tricks, g.Current)

	if len(g.Tricks) == g.totalTricksForVariant() {
		g.Phase = PhaseScoring
		g.computeScore()
		g.Phase = PhaseEnd
		return
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
