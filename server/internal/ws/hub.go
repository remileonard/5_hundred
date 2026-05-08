package ws

import (
	"context"
	"encoding/json"
	"log"
	"sync"
	"time"

	"nhooyr.io/websocket"

	"github.com/remi/5_hundred/internal/protocol"
)

const (
	writeTimeout = 10 * time.Second
	pingInterval = 25 * time.Second
	pongTimeout  = 35 * time.Second
)

// Client represents a single connected WebSocket peer.
type Client struct {
	ID   string
	Name string

	conn *websocket.Conn
	hub  *Hub
	send chan []byte

	once sync.Once
	done chan struct{}
}

func newClient(id string, conn *websocket.Conn, hub *Hub) *Client {
	return &Client{
		ID:   id,
		conn: conn,
		hub:  hub,
		send: make(chan []byte, 64),
		done: make(chan struct{}),
	}
}

// Send queues a message for delivery. Non-blocking; drops if queue full.
func (c *Client) Send(msgType string, payload any) {
	data, err := protocol.Encode(msgType, payload)
	if err != nil {
		log.Printf("client %s encode error: %v", c.ID, err)
		return
	}
	select {
	case c.send <- data:
	default:
		log.Printf("client %s send buffer full, dropping message", c.ID)
	}
}

// Close disconnects this client.
func (c *Client) Close() {
	c.once.Do(func() {
		close(c.done)
		c.conn.Close(websocket.StatusGoingAway, "bye")
	})
}

// readPump reads messages from the WebSocket and forwards them to the Hub.
func (c *Client) readPump(ctx context.Context) {
	defer func() {
		c.hub.unregister <- c
		c.Close()
	}()

	for {
		ctx2, cancel := context.WithTimeout(ctx, pongTimeout)
		_, data, err := c.conn.Read(ctx2)
		cancel()
		if err != nil {
			if ctx.Err() == nil {
				log.Printf("client %s read error: %v", c.ID, err)
			}
			return
		}

		msg, err := protocol.Decode(data)
		if err != nil {
			log.Printf("client %s bad message: %v", c.ID, err)
			c.Send(protocol.TypeError, protocol.ErrorPayload{Message: "invalid JSON"})
			continue
		}

		c.hub.incoming <- IncomingMsg{Client: c, Msg: msg}
	}
}

// writePump drains the send channel onto the WebSocket.
func (c *Client) writePump(ctx context.Context) {
	ticker := time.NewTicker(pingInterval)
	defer ticker.Stop()

	for {
		select {
		case <-c.done:
			return
		case data, ok := <-c.send:
			if !ok {
				return
			}
			wctx, cancel := context.WithTimeout(ctx, writeTimeout)
			err := c.conn.Write(wctx, websocket.MessageText, data)
			cancel()
			if err != nil {
				log.Printf("client %s write error: %v", c.ID, err)
				return
			}
		case <-ticker.C:
			// nhooyr/websocket handles ping/pong internally — just write a
			// keep-alive ping frame.
			wctx, cancel := context.WithTimeout(ctx, writeTimeout)
			err := c.conn.Ping(wctx)
			cancel()
			if err != nil {
				log.Printf("client %s ping error: %v", c.ID, err)
				return
			}
		}
	}
}

// ── IncomingMsg ───────────────────────────────────────────────────────────────

// IncomingMsg pairs a decoded message with its sender.
type IncomingMsg struct {
	Client *Client
	Msg    protocol.Msg
}

// ── Hub ───────────────────────────────────────────────────────────────────────

// MsgHandler is called by the Hub for every incoming message.
type MsgHandler func(c *Client, msg protocol.Msg)

// Hub manages the set of active clients and dispatches messages.
type Hub struct {
	clients    map[string]*Client
	mu         sync.RWMutex
	register   chan *Client
	unregister chan *Client
	incoming   chan IncomingMsg
	handler    MsgHandler
}

// NewHub creates a Hub. Call Run in a goroutine.
func NewHub(handler MsgHandler) *Hub {
	return &Hub{
		clients:    make(map[string]*Client),
		register:   make(chan *Client, 16),
		unregister: make(chan *Client, 16),
		incoming:   make(chan IncomingMsg, 256),
		handler:    handler,
	}
}

// Run processes hub events. Blocks until ctx is cancelled.
func (h *Hub) Run(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case c := <-h.register:
			h.mu.Lock()
			h.clients[c.ID] = c
			h.mu.Unlock()
			log.Printf("hub: client %s connected (%s)", c.ID, c.Name)
		case c := <-h.unregister:
			h.mu.Lock()
			if _, ok := h.clients[c.ID]; ok {
				delete(h.clients, c.ID)
				log.Printf("hub: client %s disconnected", c.ID)
			}
			h.mu.Unlock()
		case im := <-h.incoming:
			h.handler(im.Client, im.Msg)
		}
	}
}

// RegisterConn upgrades an HTTP connection to WebSocket, registers the client,
// and starts its read/write pumps. Blocks until the connection closes.
func (h *Hub) RegisterConn(ctx context.Context, id string, conn *websocket.Conn) {
	c := newClient(id, conn, h)
	h.register <- c
	go c.writePump(ctx)
	c.readPump(ctx) // blocks
}

// Get returns the client with the given ID, or nil.
func (h *Hub) Get(id string) *Client {
	h.mu.RLock()
	defer h.mu.RUnlock()
	return h.clients[id]
}

// Broadcast sends a message to all connected clients.
func (h *Hub) Broadcast(msgType string, payload any) {
	data, err := protocol.Encode(msgType, payload)
	if err != nil {
		return
	}
	h.mu.RLock()
	defer h.mu.RUnlock()
	for _, c := range h.clients {
		select {
		case c.send <- append([]byte(nil), data...):
		default:
		}
	}
}

// ClientCount returns the number of connected clients.
func (h *Hub) ClientCount() int {
	h.mu.RLock()
	defer h.mu.RUnlock()
	return len(h.clients)
}

// ── JSON helpers ──────────────────────────────────────────────────────────────

// UnmarshalPayload is a convenience wrapper used by handlers.
func UnmarshalPayload(msg protocol.Msg, v any) error {
	return json.Unmarshal(msg.Payload, v)
}
