#!/bin/bash
set -e -o pipefail

if [ $# -ne 2 ]; then
    echo "Usage: htsnexus_merge_databases.sh source.db destination.db"
    echo ""
    echo "destination.db is modified in-place!"
    exit 1
fi

if ! [ -f "$1" ]; then
    echo "does not exist: $1"
    exit 1
fi
if ! [ -f "$2" ]; then
    echo "does not exist: $2"
    exit 1
fi

sqlite3 -batch -bail "$2" "attach '$1' as toMerge;
begin;
insert into htsfiles select * from toMerge.htsfiles;
insert into htsfiles_blocks_meta select * from toMerge.htsfiles_blocks_meta;
insert into htsfiles_blocks select * from toMerge.htsfiles_blocks;
commit;
detach toMerge"
