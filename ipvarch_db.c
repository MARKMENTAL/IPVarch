/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ipvarch_db.c - used-networks SQLite backend for IPVarch */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "vendor/sqlite3.h"
#include "ipvarch_db.h"

#define DB_PATH_SZ 640

static sqlite3 *g_db = NULL;
static int g_state = -1;             /* -1 = not opened; else DB_* state  */
static char g_path[DB_PATH_SZ] = "";
static char g_err[256] = "";

const char *db_last_error(void)
{
    return g_err[0] != '\0' ? g_err : "unknown database error";
}

const char *db_path(void)
{
    return g_path;
}

static void record_err(const char *context)
{
    const char *msg = (g_db != NULL) ? sqlite3_errmsg(g_db) : NULL;
    if (msg != NULL)
        sqlite3_snprintf((int)sizeof g_err, g_err, "%s: %s", context, msg);
    else
        sqlite3_snprintf((int)sizeof g_err, g_err, "%s", context);
    return;
}

static void resolve_db_path(char *path, int editable)
{
    const char *env, *base;
    char dir[512];

    env = getenv("IPVARCH_DB");
    if (env != NULL && env[0] != '\0') {
        sqlite3_snprintf(DB_PATH_SZ, path, "%s", env);
        return;
    }
    base = getenv("XDG_CONFIG_HOME");
    if (base != NULL && base[0] != '\0') {
        sqlite3_snprintf((int)sizeof dir, dir, "%s/ipvarch", base);
    } else {
        base = getenv("HOME");
        if (base == NULL || base[0] == '\0') {
            sqlite3_snprintf(DB_PATH_SZ, path, "networks.db");
            return;
        }
        sqlite3_snprintf((int)sizeof dir, dir, "%s/.config/ipvarch", base);
    }
    if (editable)
        mkdir(dir, 0755);            /* silently fine when it exists */
    sqlite3_snprintf(DB_PATH_SZ, path, "%s/networks.db", dir);
    return;
}

static int table_exists(void)
{
    sqlite3_stmt *st = NULL;
    int exists = 0;

    if (sqlite3_prepare_v2(g_db,
            "SELECT count(*) FROM sqlite_master WHERE type='table' "
            "AND name='UsedNetworks'", -1, &st, NULL) != SQLITE_OK) {
        record_err("count tables");
        return -1;
    }
    if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int64(st, 0) > 0)
        exists = 1;
    sqlite3_finalize(st);
    return exists;
}

/* Release the SQLite connection (registered via atexit in db_do_open;
 * runs on both normal returns and die()/exit() paths so sanitizers
 * see a clean shutdown). */
void db_close(void)
{
    if (g_db != NULL) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
    g_state = -1;
    sqlite3_shutdown();              /* free SQLite's one-time globals */
}

static int db_do_open(int editable)
{
    static int close_registered = 0;
    int flags, exists;

    if (g_state != -1)
        return g_state;              /* already opened once */

    resolve_db_path(g_path, editable);
    flags = editable ? (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)
                     : SQLITE_OPEN_READONLY;

    if (sqlite3_open_v2(g_path, &g_db, flags, NULL) != SQLITE_OK) {
        record_err("open database");
        if (g_db != NULL)
            sqlite3_close(g_db);
        g_db = NULL;
        g_state = editable ? DB_IO : DB_EMPTY;
        return g_state;
    }

    if (editable) {
        if (sqlite3_exec(g_db,
                "CREATE TABLE IF NOT EXISTS UsedNetworks ("
                " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                " cidr TEXT NOT NULL UNIQUE,"
                " prefix INTEGER NOT NULL,"
                " start_hi INTEGER NOT NULL,"
                " start_lo INTEGER NOT NULL,"
                " end_hi INTEGER NOT NULL,"
                " end_lo INTEGER NOT NULL,"
                " label TEXT NOT NULL,"
                " created_at DATETIME DEFAULT CURRENT_TIMESTAMP)",
                NULL, NULL, NULL) != SQLITE_OK) {
            record_err("create table");
            sqlite3_close(g_db);
            g_db = NULL;
            g_state = DB_IO;
            return g_state;
        }
    }

    if (!close_registered) {
        atexit(db_close);
        close_registered = 1;
    }

    exists = table_exists();
    if (exists < 0) {
        sqlite3_close(g_db);
        g_db = NULL;
        g_state = editable ? DB_IO : DB_EMPTY;
        return g_state;
    }
    g_state = exists ? DB_OK : DB_EMPTY;
    return g_state;
}

int db_init(int editable)
{
    return db_do_open(editable);
}

/* 32-bit mask math (host order). */
static unsigned long mask_of(int prefix)
{
    if (prefix >= 32)
        return 0xFFFFFFFFUL;
    if (prefix <= 0)
        return 0UL;
    return 0xFFFFFFFFUL << (32 - prefix);
}

