#!/bin/bash
# Start htsnexus within the Elastic Beanstalk environment
set -ex -o pipefail

HERE="${BASH_SOURCE[0]}"
while [ -h "$HERE" ] ; do HERE="$(readlink "$HERE")"; done
HERE="$( cd -P "$( dirname "$HERE" )" && pwd )"

mkdir /data
aws s3 cp s3://dnanexus-rnd-htsnexus/current.db.gz /data
gunzip /data/current.db.gz
$HERE/server.sh --bind 0.0.0.0 /data/current.db
