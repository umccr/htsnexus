// Performs stdin-to-stdout BGZF compression of an input text file (e.g. VCF) in
// a way that avoids splitting lines of the input across BGZF block boundaries.
// This facilitates later taking slices of the file without recompression.
//
// Each BGZF block begins exactly at the beginning of some line of the input
// file, and ends exactly at the end of some line (usually not the same one).
// The only EXCEPTION is if the first line in the block is so long as to span
// multiple BGZF blocks; then as many additional blocks as necessary will be
// dedicated to remaining parts of that line, and the next line will begin with
// a new block.
//
// Furthermore, if the input file has a header consisting of lines starting with
// '#', a new BGZF block is started beginning at the first line after the header.

#include <iostream>
#include <string>
#include <getopt.h>
#include <unistd.h>
#include "htslib/bgzf.h"

using namespace std;

const char* usage =
    "cat input.txt | bgzip_lines > output.txt.gz\n"
;

int main(int argc, char* argv[]) {
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'}
    };

    int c;
    while (-1 != (c = getopt_long(argc, argv, "h", long_options, 0))) {
        switch (c) {
            default:
                cout << usage << endl;
                return 1;
        }
    }

    if (isatty(0) || optind != argc) {
        cout << usage << endl;
        return 1;
    }

    #define H(cond) if (cond) { cerr << "[bgzip_lines] error: " << #cond << endl; return 1; }
    BGZF *fp = bgzf_dopen(fileno(stdout), "w");

    bool in_header = true;
    string line;
    while (getline(cin, line)) {
        // If we've buffered a partial BGZF block and this line isn't gonna fit
        // in its remaining capacity, flush it and start a new block.
        H(bgzf_flush_try(fp, line.size()));
        // If this is the first non-header line, start a new block.
        if (in_header && (line.size() > 0 && line[0] != '#')) {
            H(bgzf_flush(fp));
            in_header = false;
        }
        // Write the line
        line += '\n';
        auto block_address_pre = fp->block_address;
        H(bgzf_write(fp, line.c_str(), line.size()) < ssize_t(line.size()));
        // If that line was so long that it spilled over into a new block, end
        // the block so that the next line will start in a new block.
        if (fp->block_address != block_address_pre) {
            H(bgzf_flush(fp));
        }
    }

    H(!cin.eof() || cin.bad());
    H(bgzf_close(fp));
    return 0;
}