static unsigned long range_end(unsigned long net, int prefix)
{
    return net | (0xFFFFFFFFUL ^ mask_of(prefix));
}

static int interval_overlaps(unsigned long s1, unsigned long e1,
                             unsigned long s2, unsigned long e2)
{
    return (s2 <= e1 && s1 <= e2) ? 1 : 0;
}

/* Count registered networks intersecting [start,end]. Optional out
 * params (may both be NULL) receive the first match and how many
 * further matches exist. Returns DB_OK or DB_IO. */
static int scan_used(unsigned long start, unsigned long end,
                     char *hit_cidr, int hit_cidr_sz,
                     char *hit_label, int hit_label_sz,
                     int *out_conflicts, int *out_extra)
{
    sqlite3_stmt *st = NULL;
    unsigned long hstart, hend;
    const unsigned char *text;
    int count = 0, extra = 0, rc;

    if (out_conflicts != NULL)
        *out_conflicts = 0;
    if (out_extra != NULL)
        *out_extra = 0;

    if (g_state == -1) {
        g_state = db_do_open(0);
        if (g_state != DB_OK) {
            return (g_state == DB_EMPTY) ? DB_OK : DB_IO;
        }
    }
    if (g_state == DB_EMPTY)
        return DB_OK;                /* nothing registered: range is free */

    if (sqlite3_prepare_v2(g_db,
            "SELECT cidr,label,start_hi,start_lo,end_hi,end_lo "
            "FROM UsedNetworks ORDER BY cidr", -1, &st, NULL) != SQLITE_OK) {
        record_err("prepare UsedNetworks scan");
        return DB_IO;
    }

    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        hstart = ((unsigned long)sqlite3_column_int64(st, 2)) << 16 |
                 ((unsigned long)sqlite3_column_int64(st, 3));
        hend   = ((unsigned long)sqlite3_column_int64(st, 4)) << 16 |
                 ((unsigned long)sqlite3_column_int64(st, 5));
        if (interval_overlaps(start, end, hstart, hend)) {
            if (count == 0 && hit_cidr != NULL) {
                text = sqlite3_column_text(st, 0);
                sqlite3_snprintf(hit_cidr_sz, hit_cidr, "%s",
                                 text ? (const char *)text : "");
            }
            if (count == 0 && hit_label != NULL) {
                text = sqlite3_column_text(st, 1);
                sqlite3_snprintf(hit_label_sz, hit_label, "%s",
                                 text ? (const char *)text : "");
            }
            if (count > 0)
                extra++;
            count++;
        }
    }
    if (rc != SQLITE_DONE) {
        record_err("step UsedNetworks scan");
        sqlite3_finalize(st);
        return DB_IO;
    }
    sqlite3_finalize(st);
    if (out_conflicts != NULL)
        *out_conflicts = count;
    if (out_extra != NULL)
        *out_extra = extra;
    return DB_OK;
}

int db_add_network(unsigned long net, int prefix, const char *cidr,
                   const char *label)
{
    sqlite3_stmt *st = NULL;
    unsigned long start, end;
    int rc;

    if (g_state != DB_OK) {
        g_state = db_do_open(1);
        if (g_state != DB_OK)
            return DB_IO;
    }
    start = net;
    end = range_end(net, prefix);

    if (sqlite3_prepare_v2(g_db,
            "INSERT INTO UsedNetworks"
            " (cidr,prefix,start_hi,start_lo,end_hi,end_lo,label)"
            " VALUES (?1,?2,?3,?4,?5,?6,?7)", -1, &st, NULL) != SQLITE_OK) {
        record_err("prepare insert");
        return DB_IO;
    }
    sqlite3_bind_text(st, 1, cidr, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, (sqlite3_int64)prefix);
    sqlite3_bind_int64(st, 3, (sqlite3_int64)((start >> 16) & 0xFFFF));
    sqlite3_bind_int64(st, 4, (sqlite3_int64)(start & 0xFFFF));
    sqlite3_bind_int64(st, 5, (sqlite3_int64)((end >> 16) & 0xFFFF));
    sqlite3_bind_int64(st, 6, (sqlite3_int64)(end & 0xFFFF));
    sqlite3_bind_text(st, 7, label, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc == SQLITE_DONE)
        return DB_OK;
    if (rc == SQLITE_CONSTRAINT)
        return DB_DUPLICATE;
    record_err("insert");
    return DB_IO;
}

