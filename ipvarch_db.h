/* ipvarch_db.h - used-networks SQLite backend for IPVarch */
#ifndef IPVARCH_DB_H
#define IPVARCH_DB_H

#define DB_OK         0   /* success / database usable            */
#define DB_EMPTY      1   /* database usable but has no table yet */
#define DB_IO         2   /* database error (see db_last_error)   */
#define DB_DUPLICATE  3   /* --use-network: cidr already present  */
#define DB_NOT_FOUND  4   /* --forget-network: cidr not present   */

#define DB_LABEL_MAX 64

typedef void (*db_list_fn)(const char *cidr, const char *label,
                           const char *created);

/* Open (or verify) the used-networks database.
 * Resolves $IPVARCH_DB, then $XDG_CONFIG_HOME/ipvarch/networks.db,
 * then ~/.config/ipvarch/networks.db (creating directories in editable
 * mode). Returns DB_OK, DB_EMPTY or DB_IO. */
int db_init(int editable);

/* Database diagnostics (valid even after a failed db_init). */
const char *db_last_error(void);
const char *db_path(void);

/* Close the database connection (called automatically at exit). */

/* Close the database connection (called automatically at exit). */
void db_close(void);

/* Register a network. cidr must be the canonical text form. */
int db_add_network(unsigned long net, int prefix, const char *cidr,
                   const char *label);

/* Remove a network; label_out receives the old label when found. */
int db_forget_network(const char *cidr, char *label_out, int label_out_sz);

/* Fetch the label of a registered cidr (DB_OK / DB_NOT_FOUND / DB_IO). */
int db_get_label(const char *cidr, char *buf, int bufsz);

/* Number of registered networks (DB_OK, or DB_EMPTY => *out = 0). */
int db_count(int *out);

/* 1 when the [start, end] range intersects any registered network,
 * 0 when free, DB_IO on database error. */
int db_overlaps_any(unsigned long start, unsigned long end);

/* Fill buf with a "cidr (\"label\")" summary of what registered
 * network(s) intersect the [start, end] range; returns 1 when a row
 * was rendered, 0 when the range is clean, DB_IO on error. */
int db_used_by_plan(unsigned long start, unsigned long end,
                    char *buf, int bufsz);

/* Iterate registered networks (ordered by cidr) through cb. */
int db_list_networks(db_list_fn cb);

#endif
