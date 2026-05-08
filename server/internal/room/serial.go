package room

import (
	"github.com/remi/5_hundred/internal/game"
	"github.com/remi/5_hundred/internal/protocol"
	"github.com/remi/5_hundred/internal/ws"
)

// ── Conversion helpers: game → protocol DTOs ──────────────────────────────────

func cardToDTO(c game.Card) protocol.CardDTO {
	return protocol.CardDTO{Rank: int(c.Rank), Suit: int(c.Suit)}
}

func cardsToDTO(cards []game.Card) []protocol.CardDTO {
	out := make([]protocol.CardDTO, len(cards))
	for i, c := range cards {
		out[i] = cardToDTO(c)
	}
	return out
}

func bidSuitStr(s game.BidSuit) string {
	switch s {
	case game.BidSpades:
		return "S"
	case game.BidClubs:
		return "C"
	case game.BidDiamonds:
		return "D"
	case game.BidHearts:
		return "H"
	case game.BidNoTrumps:
		return "NT"
	case game.BidMisere:
		return "Misere"
	case game.BidOpenMisere:
		return "OpenMisere"
	}
	return ""
}

func bidToDTO(b game.Bid) protocol.BidDTO {
	if b.Pass {
		return protocol.BidDTO{Pass: true}
	}
	return protocol.BidDTO{
		Level: int(b.Level),
		Suit:  bidSuitStr(b.Suit),
	}
}

func suitStr(s game.Suit) string {
	switch s {
	case game.Spades:
		return "S"
	case game.Clubs:
		return "C"
	case game.Diamonds:
		return "D"
	case game.Hearts:
		return "H"
	}
	return ""
}

func phaseStr(p game.Phase) string {
	switch p {
	case game.PhaseBidding:
		return "bidding"
	case game.PhaseKitty:
		return "kitty"
	case game.PhaseChooseHand:
		return "choose_hand"
	case game.PhasePlaying:
		return "playing"
	case game.PhaseScoring:
		return "scoring"
	case game.PhaseEnd:
		return "end"
	}
	return "unknown"
}

// BuildGameState builds a GameStatePayload for a specific seat.
// The requesting seat sees their own hand; opponents see only card counts.
func BuildGameState(g *game.Game, r *Room, forSeat int) protocol.GameStatePayload {
	r.mu.Lock()
	seats := r.seats
	r.mu.Unlock()

	players := make([]protocol.PlayerDTO, len(g.Players))
	for i, p := range g.Players {
		name := ""
		if seats[i] != nil {
			name = seats[i].Name
		}
		dto := protocol.PlayerDTO{
			Name:      name,
			HandCount: len(p.Hand),
			TricksWon: p.Tricks,
		}
		if i == forSeat {
			dto.Hand = cardsToDTO(p.Hand)
			if len(p.Tableau) > 0 {
				dto.Tableau = cardsToDTO(p.Tableau)
			}
		} else if len(p.Tableau) > 0 {
			// Opponent: send tableau with face-down slots (indices 0-4) zeroed out.
			// Face-up cards (indices 5-9) remain visible as public information.
			visible := make([]game.Card, len(p.Tableau))
			copy(visible, p.Tableau)
			for j := 0; j < 5; j++ {
				visible[j] = game.Card{} // hide face-down
			}
			dto.Tableau = cardsToDTO(visible)
		}
		if len(p.Tableau) > 0 {
			dto.TableauCount = len(p.Tableau)
		}
		players[i] = dto
	}

	var contractDTO *protocol.BidDTO
	if g.Phase != game.PhaseBidding {
		b := bidToDTO(g.Contract)
		contractDTO = &b
	}

	// Trick in progress.
	trick := make([]protocol.CardDTO, len(g.Current.Cards))
	for i, c := range g.Current.Cards {
		trick[i] = cardToDTO(c)
	}

	// All bids so far.
	bids := make([]protocol.BidDTO, len(g.Bids))
	for i, b := range g.Bids {
		bids[i] = bidToDTO(b)
	}

	// Kitty: only visible to contractor during kitty phase.
	var kitty []protocol.CardDTO
	if g.Phase == game.PhaseKitty && forSeat == g.Contractor {
		kitty = cardsToDTO(g.Kitty)
	}

	return protocol.GameStatePayload{
		Phase:             phaseStr(g.Phase),
		Players:           players,
		Kitty:             kitty,
		Contract:          contractDTO,
		Contractor:        g.Contractor,
		Trump:             suitStr(g.Trump),
		ToAct:             g.ToAct,
		Trick:             trick,
		TrickLeader:       g.Current.Leader,
		TrickCount:        len(g.Tricks),
		Scores:            g.Scores,
		Bids:              bids,
		TwoPlayerHandType: g.TwoPlayerHandType,
	}
}

type seatedClient struct {
	client *ws.Client
	seat   int
}

// BroadcastGameState sends a tailored GameStatePayload to each seated player.
func BroadcastGameState(g *game.Game, r *Room) {
	r.mu.Lock()
	var seated []seatedClient
	for i := 0; i < r.maxSeat; i++ {
		if r.seats[i] != nil {
			seated = append(seated, seatedClient{r.seats[i], i})
		}
	}
	r.mu.Unlock()

	for _, sc := range seated {
		gs := BuildGameState(g, r, sc.seat)
		sc.client.Send(protocol.TypeGameState, gs)
	}
}
