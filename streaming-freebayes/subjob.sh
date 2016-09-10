#!/bin/bash
#
# script to run variant calling in ONE region (used by parallel in main.sh)

REGION="$1"
SERVER="$2"
NAMESPACE="$3"
shift 3
>&2 echo "$REGION"

set -e -o pipefail

# for each sample/"read group set" ID
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
wait

# run freebayes on the BAM slices
freebayes --standard-filters --min-repeat-entropy 1 --no-partial-observations --min-alternate-fraction 0.1 \
    -f hs37d5.fa --region "$REGION" ${bams[@]}

rm ${bams[@]}
