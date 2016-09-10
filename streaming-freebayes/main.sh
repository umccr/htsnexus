#!/bin/bash
#
# main entry point inside the ga4gh-streaming-freebayes container
# TODO: 21 only

if [ $# -lt 3 ]; then
  echo "Usage: docker run dnamlin/ga4gh-streaming-freebayes SERVER NAMESPACE ID [ID2 ID3 ...] > variants.vcf"
  exit 1
fi

SERVER="$1"
NAMESPACE="$2"
shift 2 # now $@ holds the IDs

# log system utilization statistics to stderr
>&2 dstat -cmdn 60 & dstat_pid=$!

set -ex -o pipefail

# fetch, unpack, and index reference genome
aria2c -q -x 10 -j 10 -s 10 --retry-wait=1 https://s3.amazonaws.com/1000genomes/technical/reference/phase2_reference_assembly_sequence/hs37d5.fa.gz
pigz -d hs37d5.fa.gz || true # hs37d5.fa.gz has trailing junk which causes a nonzero exit of pigz
samtools faidx hs37d5.fa

# parallelize calling subjob.sh on each genomic region. Emits VCF to stdout.
# The post-processing pipeline is adapted from the freebayes-parallel script.
(parallel -k -j $(expr `nproc` \* 5 / 4) --halt 2 --delay 1 -a hs37d5_interLCR_intervals.4Mbp.regions \
    ./subjob.sh {} "$SERVER" "$NAMESPACE" $@) \
  | vcffirstheader | vcfstreamsort -w 1000 | vcfuniq

kill $dstat_pid
