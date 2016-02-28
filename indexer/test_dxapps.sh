#!/bin/bash
set -ex -o pipefail

HERE="${BASH_SOURCE[0]}"
while [ -h "$HERE" ] ; do HERE="$(readlink "$HERE")"; done
HERE="$( cd -P "$( dirname "$HERE" )" && pwd )"

HTSNEXUS_PROJECT="project-BpvkP5j0zz5YVFggVjxypjzk"

dx build --archive --destination "${HTSNEXUS_PROJECT}:/" "${HERE}/dxapp"
dx build --archive --destination "${HTSNEXUS_PROJECT}:/" "${HERE}/merge.dxapp"

acc1="NA19625
NA19700
NA19701
NA19703"

acc2="NA19704
NA19707
NA19711
NA19712"

bam_args1=$(echo "$acc1" | awk '{printf(" -i accessions=%s -i urls='https://s3.amazonaws.com/1000genomes/phase3/data/%s/alignment/%s.mapped.ILLUMINA.bwa.ASW.low_coverage.20120522.bam'", $1, $1, $1);}')
bam_indexer_job1=$(dx run "${HTSNEXUS_PROJECT}:/htsnexus_indexer" --brief --priority normal -y --destination "${HTSNEXUS_PROJECT}:/Attic/" --name test_dxapp_bam1 -i namespace=test_dxapps -i reference=hs37d5 -i output_name=test_dxapp_bam1 $bam_args1 -i downsample=true)

bam_args2=$(echo "$acc2" | awk '{printf(" -i accessions=%s -i urls='https://s3.amazonaws.com/1000genomes/phase3/data/%s/alignment/%s.mapped.ILLUMINA.bwa.ASW.low_coverage.20120522.bam'", $1, $1, $1);}')
bam_indexer_job2=$(dx run "${HTSNEXUS_PROJECT}:/htsnexus_indexer" --brief --priority normal -y --destination "${HTSNEXUS_PROJECT}:/Attic/" --name test_dxapp_bam2 -i namespace=test_dxapps -i reference=hs37d5 -i output_name=test_dxapp_bam2 $bam_args2 -i downsample=true)

bam_merge_job=$(dx run "${HTSNEXUS_PROJECT}:/htsnexus_index_merger" --brief --priority normal -y --destination "${HTSNEXUS_PROJECT}:/Attic/" --name test_dxapp_bam_merge -i htsnexus_index="${bam_indexer_job1}:index_db" -i htsnexus_index="${bam_indexer_job2}:index_db" -i output_name=test_dxapp_bam)

cram_args1=$(echo "$acc1" | awk '{printf(" -i accessions=%s -i urls='https://s3.amazonaws.com/1000genomes/phase3/data/%s/alignment/%s.mapped.ILLUMINA.bwa.ASW.low_coverage.20120522.bam.cram'", $1, $1, $1);}')
cram_indexer_job1=$(dx run "${HTSNEXUS_PROJECT}:/htsnexus_indexer" --brief --priority normal -y --destination "${HTSNEXUS_PROJECT}:/Attic/" --name test_dxapp_cram1 -i namespace=test_dxapps -i reference=hs37d5 -i output_name=test_dxapp_cram1 $cram_args1)

cram_args2=$(echo "$acc2" | awk '{printf(" -i accessions=%s -i urls='https://s3.amazonaws.com/1000genomes/phase3/data/%s/alignment/%s.mapped.ILLUMINA.bwa.ASW.low_coverage.20120522.bam.cram'", $1, $1, $1);}')
cram_indexer_job2=$(dx run "${HTSNEXUS_PROJECT}:/htsnexus_indexer" --brief --priority normal -y --destination "${HTSNEXUS_PROJECT}:/Attic/" --name test_dxapp_cram2 -i namespace=test_dxapps -i reference=hs37d5 -i output_name=test_dxapp_cram2 $cram_args2)

cram_merge_job=$(dx run "${HTSNEXUS_PROJECT}:/htsnexus_index_merger" --brief --priority normal -y --destination "${HTSNEXUS_PROJECT}:/Attic/" --name test_dxapp_cram_merge -i htsnexus_index="${cram_indexer_job1}:index_db" -i htsnexus_index="${cram_indexer_job2}:index_db" -i output_name=test_dxapp_cram)

dx run "${HTSNEXUS_PROJECT}:/htsnexus_index_merger" --priority normal -y --destination "${HTSNEXUS_PROJECT}:/Attic/" --name test_dxapp_merge -i htsnexus_index="${bam_merge_job}:index_db" -i htsnexus_index="${cram_merge_job}:index_db" -i output_name=test_dxapp
