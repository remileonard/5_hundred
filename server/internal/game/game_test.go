package game

import (
	"math/rand"
	"testing"
)

// ── Card / Suit helpers ───────────────────────────────────────────────────────

func TestSisterSuit(t *testing.T) {
	cases := [][2]Suit{
		{Spades, Clubs},
		{Clubs, Spades},
		{Hearts, Diamonds},
		{Diamonds, Hearts},
	}
	for _, c := range cases {
		if got := sisterSuit(c[0]); got != c[1] {
			t.Errorf("sisterSuit(%v) = %v, want %v", c[0], got, c[1])
		}
	}
}

func TestLeftBower(t *testing.T) {
	jc := Card{Jack, Clubs}
	if !jc.IsLeftBower(Spades) {
		t.Error("Jack of Clubs should be left bower of Spades")
	}
	if jc.IsLeftBower(Hearts) {
		t.Error("Jack of Clubs should not be left bower of Hearts")
	}
}

func TestRightBower(t *testing.T) {
	js := Card{Jack, Spades}
	if !js.IsRightBower(Spades) {
		t.Error("Jack of Spades should be right bower of Spades")
	}
	if js.IsRightBower(Clubs) {
		t.Error("Jack of Spades should not be right bower of Clubs")
	}
}

func TestEffectiveSuit(t *testing.T) {
	// Left bower counts as trump suit.
	jc := Card{Jack, Clubs}
	if jc.EffectiveSuit(Spades) != Spades {
		t.Error("Jack of Clubs effective suit with Spade trump should be Spades")
	}
	// Regular card.
	a := Card{Ace, Hearts}
	if a.EffectiveSuit(Spades) != Hearts {
		t.Error("Ace of Hearts should stay Hearts with Spade trump")
	}
	// Joker.
	if TheJoker.EffectiveSuit(Hearts) != Hearts {
		t.Error("Joker effective suit should be trump suit")
	}
}

// ── Beats ─────────────────────────────────────────────────────────────────────

func TestBeats_TrumpBeatsNonTrump(t *testing.T) {
	trump := Hearts
	low := Card{Four, Hearts} // low trump (but 4♠ is not in deck; using 5♥ style)
	high := Card{Ace, Spades}
	if !low.Beats(high, Spades, trump) {
		t.Error("low trump should beat high non-trump")
	}
}

func TestBeats_JokerBeatsAll(t *testing.T) {
	trump := Spades
	as := Card{Ace, Spades}
	if !TheJoker.Beats(as, Spades, trump) {
		t.Error("Joker should beat Ace of Spades")
	}
}

func TestBeats_RightBowerBeatsLeftBower(t *testing.T) {
	trump := Spades
	right := Card{Jack, Spades}
	left := Card{Jack, Clubs}
	if !right.Beats(left, Spades, trump) {
		t.Error("right bower should beat left bower")
	}
}

func TestBeats_FollowSuit(t *testing.T) {
	trump := Hearts
	low := Card{Four, Spades} // 4♠ is in ♠ pack
	other := Card{King, Clubs}
	// low Spades beats King of Clubs if Clubs didn't follow the led Spades.
	if !low.Beats(other, Spades, trump) {
		t.Error("card following led suit should beat card that didn't")
	}
}

// ── Deck ─────────────────────────────────────────────────────────────────────

func TestBuildDeck(t *testing.T) {
	deck := buildDeck()
	if len(deck) != 43 {
		t.Fatalf("deck should have 43 cards, got %d", len(deck))
	}

	// Count by suit.
	counts := map[Suit]int{}
	jokers := 0
	for _, c := range deck {
		if c.IsJoker() {
			jokers++
		} else {
			counts[c.Suit]++
		}
	}
	if jokers != 1 {
		t.Errorf("expected 1 joker, got %d", jokers)
	}
	if counts[Spades] != 11 {
		t.Errorf("expected 11 Spades, got %d", counts[Spades])
	}
	if counts[Clubs] != 11 {
		t.Errorf("expected 11 Clubs, got %d", counts[Clubs])
	}
	if counts[Diamonds] != 10 {
		t.Errorf("expected 10 Diamonds, got %d", counts[Diamonds])
	}
	if counts[Hearts] != 10 {
		t.Errorf("expected 10 Hearts, got %d", counts[Hearts])
	}
}

