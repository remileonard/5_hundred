package main

import (
	"context"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"

	"nhooyr.io/websocket"

	"github.com/remi/5_hundred/internal/handler"
	"github.com/remi/5_hundred/internal/room"
	"github.com/remi/5_hundred/internal/ws"
)

func main() {
	mgr := room.NewManager()
	hub := ws.NewHub(handler.NewHandler(mgr).Handle)

	mux := http.NewServeMux()

	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		fmt.Fprintf(w, `{"status":"ok","clients":%d}`, hub.ClientCount())
	})

	mux.HandleFunc("/ws", func(w http.ResponseWriter, r *http.Request) {
		conn, err := websocket.Accept(w, r, &websocket.AcceptOptions{
			InsecureSkipVerify: true, // allow any Origin (dev only)
		})
		if err != nil {
			log.Printf("ws accept error: %v", err)
			return
		}
		id := fmt.Sprintf("%s-%s", r.RemoteAddr, r.Header.Get("Sec-Websocket-Key"))
		hub.RegisterConn(r.Context(), id, conn)
	})

	addr := ":8080"
	if p := os.Getenv("PORT"); p != "" {
		addr = ":" + p
	}

	srv := &http.Server{Addr: addr, Handler: mux}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	go hub.Run(ctx)

	go func() {
		log.Printf("5 Hundred server listening on %s", addr)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatal(err)
		}
	}()

	<-ctx.Done()
	log.Println("Shutting down…")
	srv.Shutdown(context.Background())
}
