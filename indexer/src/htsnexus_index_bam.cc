// Add a BAM file to the htsnexus index database (creating the index if
// needed). With some future refactoring, this could be generalized for other
// BGZF-based formats (VCF and BCF)

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
#include "htslib/bgzf.h"
#include "htslib/sam.h"
#include "htslib/hfile.h"
#include "sqlite3.h"

using namespace std;

/*************************************************************************************************/

// htsnexus_index_util.cc prototypes
shared_ptr<sqlite3> open_database(const char* db);
string derive_dbid(const char* name_space, const char* accession, const char* format, const char* fn, const char* url);
void insert_htsfile(sqlite3* dbh, const char* dbid, const char* format, const char* name_space,
                    const char* accession, const char* url);

void insert_block_index_meta(sqlite3* dbh, const char* reference, const char* dbid,
                             const string& header, const string& prefix, const string& suffix);
shared_ptr<sqlite3_stmt> prepare_insert_block(sqlite3* dbh);
void insert_block_index_entry(sqlite3_stmt* insert_block_stmt, const char* dbid,
                              const vector<string>& target_names,
                              int64_t block_lo, int64_t block_hi,
                              int tid, int seq_lo, int seq_hi,
                              const string& prefix, const string& suffix);

/*************************************************************************************************/

const string BAM_EOF("\037\213\010\4\0\0\0\0\0\377\6\0\102\103\2\0\033\0\3\0\0\0\0\0\0\0\0\0", 28);

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
    shared_ptr<hFILE> hf(hopen(tmpfn, "r"), &hclose);
    if (!hf) {
        throw runtime_error("opening temp BAM file");
    }

    ssize_t len = hread(hf.get(), buf.get(), bufsize);
    if (len<=0 || !hf->at_eof || hf->has_errno) {
        throw runtime_error("reading temp BAM file; is the header >" + to_string(bufsize) + " bytes?");
    }

    hf.reset();
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
    shared_ptr<bam_hdr_t> header(bam_hdr_read(bgzf.get()), [](bam_hdr_t* h) { bam_hdr_destroy(h); });
    if (!header) {
        throw runtime_error("reading BAM header from " + string(bamfile));
    }

    vector<string> target_names;
    for (int i = 0; i < header->n_targets; i++) {
        target_names.push_back(string(header->target_name[i]));
    }

    string bam_header_bgzf = generate_bam_header_bgzf(header.get());

    // insert the htsfiles_blocks_meta entry
    insert_block_index_meta(dbh, reference, dbid, string(header->text, header->l_text), bam_header_bgzf, BAM_EOF);

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
    int c;
    while ((c = bam_read1(bgzf.get(), record.get())) > 0) {
        block_count++;

        if (tid >= -1 && tid != record->core.tid) {
            // transitioning from one reference sequence (tid) to the next;
            // record the range seen in this BGZF block so far
            if (record->core.tid != -1 && record->core.tid < tid) {
                throw runtime_error("BAM not sorted (by sequence) at record " + to_string(block_count));
            }
            if (lo > -1) {
                block_ranges.push_back(make_tuple(tid, lo, hi));
            }
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
            if (bgzf->block_offset != 0) {
                // it appears the last bam1_t record was split across two BGZF blocks
                throw runtime_error("Unable to index this file due to bam1_t/BGZF block misalignment. Please report this file upstream.");
            }
            block_ranges.push_back(make_tuple(tid, lo, hi));
            lo = hi = -1;
            for (const auto& r : block_ranges) {
                insert_block_index_entry(insert_block_stmt.get(), dbid, target_names,
                                         last_block_address, bgzf->block_address,
                                         get<0>(r), get<1>(r), get<2>(r),
                                         string(), string());
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
    string dbid = derive_dbid(name_space, accession, "bam", fn, url);

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
