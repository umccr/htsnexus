#!/bin/bash
#
# script to run variant calling in ONE region (used by parallel in main.sh)

JOB="$1"
JOBS="$2"
export REGION="$3"
export SERVER="$4"
export NAMESPACE="$5"
shift 5

source with_backoff.sh
export LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.1
set -e -o pipefail

# For each sample/"read group set" ID, use the htsnexus client to fetch a BAM
# slice covering the desired region.
fetch() {
    # this subroutine is invoked below within retry logic
    htsnexus -r "$REGION" -s "$SERVER" "$NAMESPACE" "$1" > "$2"
}
export -f fetch
SECONDS=0
bams=()
for id in "$@"; do
    bamfn=$(mktemp "${REGION}-${id}-XXXXXX.bam")
    # call fetch with retry
    with_backoff fetch "$id" "$bamfn"
    bams+=("$bamfn")
    # index the BAM slice -- this is unnecessary except to suppress an annoying
    # stderr warning from freebayes
    samtools index "$bamfn" &
done
T_FETCHING=$SECONDS
wait
bams_size=$(du -ch --apparent-size ${bams[@]} | tail -n1 | cut -f1)

# run freebayes on the BAM slices
SECONDS=0
freebayes --use-best-n-alleles=6 --standard-filters --min-repeat-entropy 1 --no-partial-observations --min-alternate-fraction 0.1 \
    -f hs37d5.fa --region "$REGION" ${bams[@]}
>&2 echo "(${JOB}/${JOBS}) $REGION fetched ${bams_size} in ${T_FETCHING}s, freebayes ran ${SECONDS}s"

rm ${bams[@]}

