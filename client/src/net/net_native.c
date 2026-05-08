/* net_native.c — WebSocket client using libwebsockets (native builds only)
 *
 * Design: LWS runs in its own thread so lws_service() never blocks the SDL
 * main loop (it can block for seconds on macOS kqueue due to internal LWS
 * timers overriding a timeout=0 request).
 *
 *  ┌─────────────┐       send queue (mutex)       ┌─────────────────┐
 *  │  Main thread│ ──── net_send_text() ────────→ │  LWS thread     │
 *  │  (SDL loop) │ ←─── net_poll() drains evts ── │  lws_service()  │
 *  └─────────────┘       event queue (mutex)       └─────────────────┘
 */
#ifndef __EMSCRIPTEN__

#include "net.h"

#include <libwebsockets.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Sizes ───────────────────────────────────────────────────────────────── */

#define SEND_QUEUE_CAP  32
#define RECV_QUEUE_CAP  64
#define MSG_MAX       16384   /* max message size (bytes) */

/* ── Send queue (main → LWS thread) ─────────────────────────────────────── */

typedef struct { char *data; int len; } SMsg;

static SMsg            s_sq[SEND_QUEUE_CAP];
static int             s_sq_head = 0, s_sq_tail = 0, s_sq_count = 0;
static pthread_mutex_t s_sq_mu   = PTHREAD_MUTEX_INITIALIZER;

/* ── Event queue (LWS thread → main) ────────────────────────────────────── */

typedef enum { NEVT_CONNECT, NEVT_MESSAGE, NEVT_DISCONNECT } NEvtType;
typedef struct { NEvtType type; char *data; int len; int code; } NEvt;

static NEvt            s_eq[RECV_QUEUE_CAP];
static int             s_eq_head = 0, s_eq_tail = 0, s_eq_count = 0;
static pthread_mutex_t s_eq_mu   = PTHREAD_MUTEX_INITIALIZER;

/* ── Shared LWS state (LWS thread only except s_status which is atomic) ─── */

static NetOnConnect    s_on_connect;
static NetOnMessage    s_on_message;
static NetOnDisconnect s_on_disconnect;

static struct lws_context *s_ctx    = NULL;
static struct lws          *s_wsi   = NULL;
static volatile int         s_status = NET_DISCONNECTED; /* atomic via volatile */

/* Write buffer: must be valid for the duration of lws_write */
static unsigned char s_wbuf[LWS_PRE + MSG_MAX];

/* ── Thread ──────────────────────────────────────────────────────────────── */

static pthread_t    s_thread;
static volatile int s_thread_run = 0;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void eq_push(NEvt evt)
{
    pthread_mutex_lock(&s_eq_mu);
    if (s_eq_count < RECV_QUEUE_CAP) {
        s_eq[s_eq_tail] = evt;
        s_eq_tail = (s_eq_tail + 1) % RECV_QUEUE_CAP;
        s_eq_count++;
    } else {
        /* Queue full: drop event, free heap data */
        free(evt.data);
        fprintf(stderr, "net: recv queue full, dropping event\n");
    }
    pthread_mutex_unlock(&s_eq_mu);
}

/* ── LWS callback (runs in LWS thread) ──────────────────────────────────── */

static int lws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len)
{
    (void)user;
    switch (reason) {

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        s_status = NET_CONNECTED;
        eq_push((NEvt){ NEVT_CONNECT, NULL, 0, 0 });
        lws_callback_on_writable(wsi); /* flush any queued sends */
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE: {
        if (len == 0) break;
        /* Copy fragment; for simplicity we only handle final fragments.
         * Game-state messages are well under MSG_MAX so fragmentation is
         * unlikely in practice. */
        if (!lws_is_final_fragment(wsi)) {
            fprintf(stderr, "net: fragmented message, skipping\n");
            break;
        }
        char *buf = malloc(len + 1);
        if (!buf) break;
        memcpy(buf, in, len);
        buf[len] = '\0';
        eq_push((NEvt){ NEVT_MESSAGE, buf, (int)len, 0 });
        break;
    }

    case LWS_CALLBACK_CLIENT_WRITEABLE: {
        pthread_mutex_lock(&s_sq_mu);
        if (s_sq_count > 0) {
            SMsg *m = &s_sq[s_sq_head];
            int   n  = m->len < MSG_MAX ? m->len : MSG_MAX;
            memcpy(s_wbuf + LWS_PRE, m->data, n);
            free(m->data);
            m->data = NULL;
            s_sq_head  = (s_sq_head + 1) % SEND_QUEUE_CAP;
            s_sq_count--;
            int more = s_sq_count;
            pthread_mutex_unlock(&s_sq_mu);

            lws_write(wsi, s_wbuf + LWS_PRE, (size_t)n, LWS_WRITE_TEXT);
            if (more > 0)
                lws_callback_on_writable(wsi);
        } else {
            pthread_mutex_unlock(&s_sq_mu);
        }
        break;
    }

    case LWS_CALLBACK_CLIENT_CLOSED:
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
        s_status = NET_DISCONNECTED;
        s_wsi    = NULL;
        const char *reason_str = (in && len > 0) ? (const char *)in : "closed";
        char *buf = strdup(reason_str);
        eq_push((NEvt){ NEVT_DISCONNECT, buf, 0, 0 });
        break;
    }

    default:
        break;
    }
    return 0;
}

static const struct lws_protocols s_protocols[] = {
    { "five-hundred", lws_callback, 0, MSG_MAX, 0, NULL, 0 },
    LWS_PROTOCOL_LIST_TERM
};

/* ── LWS background thread ───────────────────────────────────────────────── */

