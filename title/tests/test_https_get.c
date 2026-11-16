/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright 2026 XxXJoker13XxX and contributors */
/* Host test for the title's blur_host_https_get (wine64_main.c, cut verbatim
 * into https_cut.inc by run_title_tests.sh) against fake sysmodule, dlsym and
 * sceHttp functions and a fake clock. Modes: (none), initfail, nomodule.
 * Audit finding 8: 150 s total, resume of a .part (206 with a matching
 * Content-Range appends; 200 starts again; any other range refuses), the
 * reachability check first (no connection fails fast), and distinct results
 * (no connection, interrupted with the .part kept, server error, certificate
 * not accepted, resume mismatch). */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define PS5LOG_LOG(...) (printf("  log: " __VA_ARGS__), printf("\n"))
static uint64_t fake_ns = 1000;
static uint64_t now_ns(void) { return fake_ns; }
/* the title's neighbours for the reachability check, faked */
typedef struct BlurNetAddr { uint8_t length, family; uint16_t port; uint32_t address; uint8_t zero[8]; } BlurNetAddr;
static int resolve_rc, connect_rc, reach_calls, sockets_live, last_port;
static int blur_resolve_ipv4_wait(const char *host, uint32_t *addr, int wait_ms, uint32_t timeout_us)
{
    assert(!strcmp(host, "cs.amax-emu.com") && wait_ms <= 5000 && timeout_us <= 5000000u);
    reach_calls++;
    *addr = 0x0100007f;
    return resolve_rc;
}
static int ps5log_ps5_socket(int d, int t, int p) { assert(d == AF_INET && t == SOCK_STREAM && !p); sockets_live++; return 9; }
static int ps5log_ps5_setsockopt(int s, int l, int o, const void *v, int n)
{ assert(s == 9 && l == 0xffff && o == 0x1109 && n == 4 && *(const uint32_t *)v <= 5000000u); return 0; }
static int ps5log_ps5_connect(int s, const struct sockaddr *a, int n)
{ assert(s == 9 && n == (int)sizeof(BlurNetAddr)); const BlurNetAddr *b = (const BlurNetAddr *)a; last_port = ((b->port & 0xff) << 8) | (b->port >> 8); return connect_rc; }
static int ps5log_ps5_close(int s) { assert(s == 9); sockets_live--; return 0; }
static int pools, pools_live;
int sceNetPoolCreate(const char *n, int size, int flags) { (void)n; (void)flags; assert(size == 64 << 10); pools++; pools_live++; return 7; }
void sceNetPoolDestroy(int id) { assert(id == 7); pools_live--; }

