#!/usr/bin/env python

import subprocess
import argparse
import requests
import sys
import urllib.request, urllib.parse, urllib.error
import base64
import json
import re
import os
from copy import deepcopy

DEFAULT_SERVER = 'https://htsnexus.rnd.dnanex.us/v1/reads' if 'DX_JOB_ID' not in os.environ else 'https://htsnexus.rnd.dnanex.us/dxjob/v1/reads'

# convert a "22:1000-2000" genomic range string to a formatted query string
def genomic_range_query_string(genomic_range):
    m = re.match("^([A-Za-z0-9._*-]+)(:([0-9]+)-([0-9]+))?$", genomic_range)
    ans = "referenceName=" + urllib.parse.quote(m.group(1))
    if m.group(4):
        ans = ans + "&start=" + urllib.parse.quote(m.group(3)) + "&end=" + urllib.parse.quote(m.group(4))
    return ans

# Contact the htsnexus server to request a "ticket" for a file or slice.
# In particular the ticket will specify a URL at which the desired data can be
# accessed (possibly with a byte range and auth headers).
def get_ticket(namespace, accession, format, server=DEFAULT_SERVER, token=None, genomic_range=None,
               verbose=False, insecure=False):
    # rewrite server endpoint for variants: a temporary hack so that this client
    # can continue to address the other GA4GH prototype servers while we talk
    # about how the URL endpoints should look.
    server_endpoint = server
    if server_endpoint.endswith("/reads") and format == 'VCF':
        server_endpoint = server_endpoint[:-6] + "/variants"
    # construct query URL
    query_url = '/'.join([server_endpoint, urllib.parse.quote(namespace), urllib.parse.quote(accession)])
    query_url = query_url + "?format=" + urllib.parse.quote(format)
    if genomic_range:
        query_url = query_url + '&' + genomic_range_query_string(genomic_range)
    if verbose:
        print(('Query URL: ' + query_url), file=sys.stderr)
    query_headers = {}
    if token is not None:
        query_headers["Authorization"] = "Bearer " + token
    # issue request
    response = requests.get(query_url, headers=query_headers, verify=(not insecure))
    if response.status_code != 200:
        print(("Error: HTTP status " + str(response.status_code)), file=sys.stderr)
        print(response.json(), file=sys.stderr)
        sys.exit(1)
    # parse response JSON
    ans = response.json()
    if verbose:
        response_copy = deepcopy(ans)
        # don't print big base64 buffers to console...
        if 'htsget' in response_copy:
            for item in response_copy['htsget']['urls']:
                if item['url'].startswith('data:'):
                    delim = item['url'].index(',')
                    item['url'] = item['url'][:(delim+1)] + '[' + str(len(item['url'])-delim-1) + ' base64 characters]'
        print(('Response: ' + json.dumps(response_copy, indent=2, separators=(',', ': '))), file=sys.stderr)
    if 'htsget' not in ans:
        print(("Unexpected response JSON format from server"), file=sys.stderr)
        sys.exit(1)
    return ans['htsget']

def get(namespace, accession, format, verbose=False, insecure=False, **kwargs):
    # get ticket
    ticket = get_ticket(namespace, accession, format, verbose=verbose, insecure=insecure, **kwargs)

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
            curlcmd = ['curl','-LSs','--fail']
            if 'headers' in item:
                for k, v in list(item['headers'].items()):
                    curlcmd.append('-H')
                    curlcmd.append(str(k + ': ' + v))
            if insecure:
                curlcmd.append('--insecure')
            curlcmd.append(str(item['url']))
            if verbose:
                print(('Piping: ' + str(curlcmd)), file=sys.stderr)
                sys.stderr.flush()
            try:
                subprocess.check_call(curlcmd)
            except subprocess.CalledProcessError as exn:
                # curl's stderr message is more informative than the CalledProcessError
                sys.exit(exn.returncode)

    if verbose:
        print('Success', file=sys.stderr)

def main():
    parser = argparse.ArgumentParser(description='htsnexus streaming client', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-s','--server', metavar='URL', type=str, default=DEFAULT_SERVER, help='htsnexus server endpoint')
    parser.add_argument('-r','--range', metavar='RANGE', type=str, help='target genomic range, seq:lo-hi or just seq')
    parser.add_argument('-t','--token', metavar='XXXX', type=str, help='API auth token')
    parser.add_argument('-v', '--verbose', action='store_true', help='verbose log to standard error')
    parser.add_argument('-k', '--insecure', action='store_true', help='disable TLS certificate verification')
    parser.add_argument('namespace', type=str, help="accession namespace")
    parser.add_argument('accession', type=str, help="accession")
    parser.add_argument('format', type=str, nargs='?', default='BAM', choices=['BAM','bam','CRAM','cram','VCF','vcf'], help="format")
    args = parser.parse_args()
    args.format = args.format.upper()

    return get(args.namespace, args.accession, args.format, server=args.server,
               token=args.token, genomic_range=args.range, verbose=args.verbose,
               insecure=args.insecure)

if __name__ == '__main__':
   main()
