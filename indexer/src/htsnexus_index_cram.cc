// Add a CRAM file to the htsnexus index database (creating the index if
// needed).

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <stdlib.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include "sqlite3.h"
#include "cram/cram.h"
#include "htslib/hfile.h"

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

/*************************************************************************************************/

const string CRAM_EOF(
    "\x0f\x00\x00\x00\xff\xff\xff\xff" // Cont HDR
    "\x0f\xe0\x45\x4f\x46\x00\x00\x00" // Cont HDR
    "\x00\x01\x00"                     // Cont HDR
    "\x05\xbd\xd9\x4f"                 // CRC32
    "\x00\x01\x00\x06\x06"             // Comp.HDR blk
    "\x01\x00\x01\x00\x01\x00"         // Comp.HDR blk
    "\xee\x63\x01\x4b",                // CRC32
    38);

const string CRAM_EOF_OLD(
    "\x0b\x00\x00\x00\xff\xff\xff\xff"
    "\x0f\xe0\x45\x4f\x46\x00\x00\x00"
    "\x00\x01\x00\x00\x01\x00\x06\x06"
    "\x01\x00\x01\x00\x01\x00", 30);

// add genomic ranges covered by one CRAM slice to the output data structure
void cram_slice_ranges(cram_fd *fd, cram_container* c, cram_slice* s, map<int, tuple<int,int>>& ans) {
    if (s->hdr->ref_seq_id >= 0) {
        // s->hdr->ref_seq_start is one-based, here we express lo as zero-
        // based.
        int lo = s->hdr->ref_seq_start-1;
        int hi = lo + s->hdr->ref_seq_span;
        auto p = ans.find(s->hdr->ref_seq_id);
        if (p != ans.end()) {
            lo = min(lo, get<0>(p->second));
            hi = max(hi, get<1>(p->second));
        }
        ans[s->hdr->ref_seq_id] = make_tuple(lo, hi);
    } else if (s->hdr->ref_seq_id == -1) {
        // unmapped
        ans[-1] = make_tuple(-1,-1);
    } else if (s->hdr->ref_seq_id == -2) {
        // "multi-ref" slice: decode and scan as in htslib:cram_index.c:cram_index_build_multiref
        if (0 != cram_decode_slice(fd, c, s, fd->header)) {
            throw runtime_error("cram_decode_slice failed");
        }

        int ref = -2, ref_start = -1, ref_end = -1;
        for (int i = 0; i < s->hdr->num_records; i++) {
            if (s->crecs[i].ref_id == ref) {
                if (ref != -1) {
                    if (s->crecs[i].apos <= ref_start) {
                        throw runtime_error("unsorted within multi-ref slice");
                    }
                    ref_end = std::max(ref_end, s->crecs[i].aend);
                }
                continue;
            }

            if (ref != -2) {
                auto p = ans.find(ref);
                if (p != ans.end()) {
                    ref_start = min(ref_start, get<0>(p->second));
                    ref_end = max(ref_end, get<1>(p->second));
                }
                ans[ref] = make_tuple(ref_start, ref_end);
            }

            ref = s->crecs[i].ref_id;
            if (ref != -1) {
                ref_start = s->crecs[i].apos - 1;
                ref_end = s->crecs[i].aend;
            } else {
                ref_start = ref_end = -1;
            }
        }

        if (ref != -2) {
            auto p = ans.find(ref);
            if (p != ans.end()) {
                ref_start = min(ref_start, get<0>(p->second));
                ref_end = max(ref_end, get<1>(p->second));
            }
            ans[ref] = make_tuple(ref_start, ref_end);
        }
    } else {
        throw runtime_error("Corrupt CRAM slice header (invalid ref_seq_id)");
    }
}

