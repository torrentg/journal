/*
MIT License

logdb -- A simple log-structured database.
<https://github.com/torrentg/logdb>

Copyright (c) 2024 Gerard Torrent <gerard@generacio.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef LOGDB_H
#define LOGDB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * Logdb is a simple database with the following characteristics:
 *   - Variable length record type
 *   - Records uniquely identified by a sequential number (seqnum)
 *   - Records are indexed by timestamp (monotonic non-decreasing field)
 *   - There are no other indexes other than seqnum and timestamp.
 *   - Records can be appended, read, and searched
 *   - Records can not be updated nor deleted
 *   - Allows to revert last entries (rollback)
 *   - Allows to remove obsolete entries (purge)
 *   - Read-write concurrency supported (multi-thread)
 *   - Automatic data recovery on catastrofic event
 *   - Minimal memory footprint
 * 
 * Use cases:
 *   - Storage engine in a raft library (fault-tolerant distributed applications)
 *   - Storage engine for journal-based apps
 * 
 * Basically, logdb is an append-only data file (\*.dat) with 
 * an index file (\*.idx) used to speed up lookups. No complex 
 * data structures, no sofisticated algorithms, only basic file
 * access. We rely on the filesystem cache (managed by the operating 
 * system) to ensure read performance.
 * 
 * dat file format
 * ---------------
 * 
 * Contains the database data.
 * 
 * @see struct ldb_header_dat_t
 * @see struct ldb_record_dat_t
 * 
 *     header        record1          data1          record2       data2
 * ┌──────┴──────┐┌─────┴─────┐┌────────┴────────┐┌─────┴─────┐┌─────┴─────┐...
 *   magic number   seqnum1        raw bytes 1      seqnum2     raw bytes 2
 *   format         timestamp1                      timestamp2
 *   etc            checksum1                       checksum2
 *                  length1                         length2
 * 
 * idx file format
 * ---------------
 * 
 * Used to search database entries.
 * If idx file does not exist, it is rebuilt from the data.
 * 
 * @see struct ldb_header_idx_t
 * @see struct ldb_record_idx_t
 * 
 *      header      record1       record2
 * ┌──────┴──────┐┌─────┴─────┐┌─────┴─────┐...
 *   magic number   seqnum1      seqnum2
 *   format         timestamp1   timestamp2
 *   etc            pos1         pos2
 * 
 * We can access directly any record by seqnum because:
 *  - we know the first seqnum in the db
 *  - we know the last seqnum in the db
 *  - idx header has fixed size
 *  - all idx records have same size
 *
 * We use the binary search method over the index records to search data by timestamp.
 * In all cases we rely on the system file caches to store data in memory.
 * 
 * Concurrency
 * ---------------
 * 
 * Main goal: append() function not blocked.
 * File write ops are done with [dat|idx]_fp.
 * File read ops are done with [dat|idx]_fd.
 * 
 * We use 2 mutex:
 *   - data mutex: grants data integrity ([first|last]_[seqnum|timestamp])
 *                 reduced scope (variables update)
 *   - file mutex: grants that no reads are done during destructive writes
 *                 extended scope (function execution)
 * 
 *                             File     Data
 * Thread        Function      Mutex    Mutex   Notes
 * -------------------------------------------------------------------
 *               ┌ open()         -       -     Init mutexes, create FILE's used to write and fd's used to read
 *               ├ append()       -       W     dat and idx files flushed at the end. State updated after flush.
 * thread-write: ┼ rollback()     W       W     
 *               ├ purge()        W       W     
 *               └ close()        -       -     Destroy mutexes, close files
 *               ┌ stats()        R       R     
 * thread-read:  ┼ read()         R       R     
 *               └ search()       R       R     
 */

#define LDB_VERSION_MAJOR          1
#define LDB_VERSION_MINOR          1
#define LDB_VERSION_PATCH          0

#define LDB_OK                     0
#define LDB_ERR                   -1
#define LDB_ERR_ARG               -2
#define LDB_ERR_MEM               -3
#define LDB_ERR_PATH              -4
#define LDB_ERR_NAME              -5
#define LDB_ERR_OPEN_DAT          -6
#define LDB_ERR_READ_DAT          -7
#define LDB_ERR_WRITE_DAT         -8
#define LDB_ERR_OPEN_IDX          -9
#define LDB_ERR_READ_IDX         -10
#define LDB_ERR_WRITE_IDX        -11
#define LDB_ERR_FMT_DAT          -12
#define LDB_ERR_FMT_IDX          -13
#define LDB_ERR_ENTRY_SEQNUM     -14
#define LDB_ERR_ENTRY_TIMESTAMP  -15
#define LDB_ERR_ENTRY_METADATA   -16
#define LDB_ERR_ENTRY_DATA       -17
#define LDB_ERR_NOT_FOUND        -18
#define LDB_ERR_TMP_FILE         -19
#define LDB_ERR_CHECKSUM         -20

