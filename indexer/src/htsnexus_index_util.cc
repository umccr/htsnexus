#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include "htslib/sam.h"
#include "sqlite3.h"

using namespace std;

/*************************************************************************************************/

const char* schema =
    "begin;"
    "create table if not exists htsfiles (_dbid text primary key, format text not null, \
        namespace text not null, accession text not null, url text not null, \
        file_size integer check(file_size is null or file_size > 0));"
    "create unique index if not exists htsfiles_namespace_accession on htsfiles(namespace,accession,format);"
    "create table if not exists htsfiles_blocks_meta (_dbid text primary key, reference text not null, \
        header text not null, slice_prefix blob, slice_suffix blob, \
        foreign key(_dbid) references htsfiles(_dbid));"
    "create table if not exists htsfiles_blocks (_dbid text not null, \
        byteLo integer not null check(byteLo >= 0), byteHi integer not null check(byteHi > byteLo), \
        seq text check(seq is not null or (seqLo is null and seqHi is null)), \
        seqLo integer check(seq is null or (seqLo is not null and seqLo >= 0)), \
        seqHi integer check(seq is null or (seqHi is not null and seqHi >= seqLo)), \
        block_prefix blob, block_suffix blob, foreign key(_dbid) references htsfiles_index_meta(_dbid));"
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

    shared_ptr<sqlite3> dbh(raw, &sqlite3_close);

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
// permit naive merging of databases indexing different sets of files
string derive_dbid(const char* name_space, const char* accession, const char* format, const char* fn, const char* url) {
    // TODO: would be nice to use a hash of the file contents
    ostringstream dbid;
    dbid << name_space << ":" << accession << ":" << format;
    return dbid.str();
}

// insert the core entry in the htsfiles table. set file_size to negative if
// that information is not known.
void insert_htsfile(sqlite3* dbh, const char* dbid, const char* format, const char* name_space,
                    const char* accession, const char* url, ssize_t file_size) {

	ostringstream msg;
	msg << "Inserting BAM index info to SQLite db: " << dbid;
    sqlite3_stmt *raw = 0;
    if (sqlite3_prepare_v2(dbh, "insert into htsfiles values(?,?,?,?,?,?)", -1, &raw, 0)) {
        throw runtime_error("Failed to prepare statement: insert into htsfiles...\n");
    }
    shared_ptr<sqlite3_stmt> stmt(raw, &sqlite3_finalize);

    if (sqlite3_bind_text(stmt.get(), 1, dbid, -1, 0) ||
        sqlite3_bind_text(stmt.get(), 2, format, -1, 0) ||
        sqlite3_bind_text(stmt.get(), 3, name_space, -1, 0) ||
        sqlite3_bind_text(stmt.get(), 4, accession, -1, 0) ||
        sqlite3_bind_text(stmt.get(), 5, url, -1, 0) ||
        (file_size >= 0 ? sqlite3_bind_int64(stmt.get(), 6, file_size)
                        : sqlite3_bind_null(stmt.get(), 6))) {
        throw runtime_error("Failed to bind: insert into htsfiles...");
    }

    int c = sqlite3_step(stmt.get());
    if (c != SQLITE_DONE) {
        ostringstream msg;
        msg << "Error inserting htsfiles entry: " << sqlite3_errstr(c);
        throw runtime_error(msg.str());
    }
}

