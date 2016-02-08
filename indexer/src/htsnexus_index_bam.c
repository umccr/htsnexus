/* Add a BAM file to the htsnexus index database (creating the index if
needed). This can be generalized for other formats (CRAM, VCF, BCF) in the
future. */

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include "sam.h"
#include "sqlite3.h"

const char* schema = 
    "begin;"
    "create table if not exists htsfiles (_dbid text primary key, format text not null, \
        namespace text not null, accession text not null, url text not null);"
    "create unique index if not exists htsfiles_namespace_accession on htsfiles(namespace,accession);"
    "create table if not exists htsfiles_blocks_meta (_dbid text primary key, reference text not null, \
        header text not null, bamHeaderBGZF blob not null, \
        foreign key(_dbid) references htsfiles(_dbid));"
    "create table if not exists htsfiles_blocks (_dbid text not null, byteLo integer not null, \
        byteHi integer not null, seq text, seqLo integer not null, seqHi integer not null, \
        foreign key(_dbid) references htsfiles_index_meta(_dbid));"
    "create index if not exists htsfiles_blocks_index1 on htsfiles_blocks(_dbid,seq,seqLo,seqHi);"
    "create index if not exists htsfiles_blocks_index2 on htsfiles_blocks(_dbid,seq,seqHi);"
    "commit";

/* open the htsnexus index database, or create it if necessary. */
int open_database(const char* db, sqlite3 **dbh) {
    int c;
    c = sqlite3_open_v2(db, dbh, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
    if (c) {
        fprintf(stderr, "Error opening/creating database %s: %s\n", db, sqlite3_errstr(c));
        return c;
    }

    char *errmsg = 0;
    c = sqlite3_exec(*dbh, schema, 0, 0, &errmsg);
    if (c) {
        if (errmsg) {
            fprintf(stderr, "Error applying schema in database %s: %s\n", db, errmsg);
            sqlite3_free(errmsg);
        }
        sqlite3_close_v2(*dbh);
        return c;
    }

    return 0;
}

/* derive a database ID for this file; it should be sufficiently unique to
permit naive merging of databases generated on different sets of files */
int derive_dbid(const char* namespace, const char* accession, const char* fn, const char* url, char **dbid) {
    *dbid = malloc(strlen(namespace)+strlen(accession)+2);
    sprintf(*dbid,"%s:%s",namespace,accession);
    return 0;
}

/* insert the core entry in the htsfiles table */
int insert_htsfile(sqlite3* dbh, const char* dbid, const char* format, const char* namespace,
                   const char* accession, const char* url) {

    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(dbh, "insert into htsfiles values(?,?,?,?,?)", -1, &stmt, 0)) {
        fprintf(stderr, "Failed to prepare statement: insert into htsfiles...\n");
        return 1;
    }

    if (sqlite3_bind_text(stmt, 1, dbid, -1, 0) ||
        sqlite3_bind_text(stmt, 2, format, -1, 0) ||
        sqlite3_bind_text(stmt, 3, namespace, -1, 0) ||
        sqlite3_bind_text(stmt, 4, accession, -1, 0) ||
        sqlite3_bind_text(stmt, 5, url, -1, 0)) {
        fprintf(stderr, "Failed to bind: insert into htsfiles...\n");
        sqlite3_finalize(stmt);
        return 1;
    }

    int c = sqlite3_step(stmt);
    if (c != SQLITE_DONE) {
        fprintf(stderr, "Error inserting htsfiles entry: %s\n", sqlite3_errstr(c));
        sqlite3_finalize(stmt);
        return 1;
    }

    return 0;
}

const char* usage =
    "htsnexus_index_bam [options] <index.db> <namespace> <accession> <local_file> <url>\n"
    "  index.db    SQLite3 database (will be created if nonexistent)\n"
    "  namespace   accession namespace\n"
    "  accession   accession identifier\n"
    "  local_file  filename to local copy of BAM\n"
    "  url         BAM URL to serve to clients\n"
    "The BAM file is added to the database (without a block-level range index)\n"
    "based on the above information.\n"
    "Options:\n"
    "  --reference <id>  generate the block-level range index and associate it with\n"
    "                    this (arbitrary, server-specific) reference genome ID\n"
;

int main(int argc, char* argv[]) {
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"reference", required_argument, 0, 'r'}
    };

    char *reference = 0;

    int c;
    while (-1 != (c = getopt_long(argc, argv, "hr:", long_options, 0))) {
        switch (c) {
            case 'r':
                reference = argv[optind];
                break;
            default:
                printf("%s", usage);
                return 1;
        }
    }

    if (argc-optind != 5) {
        printf("%s", usage);
        return 1;
    }
    const char *db = argv[optind],
               *namespace = argv[optind+1],
               *accession = argv[optind+2],
               *fn = argv[optind+3],
               *url = argv[optind+4];

    /* determine the database ID for this file */
    char *dbid = 0;
    if (derive_dbid(namespace, accession, fn, url, &dbid)) {
        return 1;
    }

    /* open/create the database */
    sqlite3 *dbh = 0;
    if (open_database(db, &dbh)) {
        return 1;
    }

    /* begin the master transaction */
    if (sqlite3_exec(dbh, "begin", 0, 0, 0)) {
        fprintf(stderr, "failed to begin transaction...\n");
        sqlite3_close_v2(dbh);
        return 1;
    }

    /* insert the basic htsfiles entry */
    if (insert_htsfile(dbh, dbid, "bam", namespace, accession, url)) {
        sqlite3_close_v2(dbh);
        return 1;
    }

    if (reference) {
        /* build the block-level range index */
        fprintf(stderr, "--reference not implemented\n");
        return 1;
    }

    /* commit the master transaction */
    char *errmsg = 0;
    if (sqlite3_exec(dbh, "commit", 0, 0, &errmsg)) {
        fprintf(stderr, "Error during commit: %s\n", errmsg);
        sqlite3_free(errmsg);
        sqlite3_close_v2(dbh);
        return 1;
    }

    sqlite3_close_v2(dbh);
    free(dbid);

    return 0;
}
