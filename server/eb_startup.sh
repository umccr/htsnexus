#!/bin/bash
# Start htsnexus within the Elastic Beanstalk environment
set -ex -o pipefail

HERE="${BASH_SOURCE[0]}"
while [ -h "$HERE" ] ; do HERE="$(readlink "$HERE")"; done
HERE="$( cd -P "$( dirname "$HERE" )" && pwd )"

mkdir /host_var/htsnexus
aws s3 cp s3://dnanexus-rnd-htsnexus/current.db.gz /host_var/htsnexus
gunzip /host_var/htsnexus/current.db.gz
$HERE/server.sh --bind 0.0.0.0 /host_var/htsnexus/current.db
