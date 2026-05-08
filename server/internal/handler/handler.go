package handler

import (
	"fmt"
	"log"

	"github.com/remi/5_hundred/internal/game"
	"github.com/remi/5_hundred/internal/protocol"
	"github.com/remi/5_hundred/internal/room"
	"github.com/remi/5_hundred/internal/ws"
)

// Handler dispatches incoming WebSocket messages to game/room logic.
type Handler struct {
	mgr *room.Manager
}

// NewHandler creates a Handler backed by the given Manager.
func NewHandler(mgr *room.Manager) *Handler {
	return &Handler{mgr: mgr}
}

// Handle is called by the Hub for every incoming message.
func (h *Handler) Handle(c *ws.Client, msg protocol.Msg) {
	switch msg.Type {
	case protocol.TypeIdentify:
		h.handleIdentify(c, msg)
	case protocol.TypeRoomList:
		h.handleRoomList(c)
	case protocol.TypeRoomCreate:
		h.handleRoomCreate(c, msg)
	case protocol.TypeRoomJoin:
		h.handleRoomJoin(c, msg)
	case protocol.TypeRoomLeave:
		h.handleRoomLeave(c)
	case protocol.TypeRoomStart:
		h.handleRoomStart(c)
	case protocol.TypeGameBid:
		h.handleGameBid(c, msg)
	case protocol.TypeGameDiscard:
		h.handleGameDiscard(c, msg)
	case protocol.TypeGamePlay:
		h.handleGamePlay(c, msg)
	default:
		c.Send(protocol.TypeError, protocol.ErrorPayload{
			Message: fmt.Sprintf("unknown message type: %s", msg.Type),
		})
	}
}

// ── identify ──────────────────────────────────────────────────────────────────

func (h *Handler) handleIdentify(c *ws.Client, msg protocol.Msg) {
	var p protocol.IdentifyPayload
	if err := protocol.DecodePayload(msg, &p); err != nil || p.Name == "" {
		sendError(c, "invalid identify payload")
		return
	}
	c.Name = p.Name
	c.Send(protocol.TypeWelcome, protocol.WelcomePayload{
		PlayerID: c.ID,
		Name:     c.Name,
	})
	log.Printf("player identified: %s (%s)", c.Name, c.ID)
}

// ── room.list ─────────────────────────────────────────────────────────────────

func (h *Handler) handleRoomList(c *ws.Client) {
	c.Send(protocol.TypeRoomListed, protocol.RoomListedPayload{
		Rooms: h.mgr.List(),
	})
}

// ── room.create ───────────────────────────────────────────────────────────────

func (h *Handler) handleRoomCreate(c *ws.Client, msg protocol.Msg) {
	var p protocol.RoomCreatePayload
	if err := protocol.DecodePayload(msg, &p); err != nil || p.Name == "" {
		sendError(c, "invalid room.create payload")
		return
	}
	r, err := h.mgr.Create(p.Name, p.Variant)
	if err != nil {
		sendError(c, err.Error())
		return
	}
	h.joinRoom(c, r)
}

// ── room.join ─────────────────────────────────────────────────────────────────

func (h *Handler) handleRoomJoin(c *ws.Client, msg protocol.Msg) {
	var p protocol.RoomJoinPayload
	if err := protocol.DecodePayload(msg, &p); err != nil || p.RoomID == "" {
		sendError(c, "invalid room.join payload")
		return
	}
	r, err := h.mgr.Get(p.RoomID)
	if err != nil {
		sendError(c, err.Error())
		return
	}
	h.joinRoom(c, r)
}

func (h *Handler) joinRoom(c *ws.Client, r *room.Room) {
	seat, rm, err := h.mgr.JoinRoom(c, r.ID)
	if err != nil {
		sendError(c, err.Error())
		return
	}
	c.Send(protocol.TypeRoomJoined, protocol.RoomJoinedPayload{
		Room:     rm.Info(),
		YourSeat: seat,
	})
	// Notify all other players.
	rm.Broadcast(protocol.TypeRoomUpdated, protocol.RoomUpdatedPayload{Room: rm.Info()})

	// Auto-start when room is full.
	if rm.IsFull() {
		if err := rm.StartGame(); err != nil {
			log.Printf("start game error: %v", err)
			return
		}
		log.Printf("game started in room %s", rm.ID)
		room.BroadcastGameState(rm.Game(), rm)
	}
}

// ── room.leave ────────────────────────────────────────────────────────────────

func (h *Handler) handleRoomLeave(c *ws.Client) {
	h.mgr.LeaveRoom(c.ID)
}

// ── room.start ────────────────────────────────────────────────────────────────

func (h *Handler) handleRoomStart(c *ws.Client) {
	r := h.mgr.RoomOf(c.ID)
	if r == nil {
		sendError(c, "not in a room")
		return
	}
	if r.Game() != nil {
		sendError(c, "game already started")
		return
	}
	broadcast := func() { room.BroadcastGameState(r.Game(), r) }
	if err := room.StartWithBots(r, broadcast); err != nil {
		log.Printf("room.start error: %v", err)
		sendError(c, err.Error())
	}
}