func TestShuffle_Deterministic(t *testing.T) {
	deck := buildDeck()
	rng1 := rand.New(rand.NewSource(42))
	rng2 := rand.New(rand.NewSource(42))
	s1 := Shuffle(deck, rng1)
	s2 := Shuffle(deck, rng2)
	for i := range s1 {
		if s1[i] != s2[i] {
			t.Fatalf("shuffle not deterministic at index %d", i)
		}
	}
}

func TestShuffle_DoesNotMutateOriginal(t *testing.T) {
	deck := buildDeck()
	orig := make([]Card, len(deck))
	copy(orig, deck)
	rng := rand.New(rand.NewSource(0))
	Shuffle(deck, rng)
	for i := range deck {
		if deck[i] != orig[i] {
			t.Fatal("Shuffle should not mutate the original deck")
		}
	}
}

// ── Deal ─────────────────────────────────────────────────────────────────────

func TestDeal_FourPlayer(t *testing.T) {
	deck := buildDeck()
	rng := rand.New(rand.NewSource(1))
	shuffled := Shuffle(deck, rng)
	res := Deal(shuffled, FourPlayer)

	if len(res.Hands) != 4 {
		t.Fatalf("expected 4 hands, got %d", len(res.Hands))
	}
	for i, h := range res.Hands {
		if len(h) != 10 {
			t.Errorf("hand %d: expected 10 cards, got %d", i, len(h))
		}
	}
	if len(res.Kitty) != 3 {
		t.Errorf("expected 3-card kitty, got %d", len(res.Kitty))
	}
	if res.Tableau != nil {
		t.Error("4-player deal should not produce tableaux")
	}
	// All 43 cards accounted for.
	total := len(res.Kitty)
	for _, h := range res.Hands {
		total += len(h)
	}
	if total != 43 {
		t.Errorf("total cards should be 43, got %d", total)
	}
}

func TestDeal_TwoPlayer(t *testing.T) {
	deck := buildDeck()
	rng := rand.New(rand.NewSource(2))
	shuffled := Shuffle(deck, rng)
	res := Deal(shuffled, TwoPlayer)

	if len(res.Hands) != 2 {
		t.Fatalf("expected 2 hands, got %d", len(res.Hands))
	}
	if len(res.Tableau) != 2 {
		t.Fatalf("expected 2 tableaux, got %d", len(res.Tableau))
	}
	for i := 0; i < 2; i++ {
		if len(res.Hands[i]) != 10 {
			t.Errorf("hand %d: expected 10 cards, got %d", i, len(res.Hands[i]))
		}
		if len(res.Tableau[i]) != 10 {
			t.Errorf("tableau %d: expected 10 cards, got %d", i, len(res.Tableau[i]))
		}
	}
	if len(res.Kitty) != 3 {
		t.Errorf("expected 3-card kitty, got %d", len(res.Kitty))
	}
	total := len(res.Kitty)
	for i := 0; i < 2; i++ {
		total += len(res.Hands[i]) + len(res.Tableau[i])
	}
	if total != 43 {
		t.Errorf("total cards should be 43, got %d", total)
	}
}

// ── Bids ─────────────────────────────────────────────────────────────────────

func TestBid_HigherThan(t *testing.T) {
	b6S := Bid{Level: 6, Suit: BidSpades}
	b6C := Bid{Level: 6, Suit: BidClubs}
	b7S := Bid{Level: 7, Suit: BidSpades}
	misere := Bid{Suit: BidMisere}
	openMisere := Bid{Suit: BidOpenMisere}

	if !b6C.HigherThan(b6S) {
		t.Error("6♣ should be higher than 6♠")
	}
	if !b7S.HigherThan(b6C) {
		t.Error("7♠ should be higher than 6♣")
	}
	if !misere.HigherThan(b7S) {
		// Misère is between 8NT and 9♠.
		b8NT := Bid{Level: 8, Suit: BidNoTrumps}
		if !misere.HigherThan(b8NT) {
			t.Error("Misère should be higher than 8NT")
		}
		b9S := Bid{Level: 9, Suit: BidSpades}
		if !b9S.HigherThan(misere) {
			t.Error("9♠ should be higher than Misère")
		}
	}
	if !openMisere.HigherThan(misere) {
		t.Error("Open Misère should be higher than Misère")
	}
}

