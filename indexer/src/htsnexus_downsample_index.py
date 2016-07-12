#!/usr/bin/env python2.7

# This utility takes an htsnexus index database and creates a version of it
# with reduced size and resolution of the block-level genomic range index. For
# BAM files, the index has typically been generated with one entry for each
# ~64KiB BGZF block (uncompressed), which permits slicing at that high
# resolution, but can produce a fairly large index file. So we consolidate
# index entries for adjacent blocks, producing a smaller index file which
# still supports reasonable slicing resolution (~256KiB compressed,
# configurable)

import argparse
import sys
import sqlite3
import shutil

parser = argparse.ArgumentParser(description='htsnexus index downsampler utility')
parser.add_argument('-r','--resolution', metavar='SIZE', type=int, default=262144, help='target slice resolution in bytes (default: 1MiB)')
parser.add_argument('db', type=str, help="database file (output will be written to db.downsampled)")
args = parser.parse_args()

# make a copy of the database file
dest_fn = args.db + '.downsampled'
shutil.copy(args.db, dest_fn)

# open the source database and list the files and seqs
src_conn = sqlite3.connect(args.db)
files = set(row[0] for row in src_conn.execute('select distinct _dbid from htsfiles_blocks order by _dbid'))
seqs = set(row[0] for row in src_conn.execute('select distinct seq from htsfiles_blocks where seq is not null order by seq'))

# open the destination database and delete everything in htsfiles_blocks
dest_conn = sqlite3.connect(dest_fn)
dest_cursor = dest_conn.cursor()
dest_cursor.execute('delete from htsfiles_blocks')

# main processing loop
for file in files:
    for seq in seqs:
        byteLo = sys.maxint
        byteHi = -1
        seqLo = (sys.maxint if seq is not None else None)
        seqHi = (-1 if seq is not None else None)

        # scan the index entries for this file & seq
        # TODO: handle block_prefix and block_suffix
        for row in src_conn.execute('select byteLo, byteHi, seqLo, seqHi from htsfiles_blocks where _dbid=? and seq=? order by byteLo, byteHi', (file, seq)):
            byteLo = min(byteLo, row[0])
            byteHi = max(byteHi, row[1])
            if seq is not None:
                seqLo = min(seqLo, row[2])
                seqHi = max(seqHi, row[3])

            # when the accumulated byte range for seq passes the desired
            # resolution, insert a consolidated destination index entry
            assert (byteLo >= 0 and byteHi > byteLo and (seq is None or (seqLo >= 0 and seqHi > seqLo)))
            if byteHi - byteLo >= args.resolution:
                dest_cursor.execute('insert into htsfiles_blocks values(?,?,?,?,?,?,?,?)', (file, byteLo, byteHi, seq, seqLo, seqHi, None, None))
                byteLo = sys.maxint
                byteHi = -1
                seqLo = (sys.maxint if seq is not None else None)
                seqHi = (-1 if seq is not None else None)

        # last entry
        if byteHi >= 0:
            dest_cursor.execute('insert into htsfiles_blocks values(?,?,?,?,?,?,?,?)', (file, byteLo, byteHi, seq, seqLo, seqHi, None, None))

    # create a consolidated entry for the unmapped reads (if any)
    unmapped_query = 'select min(byteLo), max(byteHi) from htsfiles_blocks where _dbid=? and seq is null'
    unmapped = list(src_conn.execute(unmapped_query, (file,)))
    if len(unmapped) and unmapped[0][0] is not None:
        dest_cursor.execute('insert into htsfiles_blocks values(?,?,?,?,?,?,?,?)', (file, unmapped[0][0], unmapped[0][1], None, None, None, None, None))

# sanity check concordance of the old and new indices
check = "select min(seqLo), max(seqHi), min(byteLo), max(byteHi) from htsfiles_blocks group by _dbid, seq order by _dbid, seq"
assert (list(src_conn.execute(check)) == list(dest_conn.execute(check)))

# finish up
dest_conn.commit()
dest_conn.execute('vacuum')
