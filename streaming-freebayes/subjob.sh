#!/bin/bash
#
# script to run variant calling in ONE region (used by parallel in main.sh)

bamfn=$(mktemp XXXXXXXXX.bam)
# TODO: retry (just use parallel --retry, with 1 input line?)
>&2 echo "$4"
htsnexus -r "$4" -s "$1" "$2" "$3" > $bamfn
samtools index $bamfn # just to suppress an annoying stderr warning from freebayes
freebayes -f hs37d5.fa --standard-filters --min-repeat-entropy 1 --no-partial-observations --min-alternate-fraction 0.1 --region "$4" $bamfn

rm $bamfn
