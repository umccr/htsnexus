#!/bin/bash

merge_sql="attach 'piece' as toMerge;
begin;
insert into htsfiles select * from toMerge.htsfiles;
insert into htsfiles_blocks_meta select * from toMerge.htsfiles_blocks_meta;
insert into htsfiles_blocks select * from toMerge.htsfiles_blocks;
commit;
detach toMerge"

main() {
    set -ex -o pipefail

    N="${#htsnexus_index[@]}"
    if [ "$N" -eq "1" ]; then
        dx-jobutil-add-output index_db "${htsnexus_index[0]}"
        exit 0
    fi

    dx cat "${htsnexus_index[0]}" | pigz -dc > htsnexus_index
    for i in $(seq 1 $(expr $N - 1)); do
        dx cat "${htsnexus_index[$i]}" | pigz -dc > piece
        sqlite3 -batch -bail htsnexus_index "$merge_sql"
        rm -f piece
    done

    sqlite3 -batch -bail htsnexus_index "analyze"
    sqlite3 -batch -bail htsnexus_index "vacuum"

    id=$(pigz -c htsnexus_index |
            dx upload --destination "${output_name}" --type htsnexus_index --brief -)
    dx-jobutil-add-output index_db "$id"
}
