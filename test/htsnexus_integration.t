#!/bin/bash
set -o pipefail

cd "${HTSNEXUS_HOME}"
source test/bash-tap-bootstrap

plan tests 25

# use htsnexus_index_bam to build the test database
DBFN="${TMPDIR}/htsnexus_integration_test.db"
rm -f "$DBFN"

indexer/htsnexus_index_bam "${DBFN}" ENCODE ENCFF621SXE xxx "https://www.encodeproject.org/files/ENCFF621SXE/@@download/ENCFF621SXE.bam"
is "$?" "0" "add unindexed BAM"
find "$DBFN" -type f > /dev/null
is "$?" "0" "generate database"

indexer/htsnexus_index_bam --reference GRCh37 "$DBFN" htsnexus_test NA12878 test/htsnexus_test_NA12878.bam "https://dl.dnanex.us/F/D/pjZ1Z8fpYzKj5Z8v3qXzVfffV1XzkXk4Kg4KzGBY/htsnexus_test_NA12878.bam"
is "$?" "0" "index BAM"

# start the server
server_pid=""
function cleanup {
	if [ -n "$server_pid" ]; then
		echo "killing htsnexus test server pid=$server_pid"
		pkill -P $server_pid || true
	fi
}
trap cleanup EXIT

server/server.sh "$DBFN" &
server_pid=$!

sleep 2
ps -p $server_pid
is "$?" "0" "server startup"

# perform some queries
output=$(client/htsnexus.py -s http://localhost:48444 htsnexus_test NA12878 | samtools view -c -)
is "$?" "0" "read entire BAM"
is "$output" "39918" "read entire BAM - record count"

output=$(client/htsnexus.py -s http://localhost:48444 -r 20 htsnexus_test NA12878 | samtools view -c -)
is "$?" "0" "read BAM chromosome slice"
is "$output" "14955" "read BAM chromosome slice - approximate record count"

BAMFN="${TMPDIR}/htsnexus_integration_test.bam"
client/htsnexus.py -s http://localhost:48444 -r 20 htsnexus_test NA12878 > "$BAMFN"
is "$?" "0" "read BAM chromosome slice to file"
is "$(samtools view -c "$BAMFN")" "14955" "read BAM chromosome slice to file - approximate record count"
samtools index "$BAMFN"
is "$?" "0" "index local BAM chromosome slice"
is "$(samtools view -c "$BAMFN" 20)" "14545" "read BAM chromosome slice to file - exact record count"

client/htsnexus.py -s http://localhost:48444 -r 11:5005000-5006000 htsnexus_test NA12878 > "$BAMFN"
is "$?" "0" "read BAM range slice to file"
is "$(samtools view -c "$BAMFN")" "217" "read BAM range slice to file - approximate record count"
samtools index "$BAMFN"
is "$?" "0" "index local BAM range slice"
is "$(samtools view -c "$BAMFN" 11:5005000-5006000)" "32" "read BAM range slice to file - exact record count"

client/htsnexus.py -s http://localhost:48444 -r "*" htsnexus_test NA12878 > "$BAMFN"
is "$?" "0" "read BAM unplaced reads slice"
is "$(samtools view -c "$BAMFN")" "12551" "read BAM unplaced reads - approximate record count"
samtools index "$BAMFN"
is "$?" "0" "index local BAM unplaced reads slice"
is "$(samtools view "$BAMFN" | awk "\$3 == \"*\" {print;}" | wc -l)" "12475" "read BAM unplaced reads - exact record count"

is "$(samtools view -H "$BAMFN" | wc -l)" "103" "BAM header in slice"

output=$(client/htsnexus.py -s http://localhost:48444 -r 21 htsnexus_test NA12878 | samtools view -c -)
is "$?" "0" "read BAM empty range slice"
is "$output" "0" "read BAM empty range slice"

output=$(client/htsnexus.py -s http://localhost:48444 -r 21 htsnexus_test NA12878 | samtools view -c -)
is "$?" "0" "read BAM empty chromosome slice"
is "$output" "0" "read BAM empty chromosome slice"
