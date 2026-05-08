#pragma once
#include <stdbool.h>

/* ── Status ──────────────────────────────────────────────────────────────── */

typedef enum {
    NET_DISCONNECTED = 0,
    NET_CONNECTING,
    NET_CONNECTED,
} NetStatus;

/* ── Callbacks ───────────────────────────────────────────────────────────── */

typedef void (*NetOnConnect)(void);
typedef void (*NetOnMessage)(const char *data, int len);
typedef void (*NetOnDisconnect)(int code, const char *reason);

/* ── API ─────────────────────────────────────────────────────────────────── */

/* Initialise the network layer and register callbacks.  Call once at startup.*/
void      net_init(NetOnConnect on_connect, NetOnMessage on_message,
                   NetOnDisconnect on_disconnect);

/* Start an async connection to ws://host:port/path.
 * Returns true if the attempt was initiated.
 * The on_connect callback fires when the WebSocket handshake succeeds. */
bool      net_connect(const char *host, int port, const char *path);

/* Queue a UTF-8 text frame for delivery.  May be called any time. */
void      net_send_text(const char *data, int len);

/* Service the connection (non-blocking).  Must be called every frame. */
void      net_poll(void);

/* Gracefully close the connection (keeps the background thread alive). */
void      net_close(void);

/* Shutdown the entire network layer (stops the background thread).
 * Call once at program exit, after net_close(). */
void      net_shutdown(void);

/* Current connection status. */
NetStatus net_get_status(void);
