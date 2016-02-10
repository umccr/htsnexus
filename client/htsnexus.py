#!/usr/bin/env python2.7

import subprocess
import argparse
import requests
import sys
import urllib
import base64

DEFAULT_SERVER='http://htsnexus.rnd.dnanex.us:48444'

def query_htsnexus(namespace, accession, server=DEFAULT_SERVER, genomic_range=None, verbose=False):
    query_url = '/'.join([args.server, 'bam', urllib.quote(args.namespace), urllib.quote(args.accession)])
    if genomic_range:
        query_url = query_url + '?bamHeaderBGZF&range=' + urllib.quote(genomic_range)
    if verbose:
        print >>sys.stderr, ('Query URL: ' + query_url)
    response = requests.get(query_url)
    if response.status_code != 200:
        print >>sys.stderr, ("Error: HTTP status " + str(response.status_code))
        print >>sys.stderr, response.json()
        sys.exit(1)
    if verbose:
        print >>sys.stderr, ('Response: ' + response.text)
    return response.json()

parser = argparse.ArgumentParser(description='htsnexus streaming client')
parser.add_argument('-s','--server', metavar='URL', type=str, default=DEFAULT_SERVER, help='htsnexus server endpoint')
parser.add_argument('-r','--range', metavar='RANGE', type=str, help='target genomic range, seq:lo-hi or just seq')
parser.add_argument('-v', '--verbose', action='store_true', help='verbose log to standard error')
parser.add_argument('namespace', type=str, help="accession namespace")
parser.add_argument('accession', type=str, help="BAM accession")
args = parser.parse_args()

bam_ticket = query_htsnexus(args.namespace, args.accession, server=args.server,
                            genomic_range=args.range, verbose=args.verbose)

if 'byteRange' in bam_ticket and (bam_ticket['byteRange'] is None or bam_ticket['byteRange']['lo'] > 0):
    # if we're not reading from the beginning of the file, first emit the header block
    sys.stdout.write(base64.b64decode(bam_ticket['bamHeaderBGZF']))
    sys.stdout.flush()

if 'byteRange' not in bam_ticket or bam_ticket['byteRange'] is not None:
    # run curl to pipe the data
    curlcmd = ['curl','-LSs']
    if 'httpRequestHeaders' in bam_ticket:
        for k, v in bam_ticket['httpRequestHeaders'].items():
            curlcmd.append('-H')
            curlcmd.append(str(k + ': ' + v))
    curlcmd.append(bam_ticket['url'])
    if args.verbose:
        print >>sys.stderr, ('Piping: ' + str(curlcmd))
    subprocess.check_call(curlcmd)

if 'byteRange' in bam_ticket:
    # emit the EOF marker
    sys.stdout.write('\037\213\010\4\0\0\0\0\0\377\6\0\102\103\2\0\033\0\3\0\0\0\0\0\0\0\0\0')

if args.verbose:
    print >>sys.stderr, 'Success'