int db_get_label(const char *cidr, char *buf, int bufsz)
{
    sqlite3_stmt *st = NULL;
    const unsigned char *text;
    int rc;

    if (buf != NULL && bufsz > 0)
        buf[0] = '\0';

    if (g_state != DB_OK) {
        g_state = db_do_open(1);
        if (g_state != DB_OK)
            return DB_IO;
    }
    if (sqlite3_prepare_v2(g_db,
            "SELECT label FROM UsedNetworks WHERE cidr = ?1",
            -1, &st, NULL) != SQLITE_OK) {
        record_err("prepare label select");
        return DB_IO;
    }
    sqlite3_bind_text(st, 1, cidr, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        text = sqlite3_column_text(st, 0);
        sqlite3_snprintf(bufsz, buf, "%s",
                         text ? (const char *)text : "");
        sqlite3_finalize(st);
        return DB_OK;
    }
    sqlite3_finalize(st);
    if (rc == SQLITE_DONE)
        return DB_NOT_FOUND;
    record_err("step label select");
    return DB_IO;
}

int db_forget_network(const char *cidr, char *label_out, int label_out_sz)
{
    sqlite3_stmt *st = NULL;
    int rc;

    if (g_state != DB_OK) {
        g_state = db_do_open(1);
        if (g_state != DB_OK)
            return DB_IO;
    }

    if (label_out != NULL && label_out_sz > 0)
        label_out[0] = '\0';
    rc = db_get_label(cidr, label_out, label_out_sz);
    if (rc != DB_OK)
        return rc;

    if (sqlite3_prepare_v2(g_db,
            "DELETE FROM UsedNetworks WHERE cidr = ?1",
            -1, &st, NULL) != SQLITE_OK) {
        record_err("prepare delete");
        return DB_IO;
    }
    sqlite3_bind_text(st, 1, cidr, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE || sqlite3_changes(g_db) < 1) {
        record_err("delete");
        return DB_IO;
    }
    return DB_OK;
}

int db_count(int *out)
{
    sqlite3_stmt *st = NULL;
    int n = 0;

    *out = 0;
    if (g_state != DB_OK) {
        g_state = db_do_open(0);
        if (g_state == DB_EMPTY)
            return DB_OK;            /* no table yet: count = 0 */
        if (g_state != DB_OK)
            return DB_IO;
    }
    if (g_state == DB_EMPTY)
        return DB_OK;                /* count = 0 */

    if (sqlite3_prepare_v2(g_db, "SELECT count(*) FROM UsedNetworks",
                           -1, &st, NULL) != SQLITE_OK) {
        record_err("prepare count");
        return DB_IO;
    }
    if (sqlite3_step(st) == SQLITE_ROW)
        n = (int)sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    *out = n;
    return DB_OK;
}

/* 1 = intersecting network registered, 0 = free, DB_IO = error. */
int db_overlaps_any(unsigned long start, unsigned long end)
{
    int conflicts = 0;

    if (scan_used(start, end, NULL, 0, NULL, 0, &conflicts, NULL) != DB_OK)
        return DB_IO;
    return conflicts > 0 ? 1 : 0;
}

int db_used_by_plan(unsigned long start, unsigned long end,
                    char *buf, int bufsz)
{
    char cidr[32], label[112];
    int found = 0, extra = 0, len;

    if (buf != NULL && bufsz > 0)
        buf[0] = '\0';
    if (scan_used(start, end, cidr, (int)sizeof cidr,
                  label, (int)sizeof label, &found, &extra) != DB_OK)
        return DB_IO;
    if (found == 0)
        return 0;                    /* clean range */

    sqlite3_snprintf(bufsz, buf, "%s (\"%.64s\")", cidr, label);
    if (extra > 0) {
        len = (int)strlen(buf);
        sqlite3_snprintf(bufsz - len, buf + len, " +%d more", extra);
    }
    return 1;
}

int db_list_networks(db_list_fn cb)
{
    sqlite3_stmt *st = NULL;
    const unsigned char *c, *l, *t;
    int rc;

    if (g_state == -1) {
        g_state = db_do_open(0);
        if (g_state != DB_OK)
            return DB_IO;
    }
    if (g_state == DB_EMPTY)
        return DB_OK;                /* callback never fires */

    if (sqlite3_prepare_v2(g_db,
            "SELECT cidr,label,created_at FROM UsedNetworks ORDER BY cidr",
            -1, &st, NULL) != SQLITE_OK) {
        record_err("prepare list");
        return DB_IO;
    }
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        c = sqlite3_column_text(st, 0);
        l = sqlite3_column_text(st, 1);
        t = sqlite3_column_text(st, 2);
        cb(c ? (const char *)c : "",
           l ? (const char *)l : "",
           t ? (const char *)t : "");
    }
    if (rc != SQLITE_DONE) {
        record_err("step list");
        sqlite3_finalize(st);
        return DB_IO;
    }
    sqlite3_finalize(st);
    return DB_OK;
}
