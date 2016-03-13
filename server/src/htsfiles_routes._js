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
    if (ans.lo < 0 || ans.hi < ans.lo) {
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

    // serving/slicing logic common to format-specific routes
    htsfiles_common(request, format, _) {
        let info = this.db.get("select * from htsfiles where format = ? and namespace = ? and accession = ?",
                               format, request.params.namespace, request.params.accession, _);
        if (!info) {
            throw new Errors.NotFound();
        }

        let ans = {
            namespace: request.params.namespace,
            accession: request.params.accession,
            format: format,
            url: info.url,
            httpRequestHeaders: {
                "referer": request.connection.info.protocol + '://' + request.info.host + request.url.path
            }
        };

        if (typeof info.file_size === 'number') {
            ans.size = info.file_size;
        }

        // genomic range slicing
        if (request.query.range) {
            let genomicRange = parseGenomicRange(request.query.range);

            // query for index metadata (will fail if we don't have the file indexed)
            let meta = this.db.get("select htsfiles._dbid, reference, slice_prefix, slice_suffix from htsfiles, htsfiles_blocks_meta where htsfiles._dbid = htsfiles_blocks_meta._dbid and format = ? and namespace = ? and accession = ?",
                                    format, ans.namespace, ans.accession, _);
            if (!meta) {
                throw new Errors.Unable("No genomic range index available for the requested file.");
            }
            ans.reference = meta.reference;

            // Calculate the byte range of BGZF blocks overlapping the query
            // genomic range. The query probably has to scan index entries for
            // all blocks in the file. In the future, we could implement a
            // more efficient indexing strategy, such as UCSC binning, perhaps
            // using SQL views.
            let rslt;
            if (genomicRange.seq !== '*') {
                rslt = this.db.get("select count(*), min(byteLo), max(byteHi) from htsfiles_blocks where _dbid = ? and seq = ? and not (seqLo > ? or seqHi < ?)",
                                   meta._dbid, genomicRange.seq, genomicRange.hi, genomicRange.lo, _);
            } else {
                // unmapped reads
                rslt = this.db.get("select count(*), min(byteLo), max(byteHi) from htsfiles_blocks where _dbid = ? and seq is null",
                                   meta._dbid, _);
            }

            // TODO: handle block_prefix too. it'll be slightly tricky to get
            // block_prefix and block_suffix from the above aggregation query.
            // http://stackoverflow.com/a/17319622
            if (meta.slice_prefix !== null && request.query['noHeaderPrefix'] === undefined) {
                ans.prefix = meta.slice_prefix.toString('base64');
            }

            ans.byteRange = null;
            if (rslt['count(*)']>0) {
                let lo = rslt['min(byteLo)'];
                let hi = rslt['max(byteHi)'];
                // reporting byteRange as zero-based, half-open
                ans.byteRange = { start : lo, end : hi };
            }
            // else: empty result set; ans.byteRange remains null

            // TODO: handle block_suffix as well.
            if (meta.slice_suffix !== null) {
                ans.suffix = meta.slice_suffix.toString('base64');
            }
        }

        return ans;
    }

    bam(request, _) {
        if (request.params.namespace == "lh3bamsvr") {
            return this.bam_lh3bamsvr(request, _);
        }

        return this.htsfiles_common(request, 'bam', _);
    }

    // special handling for the "lh3bamsvr" namespace, which we redirect to
    // Heng Li's bamsvr
    bam_lh3bamsvr(request, _) {
        let ans = {
            namespace: request.params.namespace,
            accession: request.params.accession,
            url: "http://bamsvr.herokuapp.com/get?ac=" + encodeURIComponent(request.params.accession),
            format: "bam"
        }

        if (request.query.range) {
            let genomicRange = parseGenomicRange(request.query.range);
            ans.url += "&chr=" + encodeURIComponent(genomicRange.seq) +
                       "&start=" + genomicRange.lo + "&end=" + genomicRange.hi;
        }

        return ans;
    }

    cram(request, _) {
        return this.htsfiles_common(request, 'cram', _);
    }
}

module.exports.register = (server, config, next) => {
    let impl = new HTSRoutes(config.db);
    server.route({
        method: 'GET',
        path:'/v0/data/{namespace}/{accession}/bam',
        handler: protocol.handler((request, _) => impl.bam(request, _)),
        config: {cors: true}
    });
    server.route({
        method: 'GET',
        path:'/v0/data/{namespace}/{accession}/cram',
        handler: protocol.handler((request, _) => impl.cram(request, _)),
        config: {cors: true}
    });
    return next();
}

module.exports.register.attributes = {
    name: "htsnexus routes",
    version: "0.0.1"
}
