// Add a BAM file to the htsnexus index database (creating the index if
// needed). With some future refactoring, this could be generalized for other
// formats (CRAM, VCF, BCF)

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <stdlib.h>
#include <getopt.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include "bgzf.h"
#include "sam.h"
#include "hfile.h"
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
        byteHi integer not null, seq text, seqLo integer, seqHi integer, \
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
string derive_dbid(const char* name_space, const char* accession, const char* fn, const char* url) {
    // TODO: would be nice to use a hash of the file contents
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

// Serialize the BAM header to a BGZF fragment, to which additional BGZF
// blocks containing alignments can be appended. Here be an ugly hack...
string generate_bam_header_bgzf(const bam_hdr_t* header) {
    // open a temp file (descriptor)
    char tmpfn[16];
    strcpy(tmpfn, "/tmp/XXXXXX.bam");
    int tmpfd = mkstemps(tmpfn,4);
    if (tmpfd < 0) {
        throw runtime_error(string("creating temp file") + tmpfn);
    }

    // attach BGZF and write out the BAM header
    BGZF* tmpbgzf = bgzf_dopen(tmpfd, "w");
    if (!tmpbgzf) {
        close(tmpfd);
        throw runtime_error("opening temp BGZF");
    }

    if (bam_hdr_write(tmpbgzf, header)) {
        bgzf_close(tmpbgzf);
        throw runtime_error("writing temp BAM header");
    }

    if (bgzf_close(tmpbgzf)) {
        throw runtime_error("closing temp BAM file");
    }
    // tmpfd is now closed too...

    // read back the temp file contents
    const size_t bufsize = 4194304;
    shared_ptr<void> buf(malloc(bufsize), &free);
    hFILE *hf = hopen(tmpfn, "r");
    if (!hf) {
        throw runtime_error("opening temp BAM file");
    }

    ssize_t len = hread(hf, buf.get(), bufsize);
    if (len<=0 || !hf->at_eof || hf->has_errno) {
        hclose(hf);
        throw runtime_error("reading temp BAM file; is the header >" + to_string(bufsize) + " bytes?");
    }

    hclose(hf);
    unlink(tmpfn);

    // sanity-check the temp BAM file
    if (len <= 28 ||
        memcmp((unsigned char*)buf.get(), "\x1F\x8B", 2) ||
        memcmp((unsigned char*)buf.get() + len - 28, "\037\213\010\4\0\0\0\0\0\377\6\0\102\103\2\0\033\0\3\0\0\0\0\0\0\0\0\0", 28)) {
        throw runtime_error("incomplete temp BAM file");
    }

    // return the header without the EOF marker
    return string((char*) buf.get(), len - 28);
}

// prepare the statement to insert an entry int htsfiles_blocks
shared_ptr<sqlite3_stmt> prepare_insert_block(sqlite3* dbh) {
    sqlite3_stmt *raw = 0;
    if (sqlite3_prepare_v2(dbh, "insert into htsfiles_blocks values(?,?,?,?,?,?)", -1, &raw, 0)) {
        throw runtime_error("Failed to prepare statement: insert into htsfiles_blocks...\n");
    }
    return shared_ptr<sqlite3_stmt>(raw, [](sqlite3_stmt* s) { sqlite3_finalize(s); });
}

// insert one entry in htsfiles_blocks, given the prepared statement
void insert_block_index_entry(sqlite3_stmt* insert_block_stmt, const char* dbid, bam_hdr_t* header,
                              int64_t block_lo, int64_t block_hi,
                              int tid, int seq_lo, int seq_hi) {
    if (tid < -1 || tid >= header->n_targets) {
        throw new runtime_error("Invalid tid in BAM: " + to_string(tid));
    }

    if (sqlite3_bind_text(insert_block_stmt, 1, dbid, -1, 0) ||
        sqlite3_bind_int64(insert_block_stmt, 2, block_lo) ||
        sqlite3_bind_int64(insert_block_stmt, 3, block_hi)) {
        throw runtime_error("Failed to bind: insert into htsfiles_blocks...");
    }
    if (tid != -1) {
        if (sqlite3_bind_text(insert_block_stmt, 4, header->target_name[tid], -1, 0) ||
            sqlite3_bind_int(insert_block_stmt, 5, seq_lo) ||
            sqlite3_bind_int(insert_block_stmt, 6, seq_hi)) {
            throw runtime_error("Failed to bind: insert into htsfiles_blocks...");
        }
    } else {
        for (int i = 4; i <= 6; i++) {
            if (sqlite3_bind_null(insert_block_stmt, i)) {
                throw runtime_error("Failed to bind: insert into htsfiles_blocks...");
            }
        }
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

// populate the block-level index for the BAM file (htsfiles_blocks_meta and htsfiles_blocks)
unsigned bam_block_index(sqlite3* dbh, const char* reference, const char* dbid, const char* bamfile) {
    // open the BGZF file
    BGZF* _bgzf = bgzf_open(bamfile, "r");
    if (!_bgzf) {
        throw runtime_error("opening " + string(bamfile));
    }
    shared_ptr<BGZF> bgzf(_bgzf, [](BGZF* f) { bgzf_close(f); });

    int64_t last_block_address = 0;
    if (bgzf->block_address != last_block_address) {
        throw runtime_error("Unexpected: first BGZF block address != 0");
    }

    // read the header
    bam_hdr_t* _header = bam_hdr_read(bgzf.get());
    if (!_header) {
        throw runtime_error("reading BAM header from " + string(bamfile));
    }
    shared_ptr<bam_hdr_t> header(_header, [](bam_hdr_t* h) { bam_hdr_destroy(h); });

    string bam_header_bgzf = generate_bam_header_bgzf(header.get());

    // insert the htsfiles_blocks_meta entry
    sqlite3_stmt *raw = 0;
    if (sqlite3_prepare_v2(dbh, "insert into htsfiles_blocks_meta values(?,?,?,?)", -1, &raw, 0)) {
        throw runtime_error("Failed to prepare statement: insert into htsfiles_blocks_meta...\n");
    }
    shared_ptr<sqlite3_stmt> stmt(raw, &sqlite3_finalize);

    if (sqlite3_bind_text(stmt.get(), 1, dbid, -1, 0) ||
        sqlite3_bind_text(stmt.get(), 2, reference, -1, 0) ||
        sqlite3_bind_text(stmt.get(), 3, header->text, header->l_text, 0) ||
        sqlite3_bind_blob(stmt.get(), 4, bam_header_bgzf.c_str(), bam_header_bgzf.size(), 0)) {
        throw runtime_error("Failed to bind: insert into htsfiles_blocks_meta...");
    }

    int c = sqlite3_step(stmt.get());
    if (c != SQLITE_DONE) {
        ostringstream msg;
        msg << "Error inserting htsfiles_blocks_meta entry: " << sqlite3_errstr(c);
        throw runtime_error(msg.str());
    }

    // Now scan the BAM file to populate the block index. This is a bit
    // complicated because we're bookkeeping on two interleaved structures:
    // the series of BGZF blocks, and the runs of records from the same
    // reference sequence (we don't assume the boundaries align)
    auto insert_block_stmt = prepare_insert_block(dbh);
    unsigned block_count = 0;
    // genomic ranges observed in the 'current' BGZF block
    vector<tuple<int,int,int>> block_ranges;
    // 'current' genomic range -- tid starts at -2 because -1 is used for
    // unmapped reads
    int tid = -2, lo = -1, hi = -1;

    last_block_address = bgzf->block_address;
    shared_ptr<bam1_t> record(bam_init1(), &free);
    while ((c = bam_read1(bgzf.get(), record.get())) > 0) {
        block_count++;

        if (tid >= -1 && tid != record->core.tid) {
            // transitioning from one reference sequence (tid) to the next;
            // record the range seen in this BGZF block so far
            if (record->core.tid != -1 && record->core.tid < tid) {
                throw runtime_error("BAM not sorted (by sequence) at record " + to_string(block_count));
            }
            block_ranges.push_back(make_tuple(tid, lo, hi));
            lo = hi = -1;
        }
        tid = record->core.tid;
        if (tid < -1) {
            throw runtime_error("BAM invalid tid at record " + to_string(block_count));
        }
        if (tid != -1) {
            // update genomic lo & hi to include this record's range
            if (record->core.pos < lo) {
                throw runtime_error("BAM not sorted at record " + to_string(block_count));
            }
            if (lo == -1) {
                lo = record->core.pos;
            }
            hi = max(hi, bam_endpos(record.get()));
        }

        if (bgzf->block_address != last_block_address) {
            // that was the last record in a BGZF block; record the current
            // range, and insert the index entries for that block
            if (bgzf->block_address < last_block_address) {
                throw runtime_error("Unexpected BGZF block address");
            }
            block_ranges.push_back(make_tuple(tid, lo, hi));
            lo = hi = -1;
            for (const auto& r : block_ranges) {
                insert_block_index_entry(insert_block_stmt.get(), dbid, header.get(),
                                         last_block_address, bgzf->block_address,
                                         get<0>(r), get<1>(r), get<2>(r));
            }
            block_ranges.clear();
            last_block_address = bgzf->block_address;
        }
    }
    if (c != -1) {
        throw runtime_error("Error reading BAM file, code " + to_string(c));
    }
    if (lo != -1 || !block_ranges.empty()) {
        // we assume that bgzf->block_address is updated in such a way that,
        // by this point, we'll already have inserted the index entries for
        // the last non-empty block
        throw runtime_error("Truncated BAM or unexpected BGZF/BAM reader behavior");
    }

    return block_count;
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

    string reference;

    int c;
    while (-1 != (c = getopt_long(argc, argv, "hr:", long_options, 0))) {
        switch (c) {
            case 'r':
                reference = optarg;
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

    // determine the database ID for this file
    string dbid = derive_dbid(name_space, accession, fn, url);

    // open/create the database
    shared_ptr<sqlite3> dbh = open_database(db);

    // begin the master transaction
    if (sqlite3_exec(dbh.get(), "begin", 0, 0, 0)) {
        throw runtime_error("failed to begin transaction...");
    }

    // insert the basic htsfiles entry
    insert_htsfile(dbh.get(), dbid.c_str(), "bam", name_space, accession, url);

    if (!reference.empty()) {
        // build the block-level range index
        bam_block_index(dbh.get(), reference.c_str(), dbid.c_str(), fn);
    }

    // commit the master transaction
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
