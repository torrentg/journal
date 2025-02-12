# logdb

A simple log-structured database.
It is an embeded database with no dependencies.

Logdb is a simple database with the following characteristics:

* Variable length record type
* Records uniquely identified by a sequential number (seqnum)
* Records are indexed by timestamp (monotonic non-decreasing field)
* There are no other indexes other than seqnum and timestamp.
* Records can be appended, read, and searched
* Records cannot be updated or deleted
* Allows reverting the last entries (rollback)
* Allows removing obsolete entries (purge)
* Supports read-write concurrency (multi-thread)
* Automatic data recovery in case of catastrofic events
* Minimal memory footprint

Use cases:

* Storage engine in a raft library (fault-tolerant distributed applications)
* Storage engine for journal-based apps

## Description

Logdb is essentially an append-only data file (\*.dat) with an index file (\*.idx) used to speed up lookups.
No complex data structures, no sofisticated algorithms, only basic file access.
We rely on the filesystem cache (managed by the operating system) to ensure read performance.

### dat file format

```txt
     header       record1          data1          record2       data2
┌──────┴──────┐┌─────┴─────┐┌────────┴────────┐┌─────┴─────┐┌─────┴─────┐...
  magic number   seqnum1        raw bytes 1      seqnum2     raw bytes 2
  format         timestamp1                      timestamp2
  etc            checksum1                       checksum2
                 length1                         length2
```

### idx file format

```txt
     header      record1       record2
┌──────┴──────┐┌─────┴─────┐┌─────┴─────┐...
  magic number   seqnum1      seqnum2
  format         timestamp1   timestamp2
  etc            pos1         pos2
```

## Usage

Drop [`logdb.h`](logdb.h) and [`logdb.c`](logdb.c) into your project and start using it.

```c
#include "logdb.h"

ldb_db_t db = {0};
ldb_entry_t wentries[MAX_ENTRIES] = {{0}};
ldb_entry_t rentries[MAX_ENTRIES] = {{0}};

ldb_open(&db, "/my/directory", "example", true);

// on write-thread
fill_entries(wentries, MAX_ENTRIES);
ldb_append(&db, wentries, MAX_ENTRIES, NULL);

// on read-thread
ldb_read(&db, 1, rentries, MAX_ENTRIES, NULL);
process_entries(rentries, MAX_ENTRIES);

ldb_free_entries(rentries, MAX_ENTRIES);
ldb_close(&db);
```

Read the function documentation in `logdb.h`.<br/>
See [`example.c`](example.c) for basic function usage.<br/>
See [`performance.c`](performance.c) for concurrent usage.

## Contributors

| Name | Contribution |
|:-----|:-------------|
| [Gerard Torrent](https://github.com/torrentg/) | Initial work<br/>Code maintainer|
| [J_H](https://codereview.stackexchange.com/users/145459/j-h) | [Code review ](https://codereview.stackexchange.com/questions/291660/a-c-header-only-log-structured-database) |
| [Harith](https://codereview.stackexchange.com/users/265278/harith) | [Code review ](https://codereview.stackexchange.com/questions/291660/a-c-header-only-log-structured-database) |

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