/* fake server */
static uint64_t read_delay_ns, stall_ns; static long long stall_at = -1;
static int fail_modules, fail_http_init, status_code = 200, send_rc, has_len = 0, read_fail_at = -1;
static long long body_len, announce_len;
static long long served;
static int honor_range, ranged, ssl_err_rc = -1, ssl_err, cr_override_set; static uint32_t ssl_detail;
static long long range_from = -1; static char cr_override[128], range_header[64];
static int live_tmpl, live_conn, live_req, init_calls, ssl_inits, ssl_live, http_inits, timeouts_set;
static int f_net_init(void) { init_calls++; return 0; }
static int f_ssl_init(size_t p) { assert(p == 512 << 10); ssl_inits++; ssl_live++; return 3; }
static int f_ssl_term(int id) { assert(id == 3); ssl_live--; return 0; }
static int f_http_init(int net, int ssl, size_t p) { assert(net == 7 && ssl == 3 && p == 512 << 10); http_inits++; return fail_http_init ? (int)0x80431001 : 5; }
static int f_http_term(int id) { (void)id; return 0; }
static int f_tmpl(int h, const char *a, int v, int ap) { assert(h == 5 && a && v == 2 && ap == 1); live_tmpl++; return 0x10020001; }
static int f_conn(int t, const char *u, int k) { assert(t == 0x10020001 && u && k == 1); live_conn++; return 0x20020001; }
static int f_req(int c, int m, const char *u, uint64_t l) { assert(c == 0x20020001 && m == 0 && u && l == 0); live_req++; served = 0; range_from = -1; range_header[0] = 0; return 0x30020001; }
static int f_add_header(int id, const char *n, const char *v, int mode)
{
    assert(id == 0x30020001 && !strcmp(n, "Range") && mode == 0);
    snprintf(range_header, sizeof range_header, "%s", v);
    assert(!strncmp(v, "bytes=", 6) && v[strlen(v) - 1] == '-');
    range_from = atoll(v + 6);
    if (honor_range) { served = range_from; ranged = 1; }
    return 0;
}
static int f_all_headers(int r, char **h, size_t *n)
{
    static char block[256];
    (void)r;
    if (cr_override_set) snprintf(block, sizeof block, "HTTP/1.1 206 Partial Content\r\n%s\r\n\r\n", cr_override);
    else snprintf(block, sizeof block, "HTTP/1.1 206 Partial Content\r\ncontent-range: bytes %lld-%lld/%lld\r\n\r\n",
                  range_from, body_len - 1, body_len);
    *h = block; *n = strlen(block); return 0;
}
static int f_ssl_error(int r, int *e, uint32_t *d) { (void)r; *e = ssl_err; *d = ssl_detail; return ssl_err_rc; }
static int f_send(int r, const void *d, size_t s) { assert(r == 0x30020001 && !d && !s); return send_rc; }
static int f_status(int r, int *c) { (void)r; *c = honor_range && range_from >= 0 && status_code == 200 ? 206 : status_code; return 0; }
static int f_len(int r, int *res, uint64_t *l) { (void)r; *res = has_len ? 0 : 1; *l = (uint64_t)(announce_len - (honor_range && range_from > 0 ? range_from : 0)); return 0; }
static int f_read(int r, void *d, uint32_t s) {
    (void)r;
    fake_ns += read_delay_ns;
    if (stall_at >= 0 && served >= stall_at) { fake_ns += stall_ns; stall_at = -1; }
    if (read_fail_at >= 0 && served >= read_fail_at) return (int)0x80431068;
    long long n = body_len - served; if (n > s) n = s; if (n > 1000) n = 1000;
    for (long long i = 0; i < n; i++) ((uint8_t *)d)[i] = (uint8_t)(served + i);
    served += n; return (int)n;
}
static int f_timeout(int id, uint32_t us) { assert(id == 0x10020001 && us == 30000000u); timeouts_set++; return 0; }
static int redirect_off;
static int f_redirect(int id, int e) { assert(id == 0x10020001); redirect_off += e == 0; return 0; }
static int f_dreq(int r) { assert(r == 0x30020001); live_req--; return 0; }
static int f_dconn(int c) { assert(c == 0x20020001); live_conn--; return 0; }
static int f_dtmpl(int t) { assert(t == 0x10020001); live_tmpl--; return 0; }

static int loads;
int sceSysmoduleLoadModuleInternal(unsigned int id, ...) { (void)id; loads++; return 0; }
int sceSysmoduleGetModuleHandleInternal(uint32_t id, int32_t *h) {
    if (fail_modules) return (int)0x805A1000;
    *h = id == 0x8000001Cu ? 22 : id == 0x8000000Bu ? 58 : id == 0x8000000Au ? 56 : -1; return *h < 0 ? -1 : 0;
}
int sceKernelDlsym(int32_t h, const char *n, void **a) {
    static const struct { int h; const char *n; void *f; } t[] = {
        {22, "sceNetInit", (void *)f_net_init}, {58, "sceSslInit", (void *)f_ssl_init}, {58, "sceSslTerm", (void *)f_ssl_term},
        {56, "sceHttpInit", (void *)f_http_init}, {56, "sceHttpTerm", (void *)f_http_term},
        {56, "sceHttpCreateTemplate", (void *)f_tmpl}, {56, "sceHttpCreateConnectionWithURL", (void *)f_conn},
        {56, "sceHttpCreateRequestWithURL", (void *)f_req}, {56, "sceHttpSendRequest", (void *)f_send},
        {56, "sceHttpGetStatusCode", (void *)f_status}, {56, "sceHttpGetResponseContentLength", (void *)f_len},
        {56, "sceHttpReadData", (void *)f_read}, {56, "sceHttpSetResolveTimeOut", (void *)f_timeout},
        {56, "sceHttpSetConnectTimeOut", (void *)f_timeout}, {56, "sceHttpSetSendTimeOut", (void *)f_timeout},
        {56, "sceHttpSetRecvTimeOut", (void *)f_timeout}, {56, "sceHttpSetAutoRedirect", (void *)f_redirect},
        {56, "sceHttpDeleteRequest", (void *)f_dreq}, {56, "sceHttpDeleteConnection", (void *)f_dconn},
        {56, "sceHttpDeleteTemplate", (void *)f_dtmpl}, {56, "sceHttpAddRequestHeader", (void *)f_add_header},
        {56, "sceHttpGetAllResponseHeaders", (void *)f_all_headers}, {56, "sceHttpsGetSslError", (void *)f_ssl_error} };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (t[i].h == h && !strcmp(t[i].n, n)) { *a = t[i].f; return 0; }
    *a = NULL; return (int)0x80020002;
}

