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
def get_ticket(namespace, accession, format, server=DEFAULT_SERVER, genomic_range=None, verbose=False):
    # construct query URL
    query_url = '/'.join([server, 'v0', 'data', urllib.quote(namespace), urllib.quote(accession), format])
    if genomic_range:
        query_url = query_url + '?range=' + urllib.quote(genomic_range)
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
        # don't print big base64 buffers to console...
        if 'prefix' in response_copy:
            response_copy['prefix'] = '[' + str(len(response_copy['prefix'])) + ' base64 characters]'
        if 'suffix' in response_copy:
            response_copy['suffix'] = '[' + str(len(response_copy['suffix'])) + ' base64 characters]'
        print >>sys.stderr, ('Response: ' + json.dumps(response_copy, indent=2, separators=(',', ': ')))
    return ans

def get(namespace, accession, format, verbose=False, **kwargs):
    # get ticket
    ticket = get_ticket(namespace, accession, format, verbose=verbose, **kwargs)

    # emit the prefix blob, if the ticket so instructs us; this typically consists
    # of the file's header when taking a genomic range slice.
    if 'prefix' in ticket:
        sys.stdout.write(base64.b64decode(ticket['prefix']))
        sys.stdout.flush()

    # pipe the raw data (unless the result genomic range slice is empty)
    if 'byteRange' not in ticket or ticket['byteRange'] is not None:
        # delegate to curl to access the URL given in the ticket, including any
        # HTTP request headers htsnexus instructed us to supply.
        curlcmd = ['curl','-LSs']
        if 'httpRequestHeaders' in ticket:
            for k, v in ticket['httpRequestHeaders'].items():
                curlcmd.append('-H')
                curlcmd.append(str(k + ': ' + v))
        # add the byte range header if we're slicing
        if 'byteRange' in ticket:
            curlcmd.append('-H')
            curlcmd.append('range: bytes=' + str(ticket['byteRange']['start']) + '-' + str(ticket['byteRange']['end']-1))
        curlcmd.append(ticket['url'])
        if verbose:
            print >>sys.stderr, ('Piping: ' + str(curlcmd))
            sys.stderr.flush()
        subprocess.check_call(curlcmd)

    # emit the suffix blob, if the ticket so instructs us; this typically consists
    # of the format-defined EOF marker when taking a genomic range slice.
    if 'suffix' in ticket:
        sys.stdout.write(base64.b64decode(ticket['suffix']))

    if verbose:
        print >>sys.stderr, 'Success'

def main():
    parser = argparse.ArgumentParser(description='htsnexus streaming client')
    parser.add_argument('-s','--server', metavar='URL', type=str, default=DEFAULT_SERVER, help='htsnexus server endpoint')
    parser.add_argument('-r','--range', metavar='RANGE', type=str, help='target genomic range, seq:lo-hi or just seq')
    parser.add_argument('-v', '--verbose', action='store_true', help='verbose log to standard error')
    parser.add_argument('namespace', type=str, help="accession namespace")
    parser.add_argument('accession', type=str, help="accession")
    parser.add_argument('format', type=str, nargs='?', default='bam', choices=['bam','cram'], help="format (default: bam)")
    args = parser.parse_args()

    return get(args.namespace, args.accession, args.format,
               server=args.server, genomic_range=args.range, verbose=args.verbose)

if __name__ == '__main__':
   main()
