#!/bin/bash
#
# script to run variant calling in ONE region (used by parallel in main.sh)

JOB="$1"
JOBS="$2"
REGION="$3"
SERVER="$4"
NAMESPACE="$5"
shift 5

set -e -o pipefail

# for each sample/"read group set" ID
SECONDS=0
bams=()
for id in "$@"; do
    # Fetch a BAM slice covering the desired region
    # TODO: timeout+retry (just use parallel --retry, with 1 input line?)
    bamfn=$(mktemp "${REGION}-${id}-XXXXXX.bam")
    htsnexus -r "$REGION" -s "$SERVER" "$NAMESPACE" "$id" > $bamfn
    bams+=($bamfn)
    # index the BAM slice -- this is only to suppress an annoying stderr
    # warning from freebayes
    samtools index $bamfn &
done
T_FETCHING=$SECONDS
wait

# run freebayes on the BAM slices
SECONDS=0
freebayes --standard-filters --min-repeat-entropy 1 --no-partial-observations --min-alternate-fraction 0.1 \
    -f hs37d5.fa --region "$REGION" ${bams[@]}
>&2 echo "(${JOB}/${JOBS}) $REGION fetched in ${T_FETCHING}s, called in ${SECONDS}s"

rm ${bams[@]}

