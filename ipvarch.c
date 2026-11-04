/*
 * ipvarch.c - IPVarch: Internet Protocol Version Architect
 *
 * A small ANSI C (C89) command line tool for inspecting and planning
 * IP addresses and IPv4 networks.
 *
 * Modes:
 *   ipvarch <address>                          inspect an IPv4 or IPv6 address
 *   ipvarch --plan-network <ipv4>/<prefix>     print a network plan table
 *   ipvarch --random-ipv4network <scope> <prefix>
 *                                              generate a random valid plan
 *
 * Targets tcc (Tiny C Compiler); also builds with gcc -std=c89 -pedantic.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

typedef unsigned char byte;

/* UTF-8 encoded box drawing characters (U+2500 block). */
#define BOX_H  "\xe2\x94\x80"   /* horizontal line */
#define BOX_V  "\xe2\x94\x82"   /* vertical line   */
#define BOX_TL "\xe2\x94\x8c"   /* top left     ┌ */
#define BOX_TR "\xe2\x94\x90"   /* top right    ┐ */
#define BOX_BL "\xe2\x94\x94"   /* bottom left  └ */
#define BOX_BR "\xe2\x94\x98"   /* bottom right ┘ */
#define BOX_TJ "\xe2\x94\xac"   /* top join     ┬ */
#define BOX_BJ "\xe2\x94\xb4"   /* bottom join  ┴ */
#define BOX_LJ "\xe2\x94\x9c"   /* left join    ├ */
#define BOX_RJ "\xe2\x94\xa4"   /* right join   ┤ */
#define BOX_CJ "\xe2\x94\xbc"   /* centre join  ┼ */

