#!/bin/bash

main() {
    set -ex -o pipefail

    N="${#accessions[@]}"
    if [ "$N" -ne "${#urls[@]}" ]; then
        dx-jobutil-report-error "accessions and urls arrays must have the same length" AppError
    fi

    mkdir -p /home/dnanexus/out/index_db
    cd /tmp
    dlpid=""
    dlfn=""
    for i in $(seq 0 $(expr $N - 1)); do
        if [ "$i" -eq 0 ]; then
            dlfn=$(basename $(mktemp --suffix .bam))
            rm "$dlfn"
            aria2c -x 10 -j 10 -s 10 -o "$dlfn" "${urls[0]}" & dlpid=$!
        fi
        wait $dlpid

        fn="$dlfn"
        if [ "$i" -lt "$(expr $N - 1)" ]; then
            dlfn=$(basename $(mktemp --suffix .bam))
            rm "$dlfn"
            aria2c -x 10 -j 10 -s 10 -o "$dlfn" "${urls[$(expr $i + 1)]}" & dlpid=$!
        fi

        if ! grep -F "${accessions[$i]}" <(echo "${urls[$i]}"); then
            dx-jobutil-report-error "Failed sanity check: URL ${urls[$i]} doesn't contain accession ${accessions[$i]}"
            exit 1
        fi

        htsnexus_index_bam --reference "$reference" /home/dnanexus/out/index_db/htsnexus_index "$namespace" "${accessions[$i]}" "$fn" "${urls[$i]}"
        rm "$fn"
    done

    htsnexus_downsample_index.py /home/dnanexus/out/index_db/htsnexus_index
    ls -l /home/dnanexus/out/index_db/

    id=$(pigz -c /home/dnanexus/out/index_db/htsnexus_index.downsampled |
            dx upload --destination "${output_name}" --type htsnexus_index --brief -)
    dx-jobutil-add-output index_db "$id"
}
