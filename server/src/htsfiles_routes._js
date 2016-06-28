"use strict";

const protocol = require('./protocol');
const Errors = protocol.Errors;

let MAX_SAFE_INTEGER = 9007199254740991;
// genomic range parsing
let re_genomicRange = /^([A-Za-z0-9._*-]+)(:([0-9]+)-([0-9]+))?$/;
function resolveGenomicRange(query) {
    let ans = {
        seq: query.referenceName,
        lo: (query.start === undefined ? 0 : parseInt(query.start)),
        hi: (query.end === undefined ? MAX_SAFE_INTEGER : parseInt(query.end))
    }
    if (isNaN(ans.lo) || isNaN(ans.hi)) {
        throw new Errors.InvalidInput("invalid positions in genomic range");
    }
    if (ans.lo < 0 || ans.hi < ans.lo) {
        throw new Errors.InvalidInput("invalid genomic range; end<start");
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
            // format was given to us in lowercase for legacy reasons (reuse v0 database for v1 server)
            format: format.toUpperCase(),
            urls: [{
                url: info.url,
                headers: {
                  "referer": request.connection.info.protocol + '://' + request.info.host + request.url.path
                }
            }]
        };

        if (typeof info.file_size === 'number') {
            ans.urls[0].headers.range = "bytes=" + 0 + "-" + (info.file_size-1);
        }

        // genomic range slicing
        if (request.query.referenceName) {
            let genomicRange = resolveGenomicRange(request.query);

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

            if (rslt['count(*)']>0) {
                let lo = rslt['min(byteLo)'];
                let hi = rslt['max(byteHi)'];
                // formulate HTTP byte range header (fully closed)
                ans.urls[0].headers.range = "bytes=" + lo + "-" + (hi-1);
            } else {
                // empty result set
                ans.urls = [];
            }

            // TODO: handle block_suffix as well.
            if (meta.slice_suffix !== null) {
                ans.suffix = meta.slice_suffix.toString('base64');
            }
        }

        // TODO: decompose the byte range into 1GB (or whatever) chunks, to help
        // clients retry & resume

        return ans;
    }

    getReads(request, _) {
        if (request.query.format === undefined || request.query.format === "BAM") {
            if (request.params.namespace == "lh3bamsvr") {
                return this.lh3bamsvr(request, _);
            }
            return this.htsfiles_common(request, 'bam', _);
        } else if (request.query.format === "CRAM") {
            return this.htsfiles_common(request, 'cram', _);
        }
        throw new Errors.UnsupportedFormat("Unrecognized/unsupported format: " + request.query.format);
    }

    // special handling for the "lh3bamsvr" namespace, which we redirect to
    // Heng Li's bamsvr
    lh3bamsvr(request, _) {
        let ans = {
            namespace: request.params.namespace,
            accession: request.params.accession,
            urls: [{url: "http://bamsvr.herokuapp.com/get?ac=" + encodeURIComponent(request.params.accession)}],
            format: "BAM"
        }

       if (request.query.referenceName) {
            let genomicRange = resolveGenomicRange(request.query);
            ans.urls[0].url += "&seq=" + encodeURIComponent(genomicRange.seq) +
                               "&start=" + genomicRange.lo + "&end=" + genomicRange.hi;
        }

        return ans;
    }
}

module.exports.register = (server, config, next) => {
    let impl = new HTSRoutes(config.db);
    server.route({
        method: 'GET',
        path:'/v1/reads/{namespace}/{accession}',
        handler: protocol.handler((request, _) => impl.getReads(request, _)),
        config: {cors: true}
    });
    return next();
}

module.exports.register.attributes = {
    name: "htsnexus routes",
    version: "0.0.1"
}