// insert the index metadata entry for a file into htsfiles_blocks_meta
void insert_block_index_meta(sqlite3* dbh, const char* reference, const char* dbid,
                             const string& header, const string& prefix, const string& suffix) {
    sqlite3_stmt *raw = 0;
    if (sqlite3_prepare_v2(dbh, "insert into htsfiles_blocks_meta values(?,?,?,?,?)", -1, &raw, 0)) {
        throw runtime_error("Failed to prepare statement: insert into htsfiles_blocks_meta...\n");
    }
    shared_ptr<sqlite3_stmt> stmt(raw, &sqlite3_finalize);

    if (sqlite3_bind_text(stmt.get(), 1, dbid, -1, 0) ||
        sqlite3_bind_text(stmt.get(), 2, reference, -1, 0) ||
        sqlite3_bind_text(stmt.get(), 3, header.c_str(), header.size(), 0) ||
        sqlite3_bind_null(stmt.get(), 4) ||
        sqlite3_bind_null(stmt.get(), 5)) {
        throw runtime_error("Failed to bind: insert into htsfiles_blocks_meta...");
    }

    if ((prefix.size() && sqlite3_bind_blob(stmt.get(), 4, prefix.c_str(), prefix.size(), 0)) ||
        (suffix.size() && sqlite3_bind_blob(stmt.get(), 5, suffix.c_str(), suffix.size(), 0))) {
        throw runtime_error("Failed to bind: insert into htsfiles_blocks_meta...");
    }

    int c = sqlite3_step(stmt.get());
    if (c != SQLITE_DONE) {
        ostringstream msg;
        msg << "Error inserting htsfiles_blocks_meta entry: " << sqlite3_errstr(c);
        throw runtime_error(msg.str());
    }
}

// prepare the statement to insert an entry into htsfiles_blocks
shared_ptr<sqlite3_stmt> prepare_insert_block(sqlite3* dbh) {
    sqlite3_stmt *raw = 0;
    if (sqlite3_prepare_v2(dbh, "insert into htsfiles_blocks values(?,?,?,?,?,?,?,?)", -1, &raw, 0)) {
        throw runtime_error("Failed to prepare statement: insert into htsfiles_blocks...\n");
    }
    return shared_ptr<sqlite3_stmt>(raw, [](sqlite3_stmt* s) { sqlite3_finalize(s); });
}

// insert one entry in htsfiles_blocks, given the prepared statement
void insert_block_index_entry(sqlite3_stmt* insert_block_stmt, const char* dbid,
                              const vector<string>& target_names,
                              int64_t block_lo, int64_t block_hi,
                              int tid, int seq_lo, int seq_hi,
                              const string& prefix, const string& suffix) {
    if (tid < -1 || tid >= (int)target_names.size()) {
        throw runtime_error("Invalid tid in BAM: " + to_string(tid));
    }

    if (sqlite3_bind_text(insert_block_stmt, 1, dbid, -1, 0) ||
        sqlite3_bind_int64(insert_block_stmt, 2, block_lo) ||
        sqlite3_bind_int64(insert_block_stmt, 3, block_hi)) {
        throw runtime_error("Failed to bind: insert into htsfiles_blocks...");
    }
    for (int i = 4; i <= 8; i++) {
        if (sqlite3_bind_null(insert_block_stmt, i)) {
            throw runtime_error("Failed to bind: insert into htsfiles_blocks...");
        }
    }
    if (tid != -1) {
        if (sqlite3_bind_text(insert_block_stmt, 4, target_names.at(tid).c_str(), -1, 0) ||
            sqlite3_bind_int64(insert_block_stmt, 5, seq_lo) ||
            sqlite3_bind_int64(insert_block_stmt, 6, seq_hi)) {
            throw runtime_error("Failed to bind: insert into htsfiles_blocks...");
        }
    }
    if ((prefix.size() && sqlite3_bind_blob(insert_block_stmt, 7, prefix.c_str(), prefix.size(), 0)) ||
        (suffix.size() && sqlite3_bind_blob(insert_block_stmt, 8, suffix.c_str(), suffix.size(), 0))) {
        throw runtime_error("Failed to bind: insert into htsfiles_blocks...");
    }

    int c = sqlite3_step(insert_block_stmt);
    if (c != SQLITE_DONE) {
        ostringstream msg;
        msg << "Error inserting htsfiles_blocks entry: " << sqlite3_errstr(c);
        throw runtime_error(msg.str());
    }

    if (sqlite3_reset(insert_block_stmt)) {
        throw runtime_error("Error resetting statement: insert into htsfiles_blocks...");
    }
}

string bgzf_eof() {
    return string("\037\213\010\4\0\0\0\0\0\377\6\0\102\103\2\0\033\0\3\0\0\0\0\0\0\0\0\0", 28);
}