func TestValidBids(t *testing.T) {
	start := Bid{Pass: true}
	bids := ValidBids(start)
	// Should have all 25 normal bids + Misère + Open Misère = 27.
	if len(bids) != 27 {
		t.Errorf("expected 27 valid bids from start, got %d", len(bids))
	}
}

// ── Game state machine ────────────────────────────────────────────────────────

func newGame4(seed int64) *Game {
	return New(FourPlayer, rand.New(rand.NewSource(seed)))
}

func newGame2(seed int64) *Game {
	return New(TwoPlayer, rand.New(rand.NewSource(seed)))
}

func TestGame_InitialPhase(t *testing.T) {
	g := newGame4(1)
	if g.Phase != PhaseBidding {
		t.Error("game should start in PhaseBidding")
	}
	if g.ToAct != 0 {
		t.Error("first to act should be player 0")
	}
}

// playBiddingPhase has player 0 bid 6♠ and all others pass.
func playBiddingPhase(t *testing.T, g *Game) {
	t.Helper()
	n := g.numPlayers()
	if err := g.PlaceBid(0, Bid{Level: 6, Suit: BidSpades}); err != nil {
		t.Fatalf("p0 bid failed: %v", err)
	}
	for i := 1; i < n; i++ {
		if err := g.PlaceBid(i, Bid{Pass: true}); err != nil {
			t.Fatalf("p%d pass failed: %v", i, err)
		}
	}
}

func TestGame_BiddingEndsWithContract(t *testing.T) {
	g := newGame4(1)
	playBiddingPhase(t, g)

	if g.Phase != PhaseKitty {
		t.Fatalf("expected PhaseKitty after bidding, got %v", g.Phase)
	}
	if g.Contractor != 0 {
		t.Errorf("expected contractor = 0, got %d", g.Contractor)
	}
	if g.Trump != Spades {
		t.Errorf("expected trump = Spades, got %v", g.Trump)
	}
}

func TestGame_WrongPlayerBid(t *testing.T) {
	g := newGame4(1)
	err := g.PlaceBid(1, Bid{Level: 6, Suit: BidSpades}) // player 1 before player 0
	if err == nil {
		t.Error("expected error when wrong player bids")
	}
}

func TestGame_LowerBidRejected(t *testing.T) {
	g := newGame4(1)
	if err := g.PlaceBid(0, Bid{Level: 7, Suit: BidSpades}); err != nil {
		t.Fatal(err)
	}
	err := g.PlaceBid(1, Bid{Level: 6, Suit: BidClubs}) // lower than 7♠
	if err == nil {
		t.Error("expected error for lower bid")
	}
}

func TestGame_KittyPickupAndDiscard(t *testing.T) {
	g := newGame4(2)
	playBiddingPhase(t, g)

	contractor := g.Contractor
	if err := g.PickUpKitty(contractor); err != nil {
		t.Fatalf("PickUpKitty failed: %v", err)
	}
	if len(g.Players[contractor].Hand) != 13 {
		t.Errorf("expected 13 cards after picking up kitty, got %d", len(g.Players[contractor].Hand))
	}

	discard := g.Players[contractor].Hand[:3]
	if err := g.Discard(contractor, discard); err != nil {
		t.Fatalf("Discard failed: %v", err)
	}
	if len(g.Players[contractor].Hand) != 10 {
		t.Errorf("expected 10 cards after discard, got %d", len(g.Players[contractor].Hand))
	}
	if g.Phase != PhasePlaying {
		t.Errorf("expected PhasePlaying after discard, got %v", g.Phase)
	}
}

func TestGame_DiscardWrongCount(t *testing.T) {
	g := newGame4(2)
	playBiddingPhase(t, g)
	g.PickUpKitty(g.Contractor)
	err := g.Discard(g.Contractor, g.Players[g.Contractor].Hand[:2])
	if err == nil {
		t.Error("expected error discarding wrong number of cards")
	}
}

