package room

import (
	"errors"
	"fmt"
	"math/rand"
	"sync"
	"time"

	"github.com/remi/5_hundred/internal/game"
	"github.com/remi/5_hundred/internal/protocol"
	"github.com/remi/5_hundred/internal/ws"
)

// ── Room ──────────────────────────────────────────────────────────────────────

type Room struct {
	ID      string
	Name    string
	Variant game.Variant

	mu      sync.Mutex
	seats   [4]*ws.Client // seats[i] = client in seat i, nil if empty
	maxSeat int           // 2 or 4
	game    *game.Game    // non-nil once the game has started
	rng     *rand.Rand
}

func newRoom(id, name string, v game.Variant) *Room {
	maxSeat := 4
	if v == game.TwoPlayer {
		maxSeat = 2
	}
	return &Room{
		ID:      id,
		Name:    name,
		Variant: v,
		maxSeat: maxSeat,
		rng:     rand.New(rand.NewSource(time.Now().UnixNano())),
	}
}

// Join assigns the client to the first free seat. Returns seat index or error.
func (r *Room) Join(c *ws.Client) (int, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	for i := 0; i < r.maxSeat; i++ {
		if r.seats[i] == nil {
			r.seats[i] = c
			return i, nil
		}
	}
	return -1, errors.New("room is full")
}

// Leave removes the client from their seat.
func (r *Room) Leave(clientID string) {
	r.mu.Lock()
	defer r.mu.Unlock()
	for i := range r.seats {
		if r.seats[i] != nil && r.seats[i].ID == clientID {
			r.seats[i] = nil
			return
		}
	}
}

// SeatOf returns the seat index for clientID, or -1 if not found.
func (r *Room) SeatOf(clientID string) int {
	r.mu.Lock()
	defer r.mu.Unlock()
	for i, s := range r.seats {
		if s != nil && s.ID == clientID {
			return i
		}
	}
	return -1
}

// IsFull returns true when all seats are taken.
func (r *Room) IsFull() bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	for i := 0; i < r.maxSeat; i++ {
		if r.seats[i] == nil {
			return false
		}
	}
	return true
}

// Info returns a snapshot for the protocol.
func (r *Room) Info() protocol.RoomInfo {
	r.mu.Lock()
	defer r.mu.Unlock()
	players := make([]string, 0, r.maxSeat)
	for i := 0; i < r.maxSeat; i++ {
		if r.seats[i] != nil {
			players = append(players, r.seats[i].Name)
		}
	}
	variant := "4p"
	if r.Variant == game.TwoPlayer {
		variant = "2p"
	}
	return protocol.RoomInfo{
		ID:       r.ID,
		Name:     r.Name,
		Variant:  variant,
		Players:  players,
		MaxSeats: r.maxSeat,
	}
}

// Broadcast sends a message to all seated players.
func (r *Room) Broadcast(msgType string, payload any) {
	r.mu.Lock()
	clients := make([]*ws.Client, 0, r.maxSeat)
	for i := 0; i < r.maxSeat; i++ {
		if r.seats[i] != nil {
			clients = append(clients, r.seats[i])
		}
	}
	r.mu.Unlock()
	for _, c := range clients {
		c.Send(msgType, payload)
	}
}

// StartGame creates a new Game if not already started and all seats are filled.
func (r *Room) StartGame() error {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.game != nil {
		return errors.New("game already started")
	}
	for i := 0; i < r.maxSeat; i++ {
		if r.seats[i] == nil {
			return fmt.Errorf("seat %d is empty", i)
		}
	}
	r.game = game.New(r.Variant, r.rng)
	return nil
}

// Game returns the current game or nil.
func (r *Room) Game() *game.Game {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.game
}

// ── Manager ───────────────────────────────────────────────────────────────────

// Manager holds all rooms and routes game actions.
type Manager struct {
	mu       sync.RWMutex
	rooms    map[string]*Room
	// clientRoom maps client ID → room ID
	clientRoom map[string]string
}

// NewManager creates an empty Manager.
func NewManager() *Manager {
	return &Manager{
		rooms:      make(map[string]*Room),
		clientRoom: make(map[string]string),
	}
}

func genID() string {
	return fmt.Sprintf("%06x", rand.Int63n(0xffffff))
}

// Create creates a new room and returns it.
func (m *Manager) Create(name, variant string) (*Room, error) {
	v := game.FourPlayer
	if variant == "2p" {
		v = game.TwoPlayer
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	id := genID()
	r := newRoom(id, name, v)
	m.rooms[id] = r
	return r, nil
}

// Get returns the room with the given ID or an error.
func (m *Manager) Get(id string) (*Room, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	r, ok := m.rooms[id]
	if !ok {
		return nil, fmt.Errorf("room %s not found", id)
	}
	return r, nil
}

// List returns a snapshot of all rooms.
func (m *Manager) List() []protocol.RoomInfo {
	m.mu.RLock()
	defer m.mu.RUnlock()
	out := make([]protocol.RoomInfo, 0, len(m.rooms))
	for _, r := range m.rooms {
		out = append(out, r.Info())
	}
	return out
}

// JoinRoom seats a client in the given room.
func (m *Manager) JoinRoom(c *ws.Client, roomID string) (int, *Room, error) {
	r, err := m.Get(roomID)
	if err != nil {
		return -1, nil, err
	}
	seat, err := r.Join(c)
	if err != nil {
		return -1, nil, err
	}
	m.mu.Lock()
	m.clientRoom[c.ID] = roomID
	m.mu.Unlock()
	return seat, r, nil
}

// LeaveRoom removes a client from whatever room they're in.
func (m *Manager) LeaveRoom(clientID string) {
	m.mu.Lock()
	roomID, ok := m.clientRoom[clientID]
	delete(m.clientRoom, clientID)
	m.mu.Unlock()
	if !ok {
		return
	}
	r, err := m.Get(roomID)
	if err != nil {
		return
	}
	r.Leave(clientID)
	r.Broadcast(protocol.TypeRoomUpdated, protocol.RoomUpdatedPayload{Room: r.Info()})
}

// RoomOf returns the room for a client, or nil.
func (m *Manager) RoomOf(clientID string) *Room {
	m.mu.RLock()
	roomID, ok := m.clientRoom[clientID]
	m.mu.RUnlock()
	if !ok {
		return nil
	}
	r, _ := m.Get(roomID)
	return r
}
