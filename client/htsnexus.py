#!/usr/bin/env python2.7

import subprocess
import argparse
import requests
import sys
import urllib
import base64
import json
import re
import os
from copy import deepcopy

DEFAULT_SERVER='http://htsnexus.rnd.dnanex.us/v1/reads'

# convert a "22:1000-2000" genomic range string to a formatted query string
def genomic_range_query_string(genomic_range):
    m = re.match("^([A-Za-z0-9._*-]+)(:([0-9]+)-([0-9]+))?$", genomic_range)
    ans = "referenceName=" + urllib.quote(m.group(1))
    if m.group(4):
        ans = ans + "&start=" + urllib.quote(m.group(3)) + "&end=" + urllib.quote(m.group(4))
    return ans

# Contact the htsnexus server to request a "ticket" for a file or slice.
# In particular the ticket will specify a URL at which the desired data can be
# accessed (possibly with a byte range and auth headers).
def get_ticket(namespace, accession, format, server=DEFAULT_SERVER, genomic_range=None, verbose=False):
    # rewrite server endpoint for variants: a temporary hack so that this client
    # can continue to address the other GA4GH prototype servers while we talk
    # about how the URL endpoints should look.
    server_endpoint = server
    if server_endpoint.endswith("/reads") and format == 'VCF':
        server_endpoint = server_endpoint[:-6] + "/variants"
    # construct query URL
    query_url = '/'.join([server_endpoint, urllib.quote(namespace), urllib.quote(accession)])
    query_url = query_url + "?format=" + urllib.quote(format)
    if genomic_range:
        query_url = query_url + '&' + genomic_range_query_string(genomic_range)
    if verbose:
        print >>sys.stderr, ('Query URL: ' + query_url)
    request_headers = {}
    if 'DX_JOB_ID' in os.environ: # DNAnexus-specific cookie
        request_headers['dx-job-id'] = os.environ['DX_JOB_ID']
    # issue request
    response = requests.get(query_url, headers=request_headers)
    if response.status_code != 200:
        print >>sys.stderr, ("Error: HTTP status " + str(response.status_code))
        print >>sys.stderr, response.json()
        sys.exit(1)
    # parse response JSON
    ans = response.json()
    if verbose:
        response_copy = deepcopy(ans)
        # don't print big base64 buffers to console...
        for item in response_copy['urls']:
            if item['url'].startswith('data:'):
                delim = item['url'].index(',')
                item['url'] = item['url'][:(delim+1)] + '[' + str(len(item['url'])-delim-1) + ' base64 characters]'
        print >>sys.stderr, ('Response: ' + json.dumps(response_copy, indent=2, separators=(',', ': ')))
    return ans

def get(namespace, accession, format, verbose=False, **kwargs):
    # get ticket
    ticket = get_ticket(namespace, accession, format, verbose=verbose, **kwargs)

    # pipe the raw data
    for item in ticket['urls']:
        if item['url'].startswith('data:'):
            # emit a blob given inline as a data URI. typically contains a format-specific
            # header or footer/EOF when taking a genomic range slice.
            sys.stdout.write(base64.b64decode(item['url'][(item['url'].index(',')+1):]))
            sys.stdout.flush()
        else:
            # delegate to curl to access the URL given in the ticket, including any
            # HTTP request headers htsnexus instructed us to supply.
            curlcmd = ['curl','-LSs']
            if 'headers' in item:
                for k, v in item['headers'].items():
                    curlcmd.append('-H')
                    curlcmd.append(str(k + ': ' + v))
            curlcmd.append(str(item['url']))
            if verbose:
                print >>sys.stderr, ('Piping: ' + str(curlcmd))
                sys.stderr.flush()
            subprocess.check_call(curlcmd)

    if verbose:
        print >>sys.stderr, 'Success'

def main():
    parser = argparse.ArgumentParser(description='htsnexus streaming client', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-s','--server', metavar='URL', type=str, default=DEFAULT_SERVER, help='htsnexus server endpoint')
    parser.add_argument('-r','--range', metavar='RANGE', type=str, help='target genomic range, seq:lo-hi or just seq')
    parser.add_argument('-v', '--verbose', action='store_true', help='verbose log to standard error')
    parser.add_argument('namespace', type=str, help="accession namespace")
    parser.add_argument('accession', type=str, help="accession")
    parser.add_argument('format', type=str, nargs='?', default='BAM', choices=['BAM','bam','CRAM','cram','VCF','vcf'], help="format")
    args = parser.parse_args()
    args.format = args.format.upper()

    return get(args.namespace, args.accession, args.format,
               server=args.server, genomic_range=args.range, verbose=args.verbose)

if __name__ == '__main__':
   main()