// playFullGame4 plays a complete 4-player game with simple (first-card) strategy.
func playFullGame4(t *testing.T, seed int64) *Game {
	t.Helper()
	g := newGame4(seed)
	playBiddingPhase(t, g)
	g.PickUpKitty(g.Contractor)
	g.Discard(g.Contractor, g.Players[g.Contractor].Hand[:3])

	for g.Phase == PhasePlaying {
		p := g.ToAct
		available := g.AvailableCards(p)
		var cardToPlay Card
		// Play any legal card: first try following suit, else first available.
		if len(g.Current.Cards) > 0 {
			ledSuit := g.Current.Cards[0].EffectiveSuit(g.Trump)
			for _, c := range available {
				if c.EffectiveSuit(g.Trump) == ledSuit {
					cardToPlay = c
					break
				}
			}
		}
		if cardToPlay == (Card{}) {
			cardToPlay = available[0]
		}
		if err := g.PlayCard(p, cardToPlay); err != nil {
			t.Fatalf("PlayCard(p%d, %v): %v", p, cardToPlay, err)
		}
	}
	return g
}

func TestGame_FullGame4Player(t *testing.T) {
	g := playFullGame4(t, 3)
	if g.Phase != PhaseEnd {
		t.Fatalf("expected PhaseEnd, got %v", g.Phase)
	}
	total := 0
	for _, p := range g.Players {
		total += p.Tricks
	}
	if total != 10 {
		t.Errorf("expected 10 tricks total, got %d", total)
	}
}

func TestGame_ScoreIsNonZero(t *testing.T) {
	g := playFullGame4(t, 3)
	if g.Scores[0] == 0 && g.Scores[1] == 0 {
		t.Error("expected non-zero scores after a full game")
	}
}

// ── 2-player variant ──────────────────────────────────────────────────────────

func TestGame_TwoPlayer_DealAndPlay(t *testing.T) {
	g := newGame2(10)
	if len(g.Players) != 2 {
		t.Fatalf("expected 2 players, got %d", len(g.Players))
	}
	for i, p := range g.Players {
		if len(p.Hand) != 10 {
			t.Errorf("p%d hand: expected 10, got %d", i, len(p.Hand))
		}
		if len(p.Tableau) != 10 {
			t.Errorf("p%d tableau: expected 10, got %d", i, len(p.Tableau))
		}
	}

	// Bid and reach kitty phase.
	if err := g.PlaceBid(0, Bid{Level: 6, Suit: BidSpades}); err != nil {
		t.Fatal(err)
	}
	if err := g.PlaceBid(1, Bid{Pass: true}); err != nil {
		t.Fatal(err)
	}
	if g.Phase != PhaseKitty {
		t.Fatalf("expected PhaseKitty, got %v", g.Phase)
	}

	g.PickUpKitty(g.Contractor)
	g.Discard(g.Contractor, g.Players[g.Contractor].Hand[:3])

	// 2-player goes directly to PhasePlaying (no PhaseChooseHand step).
	if g.Phase != PhasePlaying {
		t.Fatalf("expected PhasePlaying after discard, got %v", g.Phase)
	}

	// Play through the full game.  Each combined trick has 4 plays:
	//   step 0 (n=0): leader plays from forced source (or any source on the first trick)
	//   step 1 (n=1): follower plays same source, following suit if possible
	//   step 2 (n=2): SAME leader plays from the other source
	//   step 3 (n=3): other player follows suit if possible from the other source
	// All 4 cards form a single trick; the winner is the player with the best card.
	for g.Phase == PhasePlaying {
		p := g.ToAct
		available := g.AvailableCards(p)
		if len(available) == 0 {
			t.Fatalf("no available cards for player %d (step=%d)", p, len(g.Current.Cards))
		}
		n := len(g.Current.Cards)
		var cardToPlay Card
		// At follower steps, try to follow suit.
		if n == 1 || n == 3 {
			ledSuit := g.Current.Cards[n-1].EffectiveSuit(g.Trump)
			for _, c := range available {
				if c.EffectiveSuit(g.Trump) == ledSuit {
					cardToPlay = c
					break
				}
			}
		}
		if cardToPlay == (Card{}) {
			cardToPlay = available[0]
		}
		if err := g.PlayCard(p, cardToPlay); err != nil {
			t.Fatalf("PlayCard(p%d, %v) at step %d: %v", p, cardToPlay, n, err)
		}
	}

	if g.Phase != PhaseEnd {
		t.Fatalf("expected PhaseEnd, got %v", g.Phase)
	}
	total := 0
	for _, pl := range g.Players {
		total += pl.Tricks
	}
	// 10 combined tricks = 10 trick wins total (one winner per combined trick).
	if total != 10 {
		t.Errorf("2-player: expected 10 trick credits total, got %d", total)
	}
}