#include "https_cut.inc"

static long long prog_last = -1, prog_total = -2; static int prog_calls;
static void prog(void *p, long long got, long long total) { assert(p == (void *)&prog_calls); assert(got > prog_last); prog_last = got; prog_total = total; prog_calls++; }
static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static long long size_of(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long long)st.st_size : -1; }
static void balanced(void) { assert(!live_tmpl && !live_conn && !live_req); }
static char dest[256], part[256];
#define URL "https://cs.amax-emu.com/amax_client_files.zip"
static void reset(long long len, int announce) { honor_range = 0; ranged = 0; cr_override_set = 0; resolve_rc = 0; connect_rc = 0; ssl_err_rc = -1; ssl_err = 0; ssl_detail = 0; body_len = len; announce_len = len; has_len = announce; status_code = 200; send_rc = 0; read_fail_at = -1; prog_last = -1; prog_calls = 0; prog_total = -2; read_delay_ns = 0; stall_ns = 0; stall_at = -1; }

int main(int argc, char **argv)
{
    char dir[] = "/tmp/httpsgetXXXXXX"; assert(mkdtemp(dir));
    snprintf(dest, sizeof dest, "%s/a.zip", dir); snprintf(part, sizeof part, "%s/a.zip.part", dir);
    if (argc > 1 && !strcmp(argv[1], "initfail")) {
        /* a failed init is retried on the next call; a good one is never redone */
        fail_http_init = 1; reset(10, 1);
        assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) < 0);
        assert(http_inits == 1 && ssl_live == 0 && pools_live == 0 && !exists(dest) && !exists(part));
        assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) < 0 && http_inits == 2 && pools_live == 0);
        fail_http_init = 0;
        assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == 0 && http_inits == 3 && size_of(dest) == 10);
        assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == 0 && http_inits == 3 && pools == 3 && pools_live == 1);
        balanced(); remove(dest); rmdir(dir);
        puts("PASS initfail retried, success kept"); return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "nomodule")) {
        fail_modules = 1; reset(10, 1);
        assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) < 0 && loads == 3);
        assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) < 0 && loads == 6 && init_calls == 0);
        fail_modules = 0;
        assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == 0 && loads == 9 && init_calls == 1);
        assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == 0 && loads == 9 && init_calls == 1);
        balanced(); remove(dest); rmdir(dir);
        puts("PASS nomodule retried, success kept"); return 0;
    }
    /* 1 success with length; prog; rename; no .part; handles freed */
    reset(3300467, 1);
    assert(blur_host_https_get(NULL, URL, dest, 3300467, prog, &prog_calls) == 0);
    assert(size_of(dest) == 3300467 && !exists(part) && prog_last == 3300467 && prog_total == 3300467 && prog_calls > 1);
    assert(timeouts_set == 4 && redirect_off == 1); balanced();
    { FILE *f = fopen(dest, "rb"); int c, i = 0, ok = 1; while ((c = fgetc(f)) != EOF) { ok &= c == (uint8_t)i; i++; } fclose(f); assert(ok); }
    puts("PASS success");
    /* 2 init once per process */
    reset(10, 0);
    assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == 0 && size_of(dest) == 10);
    assert(init_calls == 1 && http_inits == 1 && ssl_inits == 1 && pools == 1 && loads == 3); balanced();
    puts("PASS init once, unknown length, NULL prog, replaces dest");
    /* 3 announced length over max: refused before any file */
    reset(200, 1);
    assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) < 0 && size_of(dest) == 10 && !exists(part)); balanced();
    /* 4 unannounced body over max: stopped while streaming, .part removed */
    reset(5000, 0);
    assert(blur_host_https_get(NULL, URL, dest, 4999, NULL, NULL) < 0 && size_of(dest) == 10 && !exists(part)); balanced();
    reset(5000, 0);
    assert(blur_host_https_get(NULL, URL, dest, 5000, NULL, NULL) == 0 && size_of(dest) == 5000); balanced();
    puts("PASS max_bytes");
    /* 5 http status, send failure (timeout), read failure, short body */
    reset(10, 1); status_code = 404;
    assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == -4 && !exists(part)); balanced();
    reset(10, 1); send_rc = (int)0x80431068;    /* a transport timeout after the host answered the check */
    assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == -3 && !exists(part)); balanced();
    reset(5000, 1); read_fail_at = 2000;         /* the connection drops mid-body: interrupted, .part kept */
    assert(blur_host_https_get(NULL, URL, dest, 9000, NULL, NULL) == -3 && size_of(part) == 2000); balanced();
    remove(part);
    reset(5000, 1); announce_len = 6000;         /* shorter than announced: the server's fault */
    assert(blur_host_https_get(NULL, URL, dest, 9000, NULL, NULL) == -4 && !exists(part)); balanced();
    assert(size_of(dest) == 5000);              /* the earlier good file is untouched by failures */
    puts("PASS failures: server error removes the .part, interrupted keeps it");
    /* 7 redirect is not followed: any non-200 fails; a 206 nobody asked for too */
    reset(10, 1); status_code = 302;
    assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == -4 && !exists(part) && size_of(dest) == 5000); balanced();
    reset(10, 1); status_code = 206;
    assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == -4 && !exists(part)); balanced();
    puts("PASS redirect and non-200 fail");
    /* 8 overall limit 150 s: 29 s per 1000 B. 5000 B = 5 reads + EOF = 174 s: over,
     * interrupted with the .part kept; 4000 B = 145 s: inside. */
    reset(5000, 1); read_delay_ns = 29000000000ull;
    assert(blur_host_https_get(NULL, URL, dest, 9000, NULL, NULL) == -3 && size_of(part) == 5000 && size_of(dest) == 5000); balanced();
    remove(part);
    reset(4000, 1); read_delay_ns = 29000000000ull;
    assert(blur_host_https_get(NULL, URL, dest, 9000, NULL, NULL) == 0 && size_of(dest) == 4000 && !exists(part)); balanced();
    reset(4800, 1); read_delay_ns = 25000000000ull;     /* 5 reads + EOF = 150 s: exactly the limit, inside */
    assert(blur_host_https_get(NULL, URL, dest, 9000, NULL, NULL) == 0 && size_of(dest) == 4800); balanced();
    puts("PASS overall limit 150 s");
    /* 9 stall: 31 s without a byte ends it (interrupted, .part kept), 29 s does
     * not; the next call resumes the .part with Range and a matching 206. */
    reset(5000, 1); stall_at = 2000; stall_ns = 31000000000ull;
    assert(blur_host_https_get(NULL, URL, dest, 9000, NULL, NULL) == -3 && size_of(part) == 2000 && size_of(dest) == 4800); balanced();
    reset(5000, 1); honor_range = 1;
    assert(blur_host_https_get(NULL, URL, dest, 9000, prog, &prog_calls) == 0); balanced();
    assert(ranged && !strcmp(range_header, "bytes=2000-") && size_of(dest) == 5000 && !exists(part));
    assert(prog_last == 5000 && prog_total == 5000);
    { FILE *f = fopen(dest, "rb"); int c, i = 0, ok = 1; while ((c = fgetc(f)) != EOF) { ok &= c == (uint8_t)i; i++; } fclose(f); assert(ok && i == 5000); }
    reset(5000, 1); stall_at = 2000; stall_ns = 29000000000ull;
    assert(blur_host_https_get(NULL, URL, dest, 9000, NULL, NULL) == 0 && size_of(dest) == 5000); balanced();
    puts("PASS stall keeps the .part; 206 resume appends exactly");
    /* 10 resume answered 200: the .part starts again from 0, never appended */
    { FILE *s0 = fopen(part, "wb"); for (int i = 0; i < 2000; i++) fputc('x', s0); fclose(s0); }
    reset(5000, 1);
    assert(blur_host_https_get(NULL, URL, dest, 9000, NULL, NULL) == 0 && size_of(dest) == 5000 && !exists(part)); balanced();
    assert(!strcmp(range_header, "bytes=2000-"));
    { FILE *f = fopen(dest, "rb"); int c, i = 0, ok = 1; while ((c = fgetc(f)) != EOF) { ok &= c == (uint8_t)i; i++; } fclose(f); assert(ok && i == 5000); }
    puts("PASS 200 to a resume restarts from 0");
    /* 11 a 206 whose Content-Range does not continue the .part: refused (-7), .part removed */
    static const char *const bad_ranges[] = {
        "Content-Range: bytes 1000-4999/5000",        /* not where the .part ends */
        "Content-Range: bytes 2000-3999/5000",        /* not up to the end */
        "Content-Range: bytes 2000-9999/10000",       /* more than max_bytes */
        "Content-Range: bytes */5000",                /* malformed */
        "Content-Length: 3000",                       /* none at all */
    };
    for (size_t k = 0; k < sizeof bad_ranges / sizeof bad_ranges[0]; k++) {
        FILE *s0 = fopen(part, "wb"); for (int i = 0; i < 2000; i++) fputc(i & 0xff, s0); fclose(s0);
        reset(5000, 0); honor_range = 1; cr_override_set = 1;
        snprintf(cr_override, sizeof cr_override, "%s", bad_ranges[k]);
        assert(blur_host_https_get(NULL, URL, dest, 9000, NULL, NULL) == -7 && !exists(part) && size_of(dest) == 5000); balanced();
    }
    {   /* announced length that does not match the range */
        FILE *s0 = fopen(part, "wb"); for (int i = 0; i < 2000; i++) fputc(i & 0xff, s0); fclose(s0);
        reset(5000, 1); honor_range = 1; announce_len = 5001;
        assert(blur_host_https_get(NULL, URL, dest, 9000, NULL, NULL) == -7 && !exists(part)); balanced();
    }
    {   /* any other answer to a resume: server error, .part removed */
        FILE *s0 = fopen(part, "wb"); fputs("partial", s0); fclose(s0);
        reset(10, 1); status_code = 500;
        assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == -4 && !exists(part) && size_of(dest) == 5000); balanced();
    }
    {   /* a read-only .part cannot be appended to: removed, a fresh download */
        FILE *s0 = fopen(part, "wb"); fputs("stale", s0); fclose(s0); chmod(part, 0444);
        reset(10, 1); honor_range = 1;
        assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == 0 && size_of(dest) == 10 && !exists(part) && !ranged); balanced();
    }
    {   /* a .part as long as the limit is not resumed */
        FILE *s0 = fopen(part, "wb"); for (int i = 0; i < 100; i++) fputc('y', s0); fclose(s0);
        reset(10, 1); honor_range = 1;
        assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == 0 && size_of(dest) == 10 && !ranged); balanced();
    }
    puts("PASS resume mismatch refused");
    /* 12 no connection: the check fails fast, before any request; a .part stays */
    {
        FILE *s0 = fopen(part, "wb"); fputs("partial", s0); fclose(s0);
        reset(10, 1); resolve_rc = -1; reach_calls = 0;
        uint64_t t0 = fake_ns;
        assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == -2 && reach_calls == 1 && size_of(part) == 7);
        assert(fake_ns == t0 && sockets_live == 0); balanced();
        reset(10, 1); connect_rc = -1;
        assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == -2 && size_of(part) == 7 && last_port == 443);
        assert(sockets_live == 0); balanced();
        reset(10, 1); connect_rc = -1;
        assert(blur_host_https_get(NULL, "https://cs.amax-emu.com:8443/x.zip", dest, 100, NULL, NULL) == -2 && last_port == 8443);
        remove(part);
    }
    puts("PASS no connection fails fast");
    /* 13 certificate not accepted (clock): a distinct result, checking stays on */
    {
        static const struct { unsigned rc; int ssl_rc; unsigned err, detail; int want; } certs[] = {
            { 0x80435060u, -1, 0, 0, -6 },                    /* SCE_HTTPS_ERROR_CERT */
            { 0x80431075u, 0, 0x80435061u, 0x08, -6 },        /* SSL error, certificate expired */
            { 0x80435061u, 0, 0x80435061u, 0x10, -6 },        /* handshake, not yet valid */
            { 0x80431075u, 0, 0x80435060u, 0, -6 },           /* the SSL error names the certificate */
            { 0x80431075u, 0, 0x80435061u, 0x04, -3 },        /* name mismatch only: not the clock */
            { 0x80431075u, -1, 0, 0, -3 },                    /* no detail */
        };
        for (size_t k = 0; k < sizeof certs / sizeof certs[0]; k++) {
            reset(10, 1); send_rc = (int)certs[k].rc; ssl_err_rc = certs[k].ssl_rc; ssl_err = (int)certs[k].err;
            ssl_detail = certs[k].detail;
            assert(blur_host_https_get(NULL, URL, dest, 100, NULL, NULL) == certs[k].want && !exists(part)); balanced();
        }
    }
    puts("PASS certificate errors told apart");
    /* 6 refused input: http://, NULL, max 0; a stale .part is removed */
    FILE *s = fopen(part, "wb"); fputs("stale", s); fclose(s);
    assert(blur_host_https_get(NULL, "http://cs.amax-emu.com/x", dest, 100, NULL, NULL) == -1 && !exists(part));
    assert(blur_host_https_get(NULL, NULL, dest, 100, NULL, NULL) == -1);
    assert(blur_host_https_get(NULL, URL, NULL, 100, NULL, NULL) == -1);
    assert(blur_host_https_get(NULL, URL, dest, 0, NULL, NULL) == -1);
    puts("PASS refused input");
    remove(dest); rmdir(dir);
    puts("ALL PASS");
    return 0;
}
