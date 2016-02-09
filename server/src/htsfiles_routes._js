"use strict";

const protocol = require('./protocol');
const Errors = protocol.Errors;

let MAX_SAFE_INTEGER = 9007199254740991;
// genomic range parsing
let re_genomicRange = /^([A-Za-z0-9._*-]+)(:([0-9]+)-([0-9]+))?$/;
function parseGenomicRange(str) {
    let m = str.match(re_genomicRange);
    if (!m) {
        throw new Errors.InvalidInput("invalid genomic range: " + str);
    }
    let ans = {
        seq: m[1],
        lo: (m[2] === undefined ? 0 : parseInt(m[3])),
        hi: (m[2] === undefined ? MAX_SAFE_INTEGER : parseInt(m[4]))
    }
    if (ans.hi < ans.lo) {
        throw new Errors.InvalidInput("invalid genomic range; hi<lo: " + str);
    }
    return ans;
}

class HTSRoutes {
    constructor(db) {
        if (!db) {
            throw new Error("htsfiles_routes: no SQLite3 database provided")
        }
        this.db = db;
    }

    bam(request, _) {
        let info = this.db.get("select * from htsfiles where format='bam' and namespace = ? and accession = ?",
                               request.params.namespace, request.params.accession, _);
        if (!info) {
            throw new Errors.NotFound();
        }

        let ans = {
            namespace: request.params.namespace,
            accession: request.params.accession,
            url: info.url
        };

        // genomic range slicing
        if (request.query.range) {
            let genomicRange = parseGenomicRange(request.query.range);

            // query for index metadata (will fail if we don't have the file indexed)
            let meta = this.db.get("select htsfiles._dbid, reference, bamHeaderBGZF from htsfiles, htsfiles_blocks_meta where htsfiles._dbid = htsfiles_blocks_meta._dbid and namespace = ? and accession = ?",
                                    ans.namespace, ans.accession, _);
            if (!meta) {
                throw new Errors.Unable("No block-level index available for the requested file.");
            }
            ans.reference = meta.reference;
            if (request.query['bamHeaderBGZF'] !== undefined) {
                ans.bamHeaderBGZF = meta.bamHeaderBGZF.toString('base64');
            }

            // calculate the byte range of BGZF blocks overlapping the query
            // genomic range
            let rslt;
            if (genomicRange.seq !== '*') {
                rslt = this.db.get("select count(*), min(byteLo), max(byteHi) from htsfiles_blocks where _dbid = ? and seq = ? and not (seqLo > ? or seqHi < ?)",
                                   meta._dbid, genomicRange.seq, genomicRange.hi, genomicRange.lo, _);
            } else {
                // unmapped reads
                rslt = this.db.get("select count(*), min(byteLo), max(byteHi) from htsfiles_blocks where _dbid = ? and seq is null",
                                   meta._dbid, _);
            }
            if (rslt['count(*)']>0) {
                let lo = rslt['min(byteLo)'];
                let hi = rslt['max(byteHi)'];
                // reporting byteRange as zero-based, half-open
                ans.byteRange = { lo : lo, hi : hi };
                // corresponding HTTP request header is zero-based, closed
                ans.httpRequestHeaders = {range: "bytes=" + lo + "-" + (hi-1)};
            } else {
                // empty result set
                ans.byteRange = null;
            }
        }

        return ans;
    }
}

module.exports.register = (server, config, next) => {
    let impl = new HTSRoutes(config.db);
    server.route({
        method: 'GET',
        path:'/bam/{namespace}/{accession}', 
        handler: protocol.handler((request, _) => impl.bam(request, _))
    });
    return next();
}

module.exports.register.attributes = {
    name: "htsnexus routes",
    version: "0.0.1"
}