// populate the block-level index for the CRAM file (htsfiles_blocks_meta and htsfiles_blocks)
unsigned cram_block_index(sqlite3* dbh, const char* reference, const char* dbid, const char* cramfile) {
    // open the CRAM file
    shared_ptr<cram_fd> fd(cram_open(cramfile, "r"), cram_close);
    if (!fd) {
        throw runtime_error("Failed to open CRAM file");
    }

    auto cram_version = cram_major_vers(fd.get());
    if (cram_version != 2 && cram_version != 3) {
        throw runtime_error("Unsupported CRAM version " + to_string(cram_version));
    }

    // read in the raw header bytes (now that we can find out its exact size
    // based on how far cram_open read)
    size_t raw_header_size = (size_t) htell(fd->fp);
    shared_ptr<void> raw_header(malloc(raw_header_size), &free);
    shared_ptr<hFILE> raw_hf(hopen(cramfile, "r"), &hclose);
    if (!raw_hf) {
        throw runtime_error("Failed to reopen CRAM file");
    }
    if (hread(raw_hf.get(), raw_header.get(), raw_header_size) != raw_header_size || raw_hf->has_errno) {
        throw runtime_error("Failed to read CRAM raw header");
    }
    raw_hf.reset();

    // read the parsed SAM header
    SAM_hdr *header = cram_fd_get_header(fd.get());
    if (!header) {
        throw runtime_error("Failed to read CRAM header");
    }
    vector<string> target_names;
    for (int i = 0; i < header->nref; i++) {
        target_names.push_back(string(header->ref[i].name));
    }

    // insert the htsfiles_blocks_meta entry
    insert_block_index_meta(dbh, reference, dbid, string(sam_hdr_str(header)),
                            string((char*)raw_header.get(), raw_header_size),
                            cram_version >= 3 ? CRAM_EOF : CRAM_EOF_OLD);

    // now scan the CRAM file to populate htsfiles_blocks
    auto insert_block_stmt = prepare_insert_block(dbh);
    unsigned containers = 0;
    auto cpos = raw_header_size;
    shared_ptr<cram_container> c(cram_read_container(fd.get()), &cram_free_container);
    while (c) {
        if (fd->err) {
            throw runtime_error("Error reading CRAM container header");
        }
        containers++;

        auto hpos = htell(fd->fp);

        if (!(c->comp_hdr_block = cram_read_block(fd.get()))) {
            throw runtime_error("Error reading CRAM compression header");
        }

        if (c->comp_hdr_block->content_type != COMPRESSION_HEADER ||
            !(c->comp_hdr = cram_decode_compression_header(fd.get(), c->comp_hdr_block))) {
            throw runtime_error("Error decoding CRAM compression header");
        }

        // iterate through the slices in this container to accumulate a table
        // of the reference genomic ranges covered
        map<int, tuple<int,int>> container_ranges;
        for (int j = 0; j < c->num_landmarks; j++) {
            auto spos = htell(fd->fp);
            if (spos - cpos - c->offset != c->landmark[j]) {
                throw runtime_error("Corrupt CRAM container header");
            }

            shared_ptr<cram_slice> s(cram_read_slice(fd.get()), &cram_free_slice);
            if (!s) {
                throw runtime_error("Error reading CRAM slice in");
            }

            cram_slice_ranges(fd.get(), c.get(), s.get(), container_ranges);
        }

        // insert the entries
        for (const auto& r : container_ranges) {
            insert_block_index_entry(insert_block_stmt.get(), dbid, target_names,
                                     cpos, htell(fd->fp),
                                     r.first, get<0>(r.second), get<1>(r.second),
                                     string(), string());
        }

        // advance to next container
        cpos = htell(fd->fp);
        if (cpos != hpos + c->length) {
            throw runtime_error("Corrupt CRAM container header");
        }
        c.reset(cram_read_container(fd.get()));
    }
    if (fd->err) {
        throw runtime_error("Error reading CRAM container header");
    }

    return containers;
}

/*************************************************************************************************/

const char* usage =
    "htsnexus_index_cram [options] <index.db> <namespace> <accession> <local_file> <url>\n"
    "  index.db    SQLite3 database (will be created if nonexistent)\n"
    "  namespace   accession namespace\n"
    "  accession   accession identifier\n"
    "  local_file  filename to local copy of CRAM\n"
    "  url         CRAM URL to serve to clients\n"
    "The CRAM file is added to the database (without a block-level range index)\n"
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

    // get the file size
    ssize_t file_size = -1;
    struct stat fnstat;
    if (stat(fn, &fnstat) == 0) {
        file_size = fnstat.st_size;
    } else {
        cerr << "WARNING: couldn't open " << fn << ", recording unknown file size." << endl;
    }

    // determine the database ID for this file
    string dbid = derive_dbid(name_space, accession, "cram", fn, url);

    // open/create the database
    shared_ptr<sqlite3> dbh = open_database(db);

    // begin the master transaction
    if (sqlite3_exec(dbh.get(), "begin", 0, 0, 0)) {
        throw runtime_error("failed to begin transaction...");
    }

    // insert the basic htsfiles entry
    insert_htsfile(dbh.get(), dbid.c_str(), "cram", name_space, accession, url, file_size);

    if (!reference.empty()) {
        // build the block-level range index
        cram_block_index(dbh.get(), reference.c_str(), dbid.c_str(), fn);
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
