#!/bin/bash

main() {
    set -ex -o pipefail

    N="${#htsnexus_index[@]}"
    if [ "$N" -eq "1" ]; then
        dx-jobutil-add-output index_db "${htsnexus_index[0]}"
        exit 0
    fi

    dx cat "${htsnexus_index[0]}" | lz4 -dc > htsnexus_index
    for i in $(seq 1 $(expr $N - 1)); do
        dx cat "${htsnexus_index[$i]}" | lz4 -dc > piece
        htsnexus_merge_databases.sh piece htsnexus_index
        rm -f piece
    done

    sqlite3 -batch -bail htsnexus_index "vacuum; analyze"
    sqlite3 -batch -bail htsnexus_index "select count(*) from htsfiles"
    sqlite3 -batch -bail htsnexus_index "select count(*) from htsfiles_blocks_meta"

    id=$(lz4 -c htsnexus_index |
            dx upload --destination "${output_name}.lz4" --type htsnexus_index --brief -)
    dx-jobutil-add-output index_db "$id"
}
