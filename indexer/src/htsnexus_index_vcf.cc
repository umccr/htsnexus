// Add a VCF file to the htsnexus index database (creating the index if
// needed). The VCF must have been compressed using bgzip_lines.

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <stdlib.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include "htslib/bgzf.h"
#include "htslib/vcf.h"
#include "htslib/hfile.h"
#include "sqlite3.h"

using namespace std;

/*************************************************************************************************/

// htsnexus_index_util.cc prototypes
shared_ptr<sqlite3> open_database(const char* db);
string derive_dbid(const char* name_space, const char* accession, const char* format, const char* fn, const char* url);
void insert_htsfile(sqlite3* dbh, const char* dbid, const char* format, const char* name_space,
                    const char* accession, const char* url, ssize_t file_size);

void insert_block_index_meta(sqlite3* dbh, const char* reference, const char* dbid,
                             const string& header, const string& prefix, const string& suffix);
shared_ptr<sqlite3_stmt> prepare_insert_block(sqlite3* dbh);
void insert_block_index_entry(sqlite3_stmt* insert_block_stmt, const char* dbid,
                              const vector<string>& target_names,
                              int64_t block_lo, int64_t block_hi,
                              int tid, int seq_lo, int seq_hi,
                              const string& prefix, const string& suffix);
string bgzf_eof();

/*************************************************************************************************/

shared_ptr<bcf_hdr_t> read_vcf_header(const char* filename) {
    shared_ptr<vcfFile> vcffile(vcf_open(filename, "r"), [](vcfFile* f) { vcf_close(f); });
    if (!vcffile) {
        throw runtime_error("opening " + string(filename));
    }
    if (vcffile->format.compression != bgzf) {
        throw runtime_error("must be compressed with bgzip_lines: " + string(filename));
    }

    // read the header
    shared_ptr<bcf_hdr_t> header(vcf_hdr_read(vcffile.get()), [](bcf_hdr_t* h) { bcf_hdr_destroy(h); });
    if (!header) {
        throw runtime_error("reading VCF header from " + string(filename));
    }
    return header;
}

// Serialize the VCF header either in plain text or to a BGZF fragment to which
// additional BGZF blocks can be appended.
string generate_vcf_header(const bcf_hdr_t* header, bool bgzf_fragment) {
    // open a temp file (descriptor)
    char tmpfn[16];
    strcpy(tmpfn, "/tmp/XXXXXX.vcf.gz");
    int tmpfd = mkstemps(tmpfn,7);
    if (tmpfd < 0) {
        throw runtime_error(string("creating temp file") + tmpfn);
    }

    // attach htsFile and write out the header
    hFILE *tmphfile = hdopen(tmpfd, "w");
    if (!tmphfile) {
        close(tmpfd);
        throw runtime_error("opening temp hFile");
    }
    htsFile *tmphtsfile = hts_hopen(tmphfile, tmpfn, bgzf_fragment ? "wz" : "w");
    if (!tmphtsfile) {
        hclose(tmphfile);
        throw runtime_error("opening temp htsFile");
    }

    if (vcf_hdr_write(tmphtsfile, header)) {
        hts_close(tmphtsfile);
        throw runtime_error("writing temp VCF header");
    }

    if (hts_close(tmphtsfile)) {
        throw runtime_error("closing temp VCF file");
    }
    // tmpfd is now closed too...

    // read back the temp file contents
    const size_t bufsize = 4194304;
    shared_ptr<void> buf(malloc(bufsize), &free);
    shared_ptr<hFILE> hf(hopen(tmpfn, "r"), &hclose);
    if (!hf) {
        throw runtime_error("opening temp VCF file");
    }

    ssize_t len = hread(hf.get(), buf.get(), bufsize);
    if (len<=0 || !hf->at_eof || hf->has_errno) {
        throw runtime_error("reading temp VCF file; is the header >" + to_string(bufsize) + " bytes?");
    }

    hf.reset();
    unlink(tmpfn);

    // sanity-check the temp VCF file
    if (!bgzf_fragment) {
        string line1("##fileformat=VCF");
        if (len <= line1.size() ||
            memcmp((unsigned char*) buf.get(), line1.c_str(), line1.size()) ||
            ((char*)buf.get())[len-1] != '\n') {
            throw runtime_error("ill-formed temp VCF file");
        }
        return string((char*) buf.get(), len);;
    }

    if (len <= 28 ||
        memcmp((unsigned char*)buf.get(), "\x1F\x8B", 2) ||
        memcmp((unsigned char*)buf.get() + len - 28, "\037\213\010\4\0\0\0\0\0\377\6\0\102\103\2\0\033\0\3\0\0\0\0\0\0\0\0\0", 28)) {
        throw runtime_error("incomplete temp VCF file");
    }

    // return the header without the EOF marker
    return string((char*) buf.get(), len - 28);
}

