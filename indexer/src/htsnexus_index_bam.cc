// Add a BAM file to the htsnexus index database (creating the index if
// needed). This can be generalized for other formats (CRAM, VCF, BCF) in the
// future.

#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include <stdlib.h>
#include <getopt.h>
#include "bgzf.h"
#include "sam.h"
#include "sqlite3.h"

using namespace std;

/*************************************************************************************************/

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

// open the htsnexus index database, or create it if necessary.
shared_ptr<sqlite3> open_database(const char* db) {
    sqlite3* raw;
    int c = sqlite3_open_v2(db, &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 0);
    if (c) {
        ostringstream msg;
        msg << "Error opening/creating database " << db << ": " << sqlite3_errstr(c);
        throw runtime_error(msg.str());
    }

    shared_ptr<sqlite3> dbh(raw, &sqlite3_close_v2);

    char *errmsg = 0;
    c = sqlite3_exec(dbh.get(), schema, 0, 0, &errmsg);
    if (c) {
        ostringstream msg;
        msg << "Error applying schema in database %s" << db;
        if (errmsg) {
            msg << ": " << errmsg;
            sqlite3_free(errmsg);
        }
        throw runtime_error(msg.str());
    }

    return dbh;
}

// derive a database ID for this file; it should be sufficiently unique to
// permit naive merging of databases generated on different sets of files
string derive_dbid(const char* name_space, const char* accession, const char* fn, const char* url) {
    ostringstream dbid;
    dbid << name_space << ":" << accession;
    return dbid.str();
}

// insert the core entry in the htsfiles table
void insert_htsfile(sqlite3* dbh, const char* dbid, const char* format, const char* name_space,
                    const char* accession, const char* url) {

    sqlite3_stmt *raw = 0;
    if (sqlite3_prepare_v2(dbh, "insert into htsfiles values(?,?,?,?,?)", -1, &raw, 0)) {
        throw runtime_error("Failed to prepare statement: insert into htsfiles...\n");
    }
    shared_ptr<sqlite3_stmt> stmt(raw, &sqlite3_finalize);

    if (sqlite3_bind_text(stmt.get(), 1, dbid, -1, 0) ||
        sqlite3_bind_text(stmt.get(), 2, format, -1, 0) ||
        sqlite3_bind_text(stmt.get(), 3, name_space, -1, 0) ||
        sqlite3_bind_text(stmt.get(), 4, accession, -1, 0) ||
        sqlite3_bind_text(stmt.get(), 5, url, -1, 0)) {
        throw runtime_error("Failed to bind: insert into htsfiles...");
    }

    int c = sqlite3_step(stmt.get());
    if (c != SQLITE_DONE) {
        ostringstream msg;
        msg << "Error inserting htsfiles entry: " << sqlite3_errstr(c);
        throw runtime_error(msg.str());
    }
}

/*************************************************************************************************/

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
                cout << usage << endl;
                return 1;
        }
    }

    if (argc-optind != 5) {
        cout << usage << endl;
        return 1;
    }
    const char *db = argv[optind],
               *name_space = argv[optind+1],
               *accession = argv[optind+2],
               *fn = argv[optind+3],
               *url = argv[optind+4];

    /* determine the database ID for this file */
    string dbid = derive_dbid(name_space, accession, fn, url);

    /* open/create the database */
    shared_ptr<sqlite3> dbh = open_database(db);

    /* begin the master transaction */
    if (sqlite3_exec(dbh.get(), "begin", 0, 0, 0)) {
        throw runtime_error("failed to begin transaction...");
    }

    /* insert the basic htsfiles entry */
    insert_htsfile(dbh.get(), dbid.c_str(), "bam", name_space, accession, url);

    if (reference) {
        /* build the block-level range index */
        throw runtime_error("--reference not implemented");
    }

    /* commit the master transaction */
    char *errmsg = 0;
    if (sqlite3_exec(dbh.get(), "commit", 0, 0, &errmsg)) {
        ostringstream msg;
        msg << "Error during commit";
        if (errmsg) {
            msg << ": " << errmsg;
            sqlite3_free(errmsg);
        }
        throw runtime_error(msg.str());
    }

    return 0;
}
