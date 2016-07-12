"use strict";

const assert = require('assert');
const protocol = require('./protocol');
const Errors = protocol.Errors;

const MAX_POS = (1<<30);
function resolveGenomicRange(query) {
    let ans = {
        seq: query.referenceName,
        lo: (query.start === undefined ? 0 : parseInt(query.start)),
        hi: (query.end === undefined ? MAX_POS : parseInt(query.end))
    }
    if (isNaN(ans.lo) || isNaN(ans.hi) || ans.hi > MAX_POS) {
        throw new Errors.InvalidInput("invalid positions in genomic range");
    }
    if (ans.lo < 0 || ans.hi < ans.lo) {
        throw new Errors.InvalidInput("invalid genomic range; end<start");
    }
    return ans;
}

// offsets of bin numbers at each level of the bin index
const binOffsets = [1+16+256+4096, 1+16+256, 1+16, 1, 0];

class HTSRoutes {
    constructor(db) {
        if (!db) {
            throw new Error("htsfiles_routes: no SQLite3 database provided")
        }
        this.db = db;
    }

    validate_db(_) {
        let block_meta_count = 0;
        try {
            block_meta_count = this.db.get("select count(*) as ct from htsfiles_blocks_meta", _).ct;
        } catch (exn) {
            throw new Error("Invalid database: " + exn.toString());
        }
        if (block_meta_count > 0) {
            if (!this.db.get("pragma index_info(htsfiles_blocks_bin_index)", _)) {
                throw new Error("Database must be indexed using htsnexus_bin_index.py");
            }
        }
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
            assert(info.file_size > 0);
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

            let rslt;
            if (genomicRange.seq !== '*') {
                // Calculate the byte range of BGZF blocks overlapping the query
                // genomic range, searching all pertinent bins.
                rslt = this.db.get("select count(*), min(byteLo), max(byteHi) from htsfiles_blocks where _dbid = ? and seq = ? and ((seqBin between ? and ?) or (seqBin between ? and ?) or (seqBin between ? and ?) or (seqBin between ? and ?) or seqBin = 0) and not (seqLo > ? or seqHi < ?)",
                                   meta._dbid, genomicRange.seq,
                                   (genomicRange.lo>>14)+binOffsets[0], (genomicRange.hi>>14)+binOffsets[0],
                                   (genomicRange.lo>>18)+binOffsets[1], (genomicRange.hi>>18)+binOffsets[1],
                                   (genomicRange.lo>>22)+binOffsets[2], (genomicRange.hi>>22)+binOffsets[2],
                                   (genomicRange.lo>>26)+binOffsets[3], (genomicRange.hi>>26)+binOffsets[3],
                                   genomicRange.hi, genomicRange.lo, _);
            } else {
                // unmapped reads
                rslt = this.db.get("select count(*), min(byteLo), max(byteHi) from htsfiles_blocks where _dbid = ? and seq is null",
                                   meta._dbid, _);
            }

            if (rslt['count(*)']>0) {
                let lo = rslt['min(byteLo)'];
                let hi = rslt['max(byteHi)'];
                assert(lo >= 0 && hi > lo);
                // formulate HTTP byte range header (fully closed)
                ans.urls[0].headers.range = "bytes=" + lo + "-" + (hi-1);
            } else {
                // empty result set
                ans.urls = [];
            }

            // TODO: handle block_prefix too. it'll be slightly tricky to get
            // block_prefix and block_suffix from the above aggregation query.
            // http://stackoverflow.com/a/17319622
            if (meta.slice_prefix !== null && request.query['noHeaderPrefix'] === undefined) {
                ans.urls.unshift({url: "data:application/octet-stream;base64," + meta.slice_prefix.toString('base64')});
            }

            // TODO: handle block_suffix as well.
            if (meta.slice_suffix !== null) {
                ans.urls.push({url: "data:application/octet-stream;base64," + meta.slice_suffix.toString('base64')});
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

    getVariants(request, _) {
        if (request.query.format === undefined || request.query.format === "VCF") {
            return this.htsfiles_common(request, 'vcf', _);
        }
        throw new Errors.UnsupportedFormat("Unrecognized/unsupported format: " + request.query.format);
    }
}

module.exports.register = (server, config, next) => {
    let impl = new HTSRoutes(config.db)
    impl.validate_db((err) => {
        if (err) {
            throw err;
        }
        server.route({
            method: 'GET',
            path:'/v1/reads/{namespace}/{accession}',
            handler: protocol.handler((request, _) => impl.getReads(request, _)),
            config: {cors: true}
        });
        server.route({
            method: 'GET',
            path:'/v1/variants/{namespace}/{accession}',
            handler: protocol.handler((request, _) => impl.getVariants(request, _)),
            config: {cors: true}
        });
        return next();
    });
}

module.exports.register.attributes = {
    name: "htsnexus routes",
    version: "0.0.1"
}