// populate the block-level index for the VCF file (htsfiles_blocks_meta and htsfiles_blocks)
unsigned vcf_block_index(sqlite3* dbh, const char* reference, const char* dbid, const char* filename) {
    // read the header
    shared_ptr<bcf_hdr_t> header = read_vcf_header(filename);

    int nseqs = -1;
    shared_ptr<const char*> _seqnames(bcf_hdr_seqnames(header.get(), &nseqs), free);
    if (!_seqnames || nseqs <= 0) {
        throw runtime_error("reading sequence names " + string(filename));
    }
    vector<string> seqnames;
    for (int i = 0; i < nseqs; i++) {
        const char* seqname_i = _seqnames.get()[i];
        seqnames.push_back(string(seqname_i));
    }

    string vcf_header_txt = generate_vcf_header(header.get(), false);
    string vcf_header_bgzf = generate_vcf_header(header.get(), true);

    // insert the htsfiles_blocks_meta entry
    insert_block_index_meta(dbh, reference, dbid, vcf_header_txt, vcf_header_bgzf, bgzf_eof());

    // re-open as BGZF
    shared_ptr<BGZF> bgzf(bgzf_open(filename, "r"), [](BGZF* f) { bgzf_close(f); });
    if (!bgzf) {
        throw runtime_error("opening BGZF " + string(filename));
    }
    if (bgzf->block_address != 0) {
        throw runtime_error("Unexpected: first BGZF block address != 0");
    }

    // Now scan the VCF file to populate the block index. This is a bit
    // complicated because we're bookkeeping on two interleaved structures:
    // the series of BGZF blocks, and the runs of records from the same
    // reference sequence (we don't assume the boundaries align)
    auto insert_block_stmt = prepare_insert_block(dbh);
    unsigned record_count = 0;
    int64_t last_block_address = 0;
    // genomic ranges observed in the 'current' BGZF block
    vector<tuple<int,int,int>> block_ranges;
    // 'current' genomic range
    int rid = -1, lo = -1, hi = -1;

    shared_ptr<kstring_t> line((kstring_t*) calloc(1, sizeof(kstring_t)),
                               [](kstring_t* s) { if (s->s) free(s->s); free(s); });
    shared_ptr<bcf1_t> record(bcf_init(), &bcf_destroy);
    int c;
    while ((c = bgzf_getline(bgzf.get(), '\n', line.get())) >= 0) {
        if (line->l == 0 || line->s[0] == '#') {
            // skip header
            last_block_address = bgzf->block_address;
            continue;
        }
        if (last_block_address == 0 || bgzf->block_address == 0) {
            // the first record must be in a new BGZF block after the header
            throw runtime_error("You must recompress this file using bgzip_lines. (First record must begin in a new BGZF block)");
        }
        record_count++;

        if (vcf_parse(line.get(), header.get(), record.get())) {
            throw runtime_error("Error reading VCF line " + to_string(record_count));
        }

        if (rid >= 0 && rid != record->rid) {
            // transitioning from one reference sequence (rid) to the next;
            // record the range seen in this BGZF block so far
            if (record->rid != -1 && record->rid < rid) {
                throw runtime_error("VCF not sorted (by sequence) at record " + to_string(record_count));
            }
            if (lo > -1) {
                block_ranges.push_back(make_tuple(rid, lo, hi));
            }
            lo = hi = -1;
        }
        rid = record->rid;
        if (rid < 0 || rid >= seqnames.size()) {
            throw runtime_error("VCF invalid reference sequence at record " + to_string(record_count));
        }

        // update genomic lo & hi to include this record's range
        if (record->pos < lo) {
            throw runtime_error("VCF not sorted at record " + to_string(record_count));
        }
        if (lo == -1) {
            lo = record->pos;
        }
        hi = max(hi, record->pos+record->rlen);

        if (bgzf->block_address != last_block_address) {
            // that was the last record in a BGZF block; record the current
            // range, and insert the index entries for that block
            if (bgzf->block_address < last_block_address) {
                throw runtime_error("Unexpected BGZF block address");
            }
            if (bgzf->block_offset != 0) {
                // it appears the last record was split across two BGZF blocks
                throw runtime_error("You must recompress this file using bgzip_lines. (Block misalignment)");
            }
            block_ranges.push_back(make_tuple(rid, lo, hi));
            lo = hi = -1;
            for (const auto& r : block_ranges) {
                insert_block_index_entry(insert_block_stmt.get(), dbid, seqnames,
                                         last_block_address, bgzf->block_address,
                                         get<0>(r), get<1>(r), get<2>(r),
                                         string(), string());
            }
            block_ranges.clear();
            last_block_address = bgzf->block_address;
        }
    }
    if (c != -1) {
        throw runtime_error("Error reading VCF file, code " + to_string(c));
    }
    if (lo != -1 || !block_ranges.empty()) {
        // we assume that bgzf->block_address is updated in such a way that,
        // by this point, we'll already have inserted the index entries for
        // the last non-empty block
        throw runtime_error("Truncated VCF or unexpected BGZF/VCF reader behavior");
    }

    return record_count;
}

