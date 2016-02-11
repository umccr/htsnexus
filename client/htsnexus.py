#!/usr/bin/env python2.7

import subprocess
import argparse
import requests
import sys
import urllib
import base64
import json

DEFAULT_SERVER='http://htsnexus.rnd.dnanex.us'

# Contact the htsnexus server to request a "ticket" for a file or slice.
# In particular the ticket will specify a URL at which the desired data can be
# accessed (possibly with a byte range and auth headers).
def query_htsnexus(namespace, accession, server=DEFAULT_SERVER, genomic_range=None, verbose=False):
    # construct query URL
    query_url = '/'.join([args.server, 'bam', urllib.quote(args.namespace), urllib.quote(args.accession)])
    if genomic_range:
        query_url = query_url + '?bamHeaderBGZF&range=' + urllib.quote(genomic_range)
    if verbose:
        print >>sys.stderr, ('Query URL: ' + query_url)
    # issue request
    response = requests.get(query_url)
    if response.status_code != 200:
        print >>sys.stderr, ("Error: HTTP status " + str(response.status_code))
        print >>sys.stderr, response.json()
        sys.exit(1)
    # parse response JSON
    ans = response.json()
    if verbose:
        response_copy = dict(ans)
        if 'bamHeaderBGZF' in response_copy:
            # don't print big base64 buffer to console...
            response_copy['bamHeaderBGZF'] = '[' + str(len(response_copy['bamHeaderBGZF'])) + ' base64 characters]'
        print >>sys.stderr, ('Response: ' + json.dumps(response_copy, indent=2, separators=(',', ': ')))
    return ans

parser = argparse.ArgumentParser(description='htsnexus streaming client')
parser.add_argument('-s','--server', metavar='URL', type=str, default=DEFAULT_SERVER, help='htsnexus server endpoint')
parser.add_argument('-r','--range', metavar='RANGE', type=str, help='target genomic range, seq:lo-hi or just seq')
parser.add_argument('-v', '--verbose', action='store_true', help='verbose log to standard error')
parser.add_argument('namespace', type=str, help="accession namespace")
parser.add_argument('accession', type=str, help="BAM accession")
args = parser.parse_args()

# get ticket
bam_ticket = query_htsnexus(args.namespace, args.accession, server=args.server,
                            genomic_range=args.range, verbose=args.verbose)

# emit the header block (if we're not reading from the beginning of the file)
if 'byteRange' in bam_ticket and (bam_ticket['byteRange'] is None or bam_ticket['byteRange']['lo'] > 0):
    sys.stdout.write(base64.b64decode(bam_ticket['bamHeaderBGZF']))
    sys.stdout.flush()

# pipe the raw data (unless the result set is empty)
if 'byteRange' not in bam_ticket or bam_ticket['byteRange'] is not None:
    # delegate to curl to access the URL given in the ticket, including any
    # HTTP request headers htsnexus instructed us to supply.
    curlcmd = ['curl','-LSs']
    if 'httpRequestHeaders' in bam_ticket:
        for k, v in bam_ticket['httpRequestHeaders'].items():
            curlcmd.append('-H')
            curlcmd.append(str(k + ': ' + v))
    curlcmd.append(bam_ticket['url'])
    if args.verbose:
        print >>sys.stderr, ('Piping: ' + str(curlcmd))
    subprocess.check_call(curlcmd)

# emit the EOF marker (unless we were reading the entire file)
if 'byteRange' in bam_ticket:
    sys.stdout.write('\037\213\010\4\0\0\0\0\0\377\6\0\102\103\2\0\033\0\3\0\0\0\0\0\0\0\0\0')

if args.verbose:
    print >>sys.stderr, 'Success'