static void *lws_thread_func(void *arg)
{
    (void)arg;
    while (s_thread_run) {
        if (s_ctx) {
            lws_service(s_ctx, 50); /* blocks up to 50ms; lws_cancel_service wakes it */

            /* After every service cycle, schedule a write if sends are pending.
             * We're still inside the LWS thread here, so this call is safe. */
            pthread_mutex_lock(&s_sq_mu);
            int pending = (s_sq_count > 0);
            struct lws *wsi = s_wsi;
            pthread_mutex_unlock(&s_sq_mu);
            if (pending && wsi)
                lws_callback_on_writable(wsi);
        } else {
            usleep(10000); /* 10ms idle when no context */
        }
    }
    return NULL;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void net_init(NetOnConnect on_connect, NetOnMessage on_message,
              NetOnDisconnect on_disconnect)
{
    s_on_connect    = on_connect;
    s_on_message    = on_message;
    s_on_disconnect = on_disconnect;

    s_thread_run = 1;
    pthread_create(&s_thread, NULL, lws_thread_func, NULL);
}

bool net_connect(const char *host, int port, const char *path)
{
    /* Tear down any previous context (safe: LWS thread checks s_ctx) */
    if (s_ctx) {
        struct lws_context *old = s_ctx;
        s_ctx = NULL;
        s_wsi = NULL;
        lws_cancel_service(old);   /* wake the LWS thread so it exits lws_service */
        usleep(60000);             /* give the thread one service cycle to exit */
        lws_context_destroy(old);
    }
    /* Drain leftover queue entries */
    pthread_mutex_lock(&s_sq_mu);
    while (s_sq_count > 0) {
        free(s_sq[s_sq_head].data);
        s_sq_head  = (s_sq_head + 1) % SEND_QUEUE_CAP;
        s_sq_count--;
    }
    pthread_mutex_unlock(&s_sq_mu);

    struct lws_context_creation_info ctx_info;
    memset(&ctx_info, 0, sizeof(ctx_info));
    ctx_info.port      = CONTEXT_PORT_NO_LISTEN;
    ctx_info.protocols = s_protocols;
    ctx_info.options   = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    lws_set_log_level(LLL_ERR | LLL_WARN, NULL);

    s_ctx = lws_create_context(&ctx_info);
    if (!s_ctx) {
        fprintf(stderr, "net: lws_create_context failed\n");
        return false;
    }

    struct lws_client_connect_info info;
    memset(&info, 0, sizeof(info));
    info.context  = s_ctx;
    info.address  = host;
    info.port     = port;
    info.path     = path;
    info.host     = host;
    info.origin   = host;
    info.protocol = s_protocols[0].name;

    s_status = NET_CONNECTING;
    s_wsi    = lws_client_connect_via_info(&info);
    if (!s_wsi) {
        fprintf(stderr, "net: lws_client_connect_via_info failed\n");
        lws_context_destroy(s_ctx);
        s_ctx    = NULL;
        s_status = NET_DISCONNECTED;
        return false;
    }
    return true;
}

void net_send_text(const char *data, int len)
{
    if (s_status != NET_CONNECTED || !s_wsi) return;

    char *buf = malloc((size_t)len);
    if (!buf) return;
    memcpy(buf, data, len);

    pthread_mutex_lock(&s_sq_mu);
    if (s_sq_count < SEND_QUEUE_CAP) {
        s_sq[s_sq_tail] = (SMsg){ buf, len };
        s_sq_tail  = (s_sq_tail + 1) % SEND_QUEUE_CAP;
        s_sq_count++;
        pthread_mutex_unlock(&s_sq_mu);
        /* Wake the LWS thread — it will call lws_callback_on_writable itself
         * after lws_service returns, which is thread-safe. */
        if (s_ctx) lws_cancel_service(s_ctx);
    } else {
        pthread_mutex_unlock(&s_sq_mu);
        free(buf);
        fprintf(stderr, "net: send queue full\n");
    }
}

/* Drain the event queue and fire user callbacks. Call from main thread. */
void net_poll(void)
{
    for (;;) {
        pthread_mutex_lock(&s_eq_mu);
        if (s_eq_count == 0) {
            pthread_mutex_unlock(&s_eq_mu);
            break;
        }
        NEvt evt = s_eq[s_eq_head];
        s_eq_head  = (s_eq_head + 1) % RECV_QUEUE_CAP;
        s_eq_count--;
        pthread_mutex_unlock(&s_eq_mu);

        switch (evt.type) {
        case NEVT_CONNECT:
            if (s_on_connect) s_on_connect();
            break;
        case NEVT_MESSAGE:
            if (s_on_message) s_on_message(evt.data, evt.len);
            free(evt.data);
            break;
        case NEVT_DISCONNECT:
            if (s_on_disconnect) s_on_disconnect(evt.code, evt.data ? evt.data : "closed");
            free(evt.data);
            break;
        }
    }
}

void net_close(void)
{
    if (s_ctx) {
        struct lws_context *old = s_ctx;
        s_ctx = NULL;
        s_wsi = NULL;
        lws_cancel_service(old);
        usleep(60000);
        lws_context_destroy(old);
    }
    s_status = NET_DISCONNECTED;
}

void net_shutdown(void)
{
    net_close();
    s_thread_run = 0;
    /* lws_cancel_service is a no-op when s_ctx is NULL;
     * the thread will wake on its next usleep cycle. */
    pthread_join(s_thread, NULL);
}

NetStatus net_get_status(void) { return (NetStatus)s_status; }

#endif /* !__EMSCRIPTEN__ */