/*************************************************************************************************/

const char* usage =
    "htsnexus_index_vcf [options] <index.db> <namespace> <accession> <local_file> <url>\n"
    "  index.db    SQLite3 database (will be created if nonexistent)\n"
    "  namespace   accession namespace\n"
    "  accession   accession identifier\n"
    "  local_file  filename to local copy of VCF\n"
    "  url         VCF URL to serve to clients\n"
    "The VCF file is added to the database (without a block-level range index)\n"
    "based on the above information.\n"
    "Options:\n"
    "  --reference <id>  generate the block-level range index and associate it with\n"
    "                    this (arbitrary, server-specific) reference genome ID.\n"
    "                    The file must be compressed using bgzip_lines.\n"
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

    // get the file size
    ssize_t file_size = -1;
    struct stat fnstat;
    if (stat(fn, &fnstat) == 0) {
        file_size = fnstat.st_size;
    } else {
        cerr << "WARNING: couldn't open " << fn << ", recording unknown file size." << endl;
    }

    // determine the database ID for this file
    string dbid = derive_dbid(name_space, accession, "vcf", fn, url);

    // open/create the database
    shared_ptr<sqlite3> dbh = open_database(db);

    // begin the master transaction
    if (sqlite3_exec(dbh.get(), "begin", 0, 0, 0)) {
        throw runtime_error("failed to begin transaction...");
    }

    // insert the basic htsfiles entry
    insert_htsfile(dbh.get(), dbid.c_str(), "vcf", name_space, accession, url, file_size);

    if (!reference.empty()) {
        // build the block-level range index
        vcf_block_index(dbh.get(), reference.c_str(), dbid.c_str(), fn);
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
