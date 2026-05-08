package ws

// NewBotClient creates a pseudo-Client for a bot player.
// Its send channel is drained silently; it has no real WebSocket connection.
// The returned Client can be placed in a room seat just like a real client.
func NewBotClient(id, name string) *Client {
	c := &Client{
		ID:   id,
		Name: name,
		send: make(chan []byte, 16),
		done: make(chan struct{}),
	}
	// Drain outgoing messages — bots don't need them.
	go func() {
		for range c.send {
		}
	}()
	return c
}

// IsBot returns true if c is a bot pseudo-client (no real conn).
func (c *Client) IsBot() bool { return c.conn == nil }

// BotClose stops the drain goroutine cleanly.
func (c *Client) BotClose() {
	c.once.Do(func() { close(c.done); close(c.send) })
}
