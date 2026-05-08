package protocol

import "encoding/json"

// ── Message envelope ─────────────────────────────────────────────────────────

// Msg is the top-level WebSocket message.
// Every message sent between client and server has this shape:
//
//	{"type": "room.list", "payload": {...}}
type Msg struct {
	Type    string          `json:"type"`
	Payload json.RawMessage `json:"payload,omitempty"`
}

// ── Client → Server message types ────────────────────────────────────────────

const (
	// Identification
	TypeIdentify = "identify" // payload: IdentifyPayload

	// Room management
	TypeRoomCreate = "room.create" // payload: RoomCreatePayload
	TypeRoomJoin   = "room.join"   // payload: RoomJoinPayload
	TypeRoomLeave  = "room.leave"
	TypeRoomList   = "room.list"

	// Game actions (used from phase 5 onwards)
	TypeGameBid     = "game.bid"     // payload: BidPayload
	TypeGameDiscard = "game.discard" // payload: DiscardPayload
	TypeGamePlay    = "game.play"    // payload: PlayPayload
	TypeRoomStart   = "room.start"   // payload: none; fills bots + starts game
)

// ── Server → Client message types ────────────────────────────────────────────

const (
	TypeWelcome     = "welcome"      // payload: WelcomePayload
	TypeError       = "error"        // payload: ErrorPayload
	TypeRoomListed  = "room.listed"  // payload: RoomListedPayload
	TypeRoomJoined  = "room.joined"  // payload: RoomJoinedPayload
	TypeRoomUpdated = "room.updated" // payload: RoomUpdatedPayload
	TypeGameState   = "game.state"   // payload: GameStatePayload
	TypeGameEvent   = "game.event"   // payload: GameEventPayload
)

// ── Payload types ─────────────────────────────────────────────────────────────

type IdentifyPayload struct {
	Name string `json:"name"`
}

type WelcomePayload struct {
	PlayerID string `json:"player_id"`
	Name     string `json:"name"`
}

type ErrorPayload struct {
	Message string `json:"message"`
}

type RoomCreatePayload struct {
	Name    string `json:"name"`
	Variant string `json:"variant"` // "4p" or "2p"
}

type RoomJoinPayload struct {
	RoomID string `json:"room_id"`
}

type RoomInfo struct {
	ID      string   `json:"id"`
	Name    string   `json:"name"`
	Variant string   `json:"variant"`
	Players []string `json:"players"` // player names
	MaxSeats int     `json:"max_seats"`
}

type RoomListedPayload struct {
	Rooms []RoomInfo `json:"rooms"`
}

type RoomJoinedPayload struct {
	Room     RoomInfo `json:"room"`
	YourSeat int      `json:"your_seat"`
}

type RoomUpdatedPayload struct {
	Room RoomInfo `json:"room"`
}

// ── Game state payload ────────────────────────────────────────────────────────

type CardDTO struct {
	Rank int `json:"rank"` // 0=Joker, 4-14
	Suit int `json:"suit"` // 0=♠ 1=♣ 2=♦ 3=♥ 4=none
}

type PlayerDTO struct {
	Name         string    `json:"name"`
	HandCount    int       `json:"hand_count"`    // cards in hand (opponents: count only)
	Hand         []CardDTO `json:"hand,omitempty"` // only sent to the owning player
	Tableau      []CardDTO `json:"tableau,omitempty"`
	TableauCount int       `json:"tableau_count"`
	TricksWon    int       `json:"tricks_won"`
}

type BidDTO struct {
	Pass  bool   `json:"pass"`
	Level int    `json:"level,omitempty"`
	Suit  string `json:"suit,omitempty"` // "S","C","D","H","NT","Misere","OpenMisere"
}

type GameStatePayload struct {
	Phase       string      `json:"phase"` // "bidding","kitty","playing","scoring","end"
	Players     []PlayerDTO `json:"players"`
	Kitty       []CardDTO   `json:"kitty,omitempty"` // only during kitty phase for contractor
	Contract    *BidDTO     `json:"contract,omitempty"`
	Contractor  int         `json:"contractor"`
	Trump       string      `json:"trump,omitempty"` // suit string or ""
	ToAct       int         `json:"to_act"`
	Trick       []CardDTO   `json:"trick"`
	TrickLeader int         `json:"trick_leader"`
	TrickCount  int         `json:"trick_count"`
	Scores      [2]int      `json:"scores"`
	Bids        []BidDTO    `json:"bids,omitempty"`
	// 2-player only: which source is currently active within the combined trick.
	// 0 = private hand, 1 = tableau (open hand).
	// When len(trick)==0 (leader's first play), both sources are available
	// regardless of this value.
	TwoPlayerHandType int `json:"two_player_hand_type"`
	// LastTrick / LastTrickLeader: cards of the most recently completed trick
	// (4 cards in 2-player combined trick, 4 cards in 4-player trick).
	// Sent so clients can display the completed trick between turns, i.e. when
	// len(trick)==0 and no new card has been played yet.
	LastTrick       []CardDTO `json:"last_trick,omitempty"`
	LastTrickLeader int       `json:"last_trick_leader,omitempty"`
}

type GameEventPayload struct {
	Event   string `json:"event"`            // "bid","play","trick_won","game_over"…
	Seat    int    `json:"seat,omitempty"`
	Card    *CardDTO `json:"card,omitempty"`
	Bid     *BidDTO  `json:"bid,omitempty"`
	Winner  int    `json:"winner,omitempty"`
	Message string `json:"message,omitempty"`
}

type BidPayload struct {
	Pass  bool   `json:"pass"`
	Level int    `json:"level,omitempty"`
	Suit  string `json:"suit,omitempty"`
}

type DiscardPayload struct {
	Cards []CardDTO `json:"cards"`
}

type PlayPayload struct {
	Card CardDTO `json:"card"`
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// Encode marshals a Msg to JSON bytes (panics only on programmer error).
func Encode(msgType string, payload any) ([]byte, error) {
	raw, err := json.Marshal(payload)
	if err != nil {
		return nil, err
	}
	return json.Marshal(Msg{Type: msgType, Payload: raw})
}

// Decode parses raw JSON into a Msg.
func Decode(data []byte) (Msg, error) {
	var m Msg
	return m, json.Unmarshal(data, &m)
}

// DecodePayload unmarshals msg.Payload into v.
func DecodePayload(msg Msg, v any) error {
	return json.Unmarshal(msg.Payload, v)
}
