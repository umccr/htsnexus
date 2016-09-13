#!/bin/bash
#
# main entry point inside the ga4gh-streaming-freebayes container

set -e -o pipefail

########################
# process command line #
########################
USAGE="Usage:
  docker run --rm dnamlin/ga4gh-streaming-freebayes [-t] SERVER_URL NAMESPACE ID [ID2 ID3 ...] > variants.vcf
Options:
  -t    test mode; process chromosome 22 only
"

only22=""
while getopts "t" opt; do
  case $opt in
    t)
      only22="true"
      ;;
    *)
      >&2 echo "$USAGE"
      exit 1
      ;;
  esac
done
shift $((OPTIND-1))

if [ $# -lt 3 ]; then
  >&2 echo "$USAGE"
  exit 1
fi

SERVER="$1"
NAMESPACE="$2"
shift 2 # now $@ holds the IDs

# log system utilization statistics to stderr
>&2 dstat --nocolor -cmdn 60 &
trap '[ -n "$(jobs -pr)" ] && kill $(jobs -pr)' INT QUIT TERM EXIT

#############################################
# fetch, unpack, and index reference genome #
#############################################
>&2 echo "* Preparing reference genome *"
aria2c -q -x 10 -j 10 -s 10 --retry-wait=1 https://s3.amazonaws.com/1000genomes/technical/reference/phase2_reference_assembly_sequence/hs37d5.fa.gz
pigz -d hs37d5.fa.gz || true # hs37d5.fa.gz has trailing junk which causes a nonzero exit of pigz
samtools faidx hs37d5.fa

###############################################################################
# parallelize calling subjob.sh on each genomic region. Emits VCF to stdout.  #
# The post-processing pipeline is adapted from the freebayes-parallel script. #
###############################################################################
>&2 echo "* Beginning variant calling *"
if [ -n "$only22" ]; then
  grep "^22" hs37d5_interLCR_intervals.4Mbp.regions > regions
else
  mv hs37d5_interLCR_intervals.4Mbp.regions regions
fi
JOBS=$(cat regions | wc -l)
SECONDS=0
(parallel -k -j $(expr `nproc` \* 5 / 4) --halt 2 --delay 1 -a regions \
    ./subjob.sh {#} "$JOBS" {} "$SERVER" "$NAMESPACE" $@) \
  | vcffirstheader | vcfstreamsort -w 1000 | vcfuniq
>&2 echo "* Variant calling completed in ${SECONDS}s *"
