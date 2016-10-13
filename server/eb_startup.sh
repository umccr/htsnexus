#!/bin/bash
# Start htsnexus within the Elastic Beanstalk environment
set -ex -o pipefail

HERE="${BASH_SOURCE[0]}"
while [ -h "$HERE" ] ; do HERE="$(readlink "$HERE")"; done
HERE="$( cd -P "$( dirname "$HERE" )" && pwd )"

mkdir -p /host_var/htsnexus
aws s3 cp s3://dnanexus-rnd-htsnexus/current.db.gz - | pigz -dc > /host_var/htsnexus/current.db
$HERE/server.sh \
    --bind 0.0.0.0 \
    --credentials <(aws s3 cp s3://dnanexus-rnd-htsnexus/credentials.json -) \
    /host_var/htsnexus/current.db