typedef const char *Row[2];

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void die(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "ipvarch: error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

static void usage(void)
{
    printf("IPVarch - Internet Protocol Version Architect\n\n");
    printf("Usage:\n");
    printf("  ipvarch <address>                          inspect an IPv4/IPv6 address\n");
    printf("  ipvarch --plan-network <ipv4>/<prefix>     plan a network (/8 /16 /24 /32)\n");
    printf("  ipvarch --random-ipv4network <scope> <prefix>\n");
    printf("                                             random valid network plan\n\n");
    printf("Examples:\n");
    printf("  ipvarch 192.168.1.1\n");
    printf("  ipvarch 2001:db8::1\n");
    printf("  ipvarch --plan-network 172.16.0.0/16\n");
    printf("  ipvarch --random-ipv4network private /24\n");
    return;
}

/* Display width of a UTF-8 string (continuation bytes occupy no column). */
static int dwidth(const char *s)
{
    int w = 0;
    while (*s != '\0') {
        if ((((unsigned char)*s) & 0xC0) != 0x80)
            w++;
        s++;
    }
    return w;
}

static void dashes(int n)
{
    while (n-- > 0)
        fputs(BOX_H, stdout);
    return;
}

static void padcell(const char *s, int w)
{
    int d = dwidth(s) + 1;
    putchar(' ');
    fputs(s, stdout);
    while (d < w) {
        putchar(' ');
        d++;
    }
    return;
}

static void centercell(const char *s, int w)
{
    int d = dwidth(s) + 1;
    int l = (w - d) / 2;
    int r = w - d - l;
    while (l-- > 0)
        putchar(' ');
    putchar(' ');
    fputs(s, stdout);
    while (r-- > 0)
        putchar(' ');
    return;
}

static void print_table(const char *title, const char *rows[][2], int nrows)
{
    int w1 = 0, w2 = 0, tw, i;

    for (i = 0; i < nrows; i++) {
        int a = dwidth(rows[i][0]) + 2;   /* room for cell padding */
        int b = dwidth(rows[i][1]) + 2;
        if (a > w1) w1 = a;
        if (b > w2) w2 = b;
    }
    tw = dwidth(title) + 2;
    if (tw > w1 + w2 - 1)
        w2 = tw - w1 + 1;

    fputs(BOX_TL, stdout); dashes(w1); fputs(BOX_TJ, stdout);
    dashes(w2); fputs(BOX_TR "\n", stdout);

    fputs(BOX_V, stdout); centercell(title, w1 + w2 + 1); fputs(BOX_V "\n", stdout);

    fputs(BOX_LJ, stdout); dashes(w1); fputs(BOX_CJ, stdout);
    dashes(w2); fputs(BOX_RJ "\n", stdout);

    for (i = 0; i < nrows; i++) {
        fputs(BOX_V, stdout); padcell(rows[i][0], w1);
        fputs(BOX_V, stdout); padcell(rows[i][1], w2);
        fputs(BOX_V "\n", stdout);
    }

    fputs(BOX_BL, stdout); dashes(w1); fputs(BOX_BJ, stdout);
    dashes(w2); fputs(BOX_BR "\n", stdout);
    return;
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

static int parse_ipv4(const char *s, byte out[4])
{
    const char *p = s;
    int i;

    for (i = 0; i < 4; i++) {
        int v = 0, digits = 0;
        if (i > 0) {
            if (*p != '.')
                return 0;
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            digits++;
            p++;
            if (digits > 3 || v > 255)
                return 0;
        }
        if (digits == 0)
            return 0;
        out[i] = (byte)v;
    }
    if (*p != '\0')
        return 0;
    return 1;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse a colon separated piece list such as "ffff:0:192.168.1.1".
 * The final token may be an embedded IPv4 address (two groups).
 * Returns count via *cnt and 1 on success. */
static int parse_piece(const char *piece, unsigned short *gr, int *cnt)
{
    char buf[48];
    byte v4[4];
    const char *p;
    int n = 0;

    if (strlen(piece) >= sizeof(buf))
        return 0;
    strcpy(buf, piece);
    p = buf;

    while (*p != '\0') {
        const char *tok = p;
        int hasdot = 0;

        while (*p != '\0' && *p != ':') {
            if (*p == '.')
                hasdot = 1;
            p++;
        }

        if (hasdot) {
            if (*p != '\0')           /* embedded IPv4 must be last */
                return 0;
            if (!parse_ipv4(tok, v4))
                return 0;
            if (n > 6)
                return 0;
            gr[n++] = (unsigned short)((v4[0] << 8) | v4[1]);
            gr[n++] = (unsigned short)((v4[2] << 8) | v4[3]);
        } else {
            int len = (int)(p - tok);
            int j, val = 0;
            if (len < 1 || len > 4)
                return 0;
            for (j = 0; j < len; j++) {
                int hv = hexval((unsigned char)tok[j]);
                if (hv < 0)
                    return 0;
                val = (val << 4) | hv;
            }
            if (n >= 8)
                return 0;
            gr[n++] = (unsigned short)val;
        }

        if (*p == '\0')
            break;
        p++;                          /* skip ':' */
        if (*p == '\0')               /* trailing colon */
            return 0;
    }

    *cnt = n;
    return 1;
}

/* Parse an IPv6 address (RFC 4291 text form, compressed "::" allowed,
 * embedded IPv4 in the tail allowed) into 16 bytes. */
static int parse_ipv6(const char *s, byte out[16])
{
    char buf[80];
    char *dd, *head, *tail;
    unsigned short gr[16];
    int nh = 0, nt = 0, total, i;

    if (strlen(s) >= sizeof(buf))
        return 0;
    strcpy(buf, s);

    dd = strstr(buf, "::");
    if (dd != NULL) {
        if (strstr(dd + 1, "::") != NULL)
            return 0;
        *dd = '\0';
        head = buf;
        tail = dd + 2;
        /* An embedded IPv4 is only valid in the tail piece. */
        if (strchr(head, '.') != NULL && strchr(head, ':') == NULL)
            return 0;
    } else {
        head = buf;
        tail = NULL;
    }

    if (!parse_piece(head, gr, &nh))
        return 0;
    if (tail != NULL) {
        if (!parse_piece(tail, gr + nh, &nt))
            return 0;
    }
    total = nh + nt;

    if (dd != NULL) {
        if (total > 7)                /* "::" must stand for zero groups */
            return 0;
        memset(out, 0, 16);
        for (i = 0; i < nh; i++) {
            out[2 * i]     = (byte)(gr[i] >> 8);
            out[2 * i + 1] = (byte)(gr[i] & 0xFF);
        }
        for (i = 0; i < nt; i++) {
            int k = 8 - nt + i;
            out[2 * k]     = (byte)(gr[nh + i] >> 8);
            out[2 * k + 1] = (byte)(gr[nh + i] & 0xFF);
        }
    } else {
        if (total != 8)
            return 0;
        for (i = 0; i < 8; i++) {
            out[2 * i]     = (byte)(gr[i] >> 8);
            out[2 * i + 1] = (byte)(gr[i] & 0xFF);
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Formatting                                                          */
/* ------------------------------------------------------------------ */

static void fmt_v4(char *dst, const byte a[4])
{
    sprintf(dst, "%u.%u.%u.%u",
            (unsigned)a[0], (unsigned)a[1], (unsigned)a[2], (unsigned)a[3]);
    return;
}

/* Compressed RFC 5952 style text form. */
static void fmt_v6(char *dst, const byte a[16])
{
    unsigned short g[8];
    int i, zs = -1, zl = 0, bs = -1, bl = 0, first = 1, suppress = 0;
    char *p = dst;

    for (i = 0; i < 8; i++)
        g[i] = (unsigned short)((a[2 * i] << 8) | a[2 * i + 1]);

    for (i = 0; i < 8; i++) {
        if (g[i] == 0) {
            if (zl == 0)
                zs = i;
            zl++;
            if (zl > bl) {
                bl = zl;
                bs = zs;
            }
        } else {
            zl = 0;
        }
    }
    if (bl < 2) {
        bs = 0;
        bl = 0;
    }

    if (bl == 8) {
        strcpy(dst, "::");
        return;
    }

    i = 0;
    while (i < 8) {
        if (bl > 0 && i >= bs && i < bs + bl) {
            *p++ = ':';
            *p++ = ':';
            suppress = 1;                 /* "::" replaces the join */
            first = 0;
            i = bs + bl;
            continue;
        }
        if (!first && !suppress)
            *p++ = ':';
        suppress = 0;
        p += sprintf(p, "%x", (unsigned)g[i]);
        first = 0;
        i++;
    }
    *p = '\0';
    return;
}

/* ------------------------------------------------------------------ */
/* Classification (rules per ipaddrinfo.html)                          */
/* ------------------------------------------------------------------ */

static void classify_v4(const byte a[4], const char **cls, const char **scope)
{
    if (a[0] == 0) {
        *cls = "0 (reserved)";
        *scope = "Reserved (this network)";
    } else if (a[0] < 128) {
        *cls = "A";
        *scope = "Public";
    } else if (a[0] < 192) {
        *cls = "B";
        *scope = "Public";
    } else if (a[0] < 224) {
        *cls = "C";
        *scope = "Public";
    } else if (a[0] < 240) {
        *cls = "D";
        *scope = "Multicast";
    } else {
        *cls = "E";
        *scope = "Reserved";
    }

    if (a[0] == 127)
        *scope = "Loopback";
    else if (a[0] == 10)
        *scope = "Private (RFC 1918)";
    else if (a[0] == 172 && a[1] >= 16 && a[1] <= 31)
        *scope = "Private (RFC 1918)";
    else if (a[0] == 192 && a[1] == 168)
        *scope = "Private (RFC 1918)";
    else if (a[0] == 169 && a[1] == 254)
        *scope = "Link-local (APIPA)";
    else if (a[0] == 192 && a[1] == 0 && a[2] == 2)
        *scope = "Documentation (TEST-NET-1)";
    else if (a[0] == 198 && a[1] == 51 && a[2] == 100)
        *scope = "Documentation (TEST-NET-2)";
    else if (a[0] == 203 && a[1] == 0 && a[2] == 113)
        *scope = "Documentation (TEST-NET-3)";
    else if (a[0] == 100 && a[1] >= 64 && a[1] <= 127)
        *scope = "Shared (CGNAT)";
    else if (a[0] == 198 && (a[1] == 18 || a[1] == 19))
        *scope = "Benchmarking";
    return;
}

static void classify_v6(const byte a[16], const char **scope)
{
    int i;

    for (i = 0; i < 16; i++) {
        if (a[i] != 0)
            break;
    }
    if (i == 16) {
        *scope = "Unspecified (::)";
        return;
    }
    if (a[0] == 0 && a[1] == 0 && a[2] == 0 && a[3] == 0 && a[4] == 0 &&
        a[5] == 0 && a[6] == 0 && a[7] == 0 && a[8] == 0 && a[9] == 0 &&
        a[10] == 0 && a[11] == 0 && a[12] == 0 && a[13] == 0 &&
        a[14] == 0 && a[15] == 1) {
        *scope = "Loopback (::1)";
        return;
    }
    if (a[0] == 0 && a[1] == 0 && a[2] == 0 && a[3] == 0 && a[4] == 0 &&
        a[5] == 0 && a[6] == 0 && a[7] == 0 && a[8] == 0 && a[9] == 0 &&
        a[10] == 0xFF && a[11] == 0xFF) {
        *scope = "IPv4-mapped (::ffff:0:0/96)";
        return;
    }
    if (a[0] == 0xFF) {
        *scope = "Multicast (ff00::/8)";
        return;
    }
    if (a[0] == 0xFE && (a[1] & 0xC0) == 0x80) {
        *scope = "Link-local (fe80::/10)";
        return;
    }
    if ((a[0] & 0xFE) == 0xFC) {
        *scope = "Private (unique local, fc00::/7)";
        return;
    }
    if (a[0] == 0x20 && a[1] == 0x01 && a[2] == 0x0D && a[3] == 0xB8) {
        *scope = "Documentation (2001:db8::/32)";
        return;
    }
    *scope = "Public (global unicast)";
    return;
}

/* ------------------------------------------------------------------ */
/* IPv4 network math                                                   */
/* ------------------------------------------------------------------ */

static unsigned long v4_to_u32(const byte a[4])
{
    return ((unsigned long)a[0] << 24) | ((unsigned long)a[1] << 16) |
           ((unsigned long)a[2] << 8) | (unsigned long)a[3];
}

static void u32_to_v4(unsigned long u, byte out[4])
{
    out[0] = (byte)((u >> 24) & 0xFF);
    out[1] = (byte)((u >> 16) & 0xFF);
    out[2] = (byte)((u >>  8) & 0xFF);
    out[3] = (byte)(u & 0xFF);
    return;
}

static void print_plan(unsigned long net, int prefix)
{
    unsigned long mask, bc, cnt, firstu, lastu;
    byte a[4], m4[4], b4[4], lo4[4], hi4[4];
    char tmp1[24], tmp2[24];
    char nb[24], mb[40], bb[24], rb[64], cb[24], gb[24], db[24], pb[64];
    char sb[48], tb[80];
    const char *cls, *scope;
    Row rows[10];
    int nr = 0;

    mask = (prefix == 8)  ? 0xFF000000ul :
           (prefix == 16) ? 0xFFFF0000ul :
           (prefix == 24) ? 0xFFFFFF00ul : 0xFFFFFFFFul;
    bc = net | (~mask & 0xFFFFFFFFul);

    u32_to_v4(net, a);
    u32_to_v4(mask, m4);
    classify_v4(a, &cls, &scope);

    fmt_v4(nb, a);
    fmt_v4(tmp1, m4);
    sprintf(mb, "%s /%d", tmp1, prefix);

    if (prefix == 32) {
        strcpy(bb, "n/a (single host)");
        strcpy(rb, nb);                   /* the host address itself */
        strcpy(cb, "1");
        strcpy(gb, "n/a (single host)");
        strcpy(db, "n/a (single host)");
        strcpy(pb, "disabled (single-host network)");
    } else {
        firstu = net + 1;
        lastu  = bc - 1;
        cnt    = (1ul << (32 - prefix)) - 2;

        u32_to_v4(bc, b4);
        fmt_v4(bb, b4);
        u32_to_v4(firstu, lo4);
        fmt_v4(tmp1, lo4);
        u32_to_v4(lastu, hi4);
        fmt_v4(tmp2, hi4);
        sprintf(rb, "%s - %s", tmp1, tmp2);
        sprintf(cb, "%lu", cnt);

        fmt_v4(gb, lo4);                  /* gateway: first usable    */
        u32_to_v4(firstu + 1, hi4);
        fmt_v4(db, hi4);                  /* DNS/DHCP: next usable    */

        hi4[0] = a[0]; hi4[1] = a[1]; hi4[2] = a[2]; hi4[3] = 254;
        lo4[3] = 100;
        fmt_v4(tmp1, lo4);
        fmt_v4(tmp2, hi4);
        sprintf(pb, "%s - %s", tmp1, tmp2);
    }

    sprintf(sb, "%s", scope);
    sprintf(tb, "IPVarch Network Plan - %s /%d", nb, prefix);

    rows[nr][0] = "Network address";   rows[nr][1] = nb; nr++;
    rows[nr][0] = "Netmask";           rows[nr][1] = mb; nr++;
    rows[nr][0] = "Broadcast";         rows[nr][1] = bb; nr++;
    rows[nr][0] = "Usable host range"; rows[nr][1] = rb; nr++;
    rows[nr][0] = "Usable hosts";      rows[nr][1] = cb; nr++;
    rows[nr][0] = "Class";             rows[nr][1] = cls; nr++;
    rows[nr][0] = "Scope";             rows[nr][1] = sb; nr++;
    rows[nr][0] = "Gateway IP";        rows[nr][1] = gb; nr++;
    rows[nr][0] = "DNS/DHCP server";   rows[nr][1] = db; nr++;
    rows[nr][0] = "DHCP pool";         rows[nr][1] = pb; nr++;

    print_table(tb, rows, nr);
    return;
}

/* ------------------------------------------------------------------ */
/* Random network generation                                           */
/* ------------------------------------------------------------------ */

static int is_ok_public(const byte a[4])
{
    if (a[0] == 0 || a[0] == 10 || a[0] == 127)
        return 0;
    if (a[0] == 172 && a[1] >= 16 && a[1] <= 31)
        return 0;
    if (a[0] == 192 && a[1] == 168)
        return 0;
    if (a[0] == 169 && a[1] == 254)
        return 0;
    if (a[0] == 100 && a[1] >= 64 && a[1] <= 127)
        return 0;
    if (a[0] == 198 && (a[1] == 18 || a[1] == 19))
        return 0;
    if (a[0] == 192 && a[1] == 0 && a[2] == 2)
        return 0;
    if (a[0] == 198 && a[1] == 51 && a[2] == 100)
        return 0;
    if (a[0] == 203 && a[1] == 0 && a[2] == 113)
        return 0;
    if (a[0] >= 224)
        return 0;
    return 1;
}

static void random_network(int prefix, int wantprivate, byte out[4])
{
    int tries, r;
    byte a[4];

    for (tries = 0; tries < 10000; tries++) {
        r = rand();
        if (wantprivate) {
            if (prefix == 8) {
                /* Only one valid private /8 exists: 10.0.0.0/8 */
                a[0] = 10; a[1] = 0; a[2] = 0; a[3] = 0;
            } else if (prefix == 16) {
                if (r % 2) {
                    a[0] = 192; a[1] = 168; a[2] = rand() % 256;
                } else {
                    a[0] = 172; a[1] = 16 + rand() % 16; a[2] = rand() % 256;
                }
                a[3] = 0;
            } else {
                r = rand() % 8;
                if (r < 4) {
                    a[0] = 10; a[1] = rand() % 256; a[2] = rand() % 256;
                } else if (r < 6) {
                    a[0] = 172; a[1] = 16 + rand() % 16; a[2] = rand() % 256;
                } else {
                    a[0] = 192; a[1] = 168; a[2] = rand() % 256;
                }
                if (prefix == 24)
                    a[3] = 0;
                else
                    a[3] = 1 + rand() % 254;
            }
        } else {
            a[0] = (rand() % 223) + 1;    /* avoids class D/E and 0 */
            if (a[0] == 10 || a[0] == 127)
                continue;
            a[1] = rand() % 256;
            a[2] = rand() % 256;
            a[3] = (prefix == 32) ? (1 + rand() % 254) : 0;
        }
        /* Zero the host bits for the chosen prefix. */
        if (prefix == 8)  { a[1] = 0; a[2] = 0; a[3] = 0; }
        if (prefix == 16) { a[2] = 0; a[3] = 0; }
        if (prefix == 24) { a[3] = 0; }
        if (!wantprivate && !is_ok_public(a))
            continue;
        out[0] = a[0]; out[1] = a[1]; out[2] = a[2]; out[3] = a[3];
        return;
    }
    die("could not generate a suitable random network; please retry");
    return;
}

/* ------------------------------------------------------------------ */
/* Modes                                                               */
/* ------------------------------------------------------------------ */

static void mode_info(const char *arg)
{
    byte a[16];
    const char *cls = "", *scope = "";
    char nb[64], gb[96], ob[96];
    int i;
    unsigned short g[8];
    char *q;

    if (strchr(arg, '/') != NULL)
        die("info mode takes a plain address; for subnets use --plan-network");

    if (strchr(arg, ':') != NULL) {
        if (!parse_ipv6(arg, a))
            die("invalid IPv6 address: %s", arg);
        fmt_v6(nb, a);
        classify_v6(a, &scope);
        for (i = 0; i < 8; i++)
            g[i] = (unsigned short)((a[2 * i] << 8) | a[2 * i + 1]);

        printf("Address: %s\n", nb);
        printf("Type:    IPv6\n");
        printf("Class:   n/a (IPv6 has no address classes)\n");
        printf("Scope:   %s\n", scope);

        q = gb;
        for (i = 0; i < 8; i++)
            q += sprintf(q, "%s%04x", i > 0 ? " " : "", (unsigned)g[i]);
        printf("Groups:  %s\n", gb);

        q = ob;
        for (i = 0; i < 16; i++)
            q += sprintf(q, "%s%u", i > 0 ? ", " : "", (unsigned)a[i]);
        printf("Octets:  %s\n", ob);
    } else {
        byte a4[4];
        if (!parse_ipv4(arg, a4))
            die("invalid IPv4 or IPv6 address: %s", arg);
        classify_v4(a4, &cls, &scope);

        q = ob;
        for (i = 0; i < 4; i++)
            q += sprintf(q, "%s%u", i > 0 ? ", " : "", (unsigned)a4[i]);

        printf("Address: %u.%u.%u.%u\n",
               (unsigned)a4[0], (unsigned)a4[1], (unsigned)a4[2], (unsigned)a4[3]);
        printf("Type:    IPv4\n");
        printf("Class:   %s\n", cls);
        printf("Scope:   %s\n", scope);
        printf("Octets:  %s\n", ob);
    }
    return;
}

static void mode_plan(const char *arg)
{
    char buf[80];
    char *slash;
    byte a[4];
    unsigned long u, mask, net;
    int prefix;

    if (strlen(arg) >= sizeof(buf) - 1)
        die("argument too long: %s", arg);
    strcpy(buf, arg);

    slash = strchr(buf, '/');
    if (slash == NULL)
        die("expected a subnet with a mask suffix (e.g. 192.168.1.0/24)");
    *slash = '\0';

    if (!parse_ipv4(buf, a))
        die("invalid IPv4 address in subnet: %s", buf);

    prefix = atoi(slash + 1);
    if (prefix != 8 && prefix != 16 && prefix != 24 && prefix != 32)
        die("unsupported prefix '%s' (supported: /8 /16 /24 /32)", slash + 1);

    mask = (prefix == 8)  ? 0xFF000000ul :
           (prefix == 16) ? 0xFFFF0000ul :
           (prefix == 24) ? 0xFFFFFF00ul : 0xFFFFFFFFul;
    u = v4_to_u32(a);
    net = u & mask;

    if (net != u) {
        byte c[4];
        u32_to_v4(net, c);
        die("host bits are set; the network address for that mask is %u.%u.%u.%u/%d",
            (unsigned)c[0], (unsigned)c[1], (unsigned)c[2], (unsigned)c[3], prefix);
    }
    if (a[0] == 0)
        die("0.0.0.0 networks are reserved and cannot be planned");
    if (a[0] >= 224)
        die("multicast and reserved networks (class D/E) cannot be planned");

    print_plan(net, prefix);
    return;
}

static void mode_random(const char *scope_s, const char *prefix_s)
{
    char lc[16];
    const char *p;
    byte net[4];
    int prefix, priv;
    char *q;

    if (strlen(scope_s) >= sizeof(lc))
        die("invalid scope: %s", scope_s);
    q = lc;
    for (p = scope_s; *p != '\0'; p++)
        *q++ = (char)((*p >= 'A' && *p <= 'Z') ? (*p - 'A' + 'a') : *p);
    *q = '\0';

    if (strcmp(lc, "private") == 0)
        priv = 1;
    else if (strcmp(lc, "public") == 0)
        priv = 0;
    else
        die("invalid scope '%s' (expected private or public)", scope_s);

    if (prefix_s[0] == '/')
        prefix_s++;
    prefix = atoi(prefix_s);
    if (prefix != 8 && prefix != 16 && prefix != 24 && prefix != 32)
        die("unsupported prefix '/%s' (supported: /8 /16 /24 /32)", prefix_s);

    srand((unsigned)time(NULL));
    random_network(prefix, priv, net);

    printf("Random network plan (--random-ipv4network %s /%d)\n\n",
           priv ? "private" : "public", prefix);
    print_plan(v4_to_u32(net), prefix);
    return;
}

int main(int argc, char **argv)
{
    if (argc >= 2 &&
        (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ||
         strcmp(argv[1], "help") == 0)) {
        usage();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--plan-network") == 0) {
        if (argc != 3)
            die("--plan-network takes exactly one argument: <ipv4>/<prefix>");
        mode_plan(argv[2]);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--random-ipv4network") == 0) {
        if (argc != 4)
            die("--random-ipv4network takes two arguments: <private|public> </8|/16|/24|/32>");
        mode_random(argv[2], argv[3]);
        return 0;
    }
    if (argc == 2) {
        mode_info(argv[1]);
        return 0;
    }
    usage();
    return 1;
}