// ── game.bid ──────────────────────────────────────────────────────────────────

func (h *Handler) handleGameBid(c *ws.Client, msg protocol.Msg) {
	r, g, seat, ok := h.roomAndGame(c)
	if !ok {
		return
	}
	var p protocol.BidPayload
	if err := protocol.DecodePayload(msg, &p); err != nil {
		sendError(c, "invalid game.bid payload")
		return
	}
	bid := parseBid(p)
	if err := g.PlaceBid(seat, bid); err != nil {
		sendError(c, err.Error())
		return
	}
	r.Broadcast(protocol.TypeGameEvent, protocol.GameEventPayload{
		Event: "bid",
		Seat:  seat,
		Bid:   bidDTOPtr(bid),
	})
	room.BroadcastGameState(g, r)
}

// ── game.discard ──────────────────────────────────────────────────────────────

func (h *Handler) handleGameDiscard(c *ws.Client, msg protocol.Msg) {
	r, g, seat, ok := h.roomAndGame(c)
	if !ok {
		return
	}
	var p protocol.DiscardPayload
	if err := protocol.DecodePayload(msg, &p); err != nil {
		sendError(c, "invalid game.discard payload")
		return
	}

	// First pick up the kitty if not already done.
	if err := g.PickUpKitty(seat); err != nil {
		sendError(c, err.Error())
		return
	}
	cards := make([]game.Card, len(p.Cards))
	for i, dto := range p.Cards {
		cards[i] = game.Card{Rank: game.Rank(dto.Rank), Suit: game.Suit(dto.Suit)}
	}
	if err := g.Discard(seat, cards); err != nil {
		sendError(c, err.Error())
		return
	}
	room.BroadcastGameState(g, r)
}

// ── game.play ─────────────────────────────────────────────────────────────────

func (h *Handler) handleGamePlay(c *ws.Client, msg protocol.Msg) {
	r, g, seat, ok := h.roomAndGame(c)
	if !ok {
		return
	}
	var p protocol.PlayPayload
	if err := protocol.DecodePayload(msg, &p); err != nil {
		sendError(c, "invalid game.play payload")
		return
	}
	card := game.Card{Rank: game.Rank(p.Card.Rank), Suit: game.Suit(p.Card.Suit)}
	if err := g.PlayCard(seat, card); err != nil {
		sendError(c, err.Error())
		return
	}
	r.Broadcast(protocol.TypeGameEvent, protocol.GameEventPayload{
		Event: "play",
		Seat:  seat,
		Card:  &protocol.CardDTO{Rank: p.Card.Rank, Suit: p.Card.Suit},
	})
	room.BroadcastGameState(g, r)
	// If the game just ended, broadcast a game_over event.
	if g.Phase == game.PhaseEnd {
		r.Broadcast(protocol.TypeGameEvent, protocol.GameEventPayload{
			Event:   "game_over",
			Message: fmt.Sprintf("Partie terminée — Équipe A: %d  Équipe B: %d", g.Scores[0], g.Scores[1]),
		})
	}
}

// ── helpers ───────────────────────────────────────────────────────────────────

func (h *Handler) roomAndGame(c *ws.Client) (*room.Room, *game.Game, int, bool) {
	r := h.mgr.RoomOf(c.ID)
	if r == nil {
		sendError(c, "not in a room")
		return nil, nil, 0, false
	}
	g := r.Game()
	if g == nil {
		sendError(c, "game not started yet")
		return nil, nil, 0, false
	}
	seat := r.SeatOf(c.ID)
	if seat < 0 {
		sendError(c, "you are not seated")
		return nil, nil, 0, false
	}
	return r, g, seat, true
}

func sendError(c *ws.Client, msg string) {
	c.Send(protocol.TypeError, protocol.ErrorPayload{Message: msg})
}

func parseBid(p protocol.BidPayload) game.Bid {
	if p.Pass {
		return game.Bid{Pass: true}
	}
	suit := parseBidSuit(p.Suit)
	return game.Bid{Level: game.BidLevel(p.Level), Suit: suit}
}

func parseBidSuit(s string) game.BidSuit {
	switch s {
	case "S":
		return game.BidSpades
	case "C":
		return game.BidClubs
	case "D":
		return game.BidDiamonds
	case "H":
		return game.BidHearts
	case "NT":
		return game.BidNoTrumps
	case "Misere":
		return game.BidMisere
	case "OpenMisere":
		return game.BidOpenMisere
	}
	return game.BidSpades
}

func bidDTOPtr(b game.Bid) *protocol.BidDTO {
	dto := protocol.BidDTO{Pass: b.Pass, Level: int(b.Level), Suit: bidSuitStr(b.Suit)}
	return &dto
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