#ifdef __cplusplus
extern "C" {
#endif

struct ldb_impl_t;
typedef struct ldb_impl_t ldb_db_t;

typedef enum ldb_search_e {
    LDB_SEARCH_LOWER,             // Search first entry having timestamp not less than value.
    LDB_SEARCH_UPPER              // Search first entry having timestamp greater than value.
} ldb_search_e;

typedef struct ldb_state_t {
    uint64_t seqnum1;             // Initial seqnum (0 means no entries)
    uint64_t timestamp1;          // Timestamp of the first entry
    uint64_t seqnum2;             // Ending seqnum (0 means no entries)
    uint64_t timestamp2;          // Timestamp of the last entry
} ldb_state_t;

typedef struct ldb_entry_t {
    uint64_t seqnum;
    uint64_t timestamp;
    uint32_t metadata_len;
    uint32_t data_len;
    void *metadata;
    void *data;
} ldb_entry_t;

typedef struct ldb_stats_t {
    uint64_t min_seqnum;
    uint64_t max_seqnum;
    uint64_t min_timestamp;
    uint64_t max_timestamp;
    size_t num_entries;
    size_t data_size;
    size_t index_size;
} ldb_stats_t;

/**
 * Returns ldb library version.
 * @return Library version (semantic version, ex. 1.0.4).
 */
const char * ldb_version(void);

/**
 * Returns the textual description of the ldb error code.
 * 
 * @param[in] errnum Code error.
 * @return Textual description.
 */
const char * ldb_strerror(int errnum);

/**
 * Deallocates the memory pointed by the entry.
 * It do not dealloc the entry itself.
 * 
 * Use this function to deallocate entries returned by ldb_read().
 * Updates entry pointers to NULL and lengths to 0.
 * 
 * @param[in,out] entry Entry to dealloc data (if NULL does nothing).
 */
void ldb_free_entry(ldb_entry_t *entry);

/**
 * Deallocates the memory of an array of entries.
 * 
 * This is an utility function that calls ldb_free_entry() for
 * each array item.
 * 
 * @param[in] entries Array of entries (if NULL does nothing).
 * @param[in] len Number of entries.
 */
void ldb_free_entries(ldb_entry_t *entries, size_t len);

/**
 * Allocate a new ldb_db_t (opaque) object.
 * 
 * @return Allocated object or NULL if no memory.
 */
ldb_db_t * ldb_alloc(void);

/**
 * Deallocates a ldb_db_t object.
 * 
 * @param obj Object to deallocate.
 */
void ldb_free(ldb_db_t *obj);

/**
 * Open a database.
 * 
 * Creates database files (dat+idx) if they not exists.
 * Update index file if incomplete (not flushed + crash).
 * Rebuild index file when corrupted or not found.
 * 
 * By default fsync flag is disabled. Use function ldb_set_fsync_mode()
 * to change this behavior.
 * 
 * @param[in,out] obj Uninitialized database object.
 * @param[in] path Directory where database files are located.
 * @param[in] name Database name (chars allowed: [a-ZA-Z0-9_], max length = 32).
 * @param[in] check Check database files (true|false).
 * @return Error code (0 = OK). On error db is closed properly (ldb_close not required).
 *         You can check errno value to get additional error details.
 */
int ldb_open(ldb_db_t *obj, const char *path, const char *name, bool check);

/**
 * Close a database.
 * 
 * Close open files and release allocated memory.
 * 
 * @param[in,out] obj Database to close.
 * @return Return code (0 = OK).
 */
int ldb_close(ldb_db_t *obj);

/**
 * Enable or disable the fsync mode for the database.
 * 
 * When fsync mode is enabled, all data written to the database files is flushed to disk,
 * ensuring that changes are persisted in case of a system crash or power failure.
 * When fsync mode is disabled, data may not be immediately flushed to disk, which can
 * improve performance but at the risk of data loss in case of a crash.
 * 
 * @param[in] obj Database to configure.
 * @param[in] fsync Mode to set (true=enable, false=disable).
 * @return Error code (0 = OK).
 */
int ldb_set_fsync_mode(ldb_db_t *obj, bool fsync);

/**
 * Append entries to the database.
 * 
 * Entries are identified by its seqnum. 
 * First entry can have any seqnum distinct than 0.
 * The rest of entries must have correlative values (no gaps).
 * 
 * Each entry has an associated timestamp (distinct than 0). 
 * If no timestamp value is provided (0 value), logdb populates this 
 * field with milliseconds from epoch time. Otherwise, the meaning and 
 * units of this field are user-defined. Logdb verifies that the timestamp 
 * is equal to or greater than the timestamp of the preceding entry. 
 * It is legit for multiple records to have an identical timestamp 
 * because they were logged within the timestamp granularity.
 * 
 * This function is not 'atomic'. Entries are appended sequentially. 
 * On error (ex. disk full) writed entries are flushed and remaining entries
 * are reported as not writed (see num return argument).
 * 
 * Seqnum values:
 *   - equals to 0 -> system assigns the sequential value.
 *   - distinct than 0 -> system check that it is the next value.
 * 
 * Timestamp values:
 *   - equals to 0: system assigns the current UTC epoch time (in millis).
 *   - distinct than 0 -> system check that is bigger or equal to previous timestamp.
 * 
 * File operations:
 *   - Data file is updated and flushed.
 *   - Index file is updated but not flushed.
 * 
 * Memory pointed by entries is not modified and can be deallocated after function call.
 * 
 * @param[in] obj Database to modify.
 * @param[in,out] entries Entries to append to the database. Memory pointed 
 *                  by each entry is not modified. Seqnum and timestamp
 *                  are updated if they have value 0.
 *                  User must reset pointers before reuse.
 * @param[in] len Number of entries to append.
 * @param[out] num Number of entries appended (can be NULL).
 * @return Error code (0 = OK).
 */
int ldb_append(ldb_db_t *obj, ldb_entry_t *entries, size_t len, size_t *num);

/**
 * Read num entries starting from seqnum (included).
 * 
 * @param[in] obj Database to use.
 * @param[in] seqnum Initial sequence number.
 * @param[out] entries Array of entries (min length = len).
 *                  These entries are uninitialized (with NULL pointers) or entries 
 *                  previously initialized by ldb_read() function. In this case, the 
 *                  allocated memory is reused and will be reallocated if not enough.
 *                  Use ldb_free_entry() to dealloc returned entries.
 * @param[in] len Number of entries to read.
 * @param[out] num Number of entries read (can be NULL). If num less than 'len' means 
 *                  that last record was reached. Unused entries are signaled with 
 *                  seqnum = 0.
 * @return Error code (0 = OK).
 */
int ldb_read(ldb_db_t *obj, uint64_t seqnum, ldb_entry_t *entries, size_t len, size_t *num);

/**
 * Return statistics between seqnum1 and seqnum2 (both included).
 * 
 * @param[in] obj Database to use.
 * @param[in] seqnum1 First sequence number.
 * @param[in] seqnum2 Second sequence number (greater or equal than seqnum1).
 * @param[out] stats Uninitialized statistics.
 * @return Error code (0 = OK).
 */
int ldb_stats(ldb_db_t *obj, uint64_t seqnum1, uint64_t seqnum2, ldb_stats_t *stats);

/**
 * Search the seqnum corresponding to the given timestamp.
 * 
 * Use the binary search algorithm over the index file.
 * 
 * @param[in] obj Database to use.
 * @param[in] ts Timestamp to search.
 * @param[in] mode Search mode.
 * @param[out] seqnum Resulting seqnum (distinct than NULL, 0 = NOT_FOUND).
 * @return Error code (0 = OK).
 */
int ldb_search(ldb_db_t *obj, uint64_t ts, ldb_search_e mode, uint64_t *seqnum);

/**
 * Remove all entries greater than seqnum.
 * 
 * File operations:
 *   - Index file is updated (zero'ed top-to-bottom) and flushed.
 *   - Data file is updated (zero'ed bottom-to-top) and flushed.
 * 
 * @param[in] obj Database to update.
 * @param[in] seqnum Sequence number from which records are removed (seqnum=0 removes all content).
 * @return Number of removed entries, or error if negative.
 */
long ldb_rollback(ldb_db_t *obj, uint64_t seqnum);

/**
 * Remove all entries less than seqnum.
 * 
 * This function is expensive because recreates the dat and idx files.
 * 
 * To prevent data loss in case of outage we do:
 *   - A tmp data file is created.
 *   - Preserved records are copied from dat file to tmp file.
 *   - Tmp, dat and idx are closed
 *   - Idx file is removed
 *   - Tmp file is renamed to dat
 *   - Dat file is opened
 *   - Idx file is rebuilt
 * 
 * @param[in] obj Database to update.
 * @param[in] seqnum Sequence number up to which records are removed.
 * @return Number of removed entries, or error if negative.
 */
long ldb_purge(ldb_db_t *obj, uint64_t seqnum);

#ifdef __cplusplus
}
#endif

#endif /* LOGDB_H */
