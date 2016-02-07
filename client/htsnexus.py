#!/usr/bin/env python2.7

import subprocess
import argparse
import requests
import sys

DEFAULT_SERVER='http://localhost:48444'

def query_htsnexus(namespace, accession, server=DEFAULT_SERVER):
    query_url = '/'.join([args.server, 'bam', args.namespace, args.accession])
    response = requests.get(query_url)
    if response.status_code != 200:
        print >> sys.stderr, ("Error: HTTP status " + str(response.status_code))
        print >> sys.stderr, response.json()
        sys.exit(1)
    return response.json()

parser = argparse.ArgumentParser(description='htsnexus streaming client')
parser.add_argument('--server', metavar='URL', type=str, default=DEFAULT_SERVER, help='htsnexus server endpoint')
parser.add_argument('namespace', type=str, help="accession namespace")
parser.add_argument('accession', type=str, help="BAM accession")
args = parser.parse_args()

bam_info = query_htsnexus(args.namespace, args.accession, server=args.server)

# TODO: print the bamHeaderBGZF iff the byte range beginning is non-zero
# TODO: handle bam_info.httpRequestHeaders

subprocess.check_call(['curl','-LSs',bam_info['url']])

# TODO: print the EOF marker which consists of the following hexadecimal bytes:
# 1f 8b 08 04 00 00 00 00 00 ff 06 00 42 43 02 00 1b 00 03 00 00 00 00 00 00 00 00 00