// TestGame_TwoPlayer_CombinedTrickStructure verifies that:
//   - After Discard, the game is immediately in PhasePlaying (no PhaseChooseHand).
//   - The leader at step 0 can play from either hand or tableau (first trick: free choice).
//   - At step 1, the follower is restricted to the same source as the leader.
//   - At step 2, the SAME leader leads from the OTHER source (not winner of sub-play 1).
//   - At step 3, the follower responds from the other source.
//   - Completing 4 plays produces 1 trick entry (a single 4-card trick) and 1 trick credit.
//   - After the trick, TwoPlayerForcedSource is set to the source of the winning card.
func TestGame_TwoPlayer_CombinedTrickStructure(t *testing.T) {
	g := newGame2(5)
	playBiddingPhase(t, g)
	g.PickUpKitty(g.Contractor)
	g.Discard(g.Contractor, g.Players[g.Contractor].Hand[:3])

	if g.Phase != PhasePlaying {
		t.Fatalf("expected PhasePlaying after discard in 2-player, got %v", g.Phase)
	}

	// Step 0: contractor (leader) should be able to play from hand OR tableau.
	leader := g.Contractor
	avail0 := g.AvailableCards(leader)
	hasHand := len(g.Players[leader].Hand) > 0
	hasTab := false
	for i := 0; i < 5; i++ {
		if g.Players[leader].Tableau[5+i] != (Card{}) {
			hasTab = true
		}
	}
	if hasHand && hasTab && len(avail0) <= 5 {
		t.Errorf("step 0: expected cards from both sources, got %d", len(avail0))
	}

	// Play step 0: leader plays a hand card.
	handCard := g.Players[leader].Hand[0]
	if err := g.PlayCard(leader, handCard); err != nil {
		t.Fatalf("step 0 play failed: %v", err)
	}
	if g.TwoPlayerFirstSource != 0 {
		t.Errorf("expected TwoPlayerFirstSource=0 (hand), got %d", g.TwoPlayerFirstSource)
	}

	// Step 1: follower must have only hand cards available.
	follower := 1 - leader
	avail1 := g.AvailableCards(follower)
	for _, c := range avail1 {
		found := findCard(g.Players[follower].Hand, c) >= 0
		if !found {
			t.Errorf("step 1: non-hand card %v in available cards", c)
		}
	}
	if len(avail1) == 0 {
		t.Fatalf("step 1: no available cards for follower")
	}
	if err := g.PlayCard(follower, avail1[0]); err != nil {
		t.Fatalf("step 1 play failed: %v", err)
	}
	if g.TwoPlayerHandType != 1 {
		t.Errorf("after sub-play 1, expected TwoPlayerHandType=1 (tableau, other source), got %d", g.TwoPlayerHandType)
	}

	// Step 2: the SAME leader (not sub-play 1's winner) leads from the other source (tableau).
	leader2 := g.TwoPlayerSecondLeader
	if leader2 != leader {
		t.Errorf("step 2: expected same leader %d, got %d", leader, leader2)
	}
	avail2 := g.AvailableCards(leader2)
	for _, c := range avail2 {
		found := false
		for i := 0; i < 5; i++ {
			if g.Players[leader2].Tableau[5+i] == c {
				found = true
				break
			}
		}
		if !found {
			t.Errorf("step 2: non-tableau card %v in available cards", c)
		}
	}
	if len(avail2) == 0 {
		t.Fatalf("step 2: no available cards for second-sub-play leader %d", leader2)
	}
	if err := g.PlayCard(leader2, avail2[0]); err != nil {
		t.Fatalf("step 2 play failed: %v", err)
	}

	// Step 3: follower responds from the other source (tableau, since sub-play 1 was hand).
	follower2 := 1 - leader2
	avail3 := g.AvailableCards(follower2)
	for _, c := range avail3 {
		found := false
		for i := 0; i < 5; i++ {
			if g.Players[follower2].Tableau[5+i] == c {
				found = true
				break
			}
		}
		if !found {
			t.Errorf("step 3: non-tableau card %v in available cards", c)
		}
	}
	if len(avail3) == 0 {
		t.Fatalf("step 3: no available cards for follower %d", follower2)
	}
	// At step 3 the follower must follow suit of Cards[2] if possible.
	cardToPlay3 := avail3[0]
	ledSuit3 := g.Current.Cards[2].EffectiveSuit(g.Trump)
	for _, c := range avail3 {
		if c.EffectiveSuit(g.Trump) == ledSuit3 {
			cardToPlay3 = c
			break
		}
	}
	if err := g.PlayCard(follower2, cardToPlay3); err != nil {
		t.Fatalf("step 3 play failed: %v", err)
	}

	// After the combined trick: 1 trick entry stored (4-card single trick), game still playing.
	if len(g.Tricks) != 1 {
		t.Errorf("after 1 combined trick, expected 1 trick entry, got %d", len(g.Tricks))
	}
	if g.Phase != PhasePlaying {
		t.Errorf("expected PhasePlaying after first combined trick, got %v", g.Phase)
	}
	total := g.Players[0].Tricks + g.Players[1].Tricks
	if total != 1 {
		t.Errorf("after 1 combined trick, expected 1 trick credit total, got %d", total)
	}
	// TwoPlayerForcedSource must now be set (0 or 1) for the next trick.
	if g.TwoPlayerForcedSource < 0 {
		t.Errorf("after trick 1, TwoPlayerForcedSource should be set (>=0), got %d", g.TwoPlayerForcedSource)
	}
}

