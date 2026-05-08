/* net_wasm.c — WebSocket client using Emscripten (WASM builds only) */
#include "net.h"

#include <emscripten/websocket.h>
#include <stdio.h>
#include <string.h>

static NetOnConnect    s_on_connect;
static NetOnMessage    s_on_message;
static NetOnDisconnect s_on_disconnect;

static EMSCRIPTEN_WEBSOCKET_T s_socket = 0;
static NetStatus               s_status = NET_DISCONNECTED;

/* ── Emscripten callbacks ─────────────────────────────────────────────────── */

static EM_BOOL cb_open(int event_type,
                       const EmscriptenWebSocketOpenEvent *e, void *ud)
{
    (void)event_type; (void)e; (void)ud;
    s_status = NET_CONNECTED;
    if (s_on_connect) s_on_connect();
    return EM_TRUE;
}

static EM_BOOL cb_message(int event_type,
                          const EmscriptenWebSocketMessageEvent *e, void *ud)
{
    (void)event_type; (void)ud;
    if (!e->isText) return EM_TRUE;
    /* numBytes includes the null terminator for text frames */
    int len = (int)e->numBytes > 0 ? (int)e->numBytes - 1 : 0;
    if (s_on_message) s_on_message((const char *)e->data, len);
    return EM_TRUE;
}

static EM_BOOL cb_error(int event_type,
                        const EmscriptenWebSocketErrorEvent *e, void *ud)
{
    (void)event_type; (void)e; (void)ud;
    s_status = NET_DISCONNECTED;
    if (s_on_disconnect) s_on_disconnect(-1, "websocket error");
    return EM_TRUE;
}

static EM_BOOL cb_close(int event_type,
                        const EmscriptenWebSocketCloseEvent *e, void *ud)
{
    (void)event_type; (void)ud;
    s_status = NET_DISCONNECTED;
    if (s_on_disconnect) s_on_disconnect((int)e->code, e->reason);
    return EM_TRUE;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void net_init(NetOnConnect on_connect, NetOnMessage on_message,
              NetOnDisconnect on_disconnect)
{
    s_on_connect    = on_connect;
    s_on_message    = on_message;
    s_on_disconnect = on_disconnect;
}

bool net_connect(const char *host, int port, const char *path)
{
    if (s_socket > 0) {
        emscripten_websocket_close(s_socket, 1000, "reconnect");
        emscripten_websocket_delete(s_socket);
        s_socket = 0;
    }

    char url[256];
    snprintf(url, sizeof(url), "ws://%s:%d%s", host, port, path);

    EmscriptenWebSocketCreateAttributes attr;
    attr.url                   = url;
    attr.protocols             = "five-hundred";
    attr.createOnMainThread    = EM_TRUE;

    s_socket = emscripten_websocket_new(&attr);
    if (s_socket <= 0) {
        fprintf(stderr, "net_wasm: emscripten_websocket_new failed\n");
        return false;
    }

    emscripten_websocket_set_onopen_callback(s_socket,    NULL, cb_open);
    emscripten_websocket_set_onmessage_callback(s_socket, NULL, cb_message);
    emscripten_websocket_set_onerror_callback(s_socket,   NULL, cb_error);
    emscripten_websocket_set_onclose_callback(s_socket,   NULL, cb_close);

    s_status = NET_CONNECTING;
    return true;
}

void net_send_text(const char *data, int len)
{
    (void)len; /* Emscripten takes null-terminated strings */
    if (s_status != NET_CONNECTED || s_socket <= 0) return;
    emscripten_websocket_send_utf8_text(s_socket, data);
}

/* No-op: the browser event loop fires the callbacks asynchronously. */
void net_poll(void) { }

void net_close(void)
{
    if (s_socket > 0) {
        emscripten_websocket_close(s_socket, 1000, "bye");
        emscripten_websocket_delete(s_socket);
        s_socket = 0;
    }
    s_status = NET_DISCONNECTED;
}

/* No background thread in WASM — shutdown is a no-op. */
void net_shutdown(void) { net_close(); }

NetStatus net_get_status(void) { return s_status; }