// TestGame_TwoPlayer_LeaderChoosesTableauFirst verifies that the leader can also
// choose to play from the tableau first, and the follower is then restricted to
// tableau cards at step 1.
func TestGame_TwoPlayer_LeaderChoosesTableauFirst(t *testing.T) {
	g := newGame2(7)
	playBiddingPhase(t, g)
	g.PickUpKitty(g.Contractor)
	g.Discard(g.Contractor, g.Players[g.Contractor].Hand[:3])

	leader := g.Contractor

	// Find a face-up tableau card to play first.
	var tabCard Card
	for i := 0; i < 5; i++ {
		if g.Players[leader].Tableau[5+i] != (Card{}) {
			tabCard = g.Players[leader].Tableau[5+i]
			break
		}
	}
	if tabCard == (Card{}) {
		t.Skip("no face-up tableau card available for leader")
	}

	// Play tableau card as first card.
	if err := g.PlayCard(leader, tabCard); err != nil {
		t.Fatalf("step 0 (tableau first) failed: %v", err)
	}
	if g.TwoPlayerFirstSource != 1 {
		t.Errorf("expected TwoPlayerFirstSource=1 (tableau), got %d", g.TwoPlayerFirstSource)
	}

	// Step 1: follower must have only tableau cards.
	follower := 1 - leader
	avail1 := g.AvailableCards(follower)
	for _, c := range avail1 {
		found := false
		for i := 0; i < 5; i++ {
			if g.Players[follower].Tableau[5+i] == c {
				found = true
				break
			}
		}
		if !found {
			t.Errorf("step 1 (tableau first): non-tableau card %v in available", c)
		}
	}
}

// ── Score table ───────────────────────────────────────────────────────────────

func TestScoreTable_10NT(t *testing.T) {
	// 10NT should be worth 520 points.
	if scoreTable[4][4] != 520 {
		t.Errorf("10NT should be 520, got %d", scoreTable[4][4])
	}
}

func TestScoreTable_6Spades(t *testing.T) {
	if scoreTable[0][0] != 40 {
		t.Errorf("6♠ should be 40, got %d", scoreTable[0][0])
	}
}
