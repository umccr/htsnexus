"use strict";
const expect = require("expect.js");
const request = require("request");
const url = require("url");
const sqlite3 = require('sqlite3');
const Server = require("../src/server");

// GET the protocol response from the server started for local testing
function req(route, cb) {
    let rq = {
        url: "http://localhost:48444/v1" + route,
        headers: {origin: "https://www.dnanexus.com"}
    };
    request(rq, (error, response, body) => {
        if (error) {
            return cb(error);
        }
        expect(response.headers["content-type"]).to.match(/application\/json;?.*/);
        try {
            response.body = JSON.parse(body);
        } catch(e) {
            return cb(new Error("Invalid JSON response: " + body));
        }
        cb(null, response);
    });
}

describe("Server", function() {
    let server = null;
    before(function(_) {
        let db = new sqlite3.Database(__dirname + '/test.db');
        server = Server.Start({port: 48444, db: db}, _);
        server.on("response", (request) => {
            if (request.response.statusCode === 500) {
                console.dir(request.response.source);
            }
        });
    });

    describe("nonexistent route", function() {
        it("should return NotFound", function(_) {
            let res = req("/bogus", _);
            expect(res.statusCode).to.be(404);
            expect(res.body.error.type).to.be("NotFound");
        });
    });

    describe("GET reads", function() {
        it("should report NotFound for a nonexistent item", function(_) {
            let res = req("/reads/ENCODE/ENC123456", _);
            expect(res.statusCode).to.be(404);
            expect(res.body.error.type).to.be("NotFound");
        });

        it("should serve the URL for a BAM", function(_) {
            let res = req("/reads/ENCODE/ENCFF621SXE", _);
            expect(res.statusCode).to.be(200);
            expect(res.headers['access-control-allow-origin']).to.be('https://www.dnanexus.com');
            expect(res.body.urls[0]).to.be.a('string');
            expect(res.body.format).to.be('BAM');
            expect(res.body.byteRanges).to.be(undefined);

            // explicit format
            res = req("/reads/ENCODE/ENCFF621SXE?format=BAM", _);
            expect(res.statusCode).to.be(200);
            expect(res.headers['access-control-allow-origin']).to.be('https://www.dnanexus.com');
            expect(res.body.urls[0]).to.be.a('string');
            expect(res.body.format).to.be('BAM');
            expect(res.body.byteRanges).to.be(undefined);
        });

        it("should reject unspported formats", function(_) {
            let res = req("/reads/ENCODE/ENCFF621SXE?format=BOGUS", _);
            expect(res.statusCode).to.be(409);
            expect(res.body.error.type).to.be("UnsupportedFormat");
        });

        it("should serve the URL and byte range for a genomic range slice", function(_) {
            let res = req("/reads/htsnexus_test/NA12878?referenceName=20&start=6000000&end=6020000", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.urls[0]).to.be.a('string');
            expect(res.body.reference).to.be('GRCh37');
            expect(res.body.format).to.be('BAM');

            expect(res.body.prefix).to.be.a('string');
            let buf = new Buffer(res.body.prefix, 'base64');
            expect(buf[0]).to.be(0x1f);
            expect(buf[1]).to.be(0x8b);

            expect(res.body.byteRanges[0].start).to.be(977196);
            expect(res.body.byteRanges[0].end).to.be(1165273);

            expect(res.body.suffix).to.be.a('string');
            buf = new Buffer(res.body.suffix, 'base64');
            expect(buf[0]).to.be(0x1f);
            expect(buf[1]).to.be(0x8b);
            expect(buf.length).to.be(28);

            res = req("/reads/htsnexus_test/NA12878?format=BAM&referenceName=20&start=5000000&end=6020000", _);
            expect(res.body.byteRanges[0].start).to.be(977196);
            expect(res.body.byteRanges[0].end).to.be(1165273);
        });

        it("should suppress BAM header slice prefix on request", function(_) {
            let res = req("/reads/htsnexus_test/NA12878?referenceName=20&start=6000000&end=6020000&noHeaderPrefix", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.urls[0]).to.be.a('string');
            expect(res.body.format).to.be('BAM');
            expect(res.body.reference).to.be('GRCh37');
            expect(res.body.byteRanges[0].start).to.be(977196);
            expect(res.body.byteRanges[0].end).to.be(1165273);
            expect(res.body.prefix).to.be(undefined);
        });

        it("should serve the byte range for a whole reference sequence", function(_) {
            let res = req("/reads/htsnexus_test/NA12878?referenceName=20", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.urls[0]).to.be.a('string');
            expect(res.body.format).to.be('BAM');
            expect(res.body.reference).to.be('GRCh37');

            expect(res.body.prefix).to.be.a('string');
            let buf = new Buffer(res.body.prefix, 'base64');
            expect(buf[0]).to.be(0x1f);
            expect(buf[1]).to.be(0x8b);

            expect(res.body.suffix).to.be.a('string');
            buf = new Buffer(res.body.suffix, 'base64');
            expect(buf[0]).to.be(0x1f);
            expect(buf[1]).to.be(0x8b);
            expect(buf.length).to.be(28);
        });

        it("should serve the byte range for unmapped reads", function(_) {
            let res = req("/reads/htsnexus_test/NA12878?referenceName=*", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.urls[0]).to.be.a('string');
            expect(res.body.format).to.be('BAM');
            expect(res.body.reference).to.be('GRCh37');

            expect(res.body.prefix).to.be.a('string');
            let buf = new Buffer(res.body.prefix, 'base64');
            expect(buf[0]).to.be(0x1f);
            expect(buf[1]).to.be(0x8b);

            expect(res.body.suffix).to.be.a('string');
            buf = new Buffer(res.body.suffix, 'base64');
            expect(buf[0]).to.be(0x1f);
            expect(buf[1]).to.be(0x8b);
            expect(buf.length).to.be(28);
        });

        it("should serve empty result sets", function(_) {
            let res = req("/reads/htsnexus_test/NA12878?referenceName=20&start=1&end=10000", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.format).to.be('BAM');
            expect(res.body.urls.length).to.be(0);
            expect(res.body.byteRanges).to.be(undefined);

            expect(res.body.prefix).to.be.a('string');
            let buf = new Buffer(res.body.prefix, 'base64');
            expect(buf[0]).to.be(0x1f);
            expect(buf[1]).to.be(0x8b);

            expect(res.body.suffix).to.be.a('string');
            buf = new Buffer(res.body.suffix, 'base64');
            expect(buf[0]).to.be(0x1f);
            expect(buf[1]).to.be(0x8b);
            expect(buf.length).to.be(28);

            res = req("/reads/htsnexus_test/NA12878?referenceName=XXX", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.urls.length).to.be(0);
            expect(res.body.byteRanges).to.be(undefined);
        });

        it("should reject invalid ranges", function(_) {
            expect(req("/reads/htsnexus_test/NA12878?referenceName=20&start=XXX&end=2", _).statusCode).to.be(422);
            expect(req("/reads/htsnexus_test/NA12878?referenceName=20&start=2&end=1", _).statusCode).to.be(422);
        });

        it("should report Unable to range-query an unindexed BAM", function(_) {
            let res = req("/reads/ENCODE/ENCFF621SXE?referenceName=1", _);
            expect(res.statusCode).to.be(406);
        });

        it("should redirect to Heng Li's bamsvr", function(_) {
            let res = req("/reads/lh3bamsvr/EXA00001?format=BAM", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.format).to.be('BAM');
            expect(res.body.urls[0]).to.be.a('string');

            res = req("/reads/lh3bamsvr/EXA00001?referenceName=11:10899000&end=10900000", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.format).to.be('BAM');
            expect(res.body.prefix).to.be(undefined);
            expect(res.body.urls[0]).to.be.a('string');
            expect(res.body.byteRanges).to.be(undefined);
            expect(res.body.suffix).to.be(undefined);
        });
    });

    describe("GET reads CRAM", function() {
        it("should serve the URL for a CRAM", function(_) {
            let res = req("/reads/htsnexus_test/NA12878?format=CRAM", _);
            expect(res.statusCode).to.be(200);
            expect(res.headers['access-control-allow-origin']).to.be('https://www.dnanexus.com');
            expect(res.body.urls[0]).to.be.a('string');
            expect(res.body.format).to.be('CRAM');
            expect(res.body.byteRanges[0].start).to.be(0);
            expect(res.body.byteRanges[0].end).to.be(1661526);
        });

        it("should serve the URL and byte range for a genomic range slice", function(_) {
            let res = req("/reads/htsnexus_test/NA12878?format=CRAM&referenceName=20&start=6000000&end=6020000", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.urls[0]).to.be.a('string');
            expect(res.body.format).to.be('CRAM');
            expect(res.body.reference).to.be('GRCh37');

            expect(res.body.prefix).to.be.a('string');
            let buf = new Buffer(res.body.prefix, 'base64');
            expect(buf.slice(0,4).toString()).to.be('CRAM');

            expect(res.body.byteRanges[0].start).to.be(617115);
            expect(res.body.byteRanges[0].end).to.be(1094993);

            expect(res.body.suffix).to.be.a('string');
            buf = new Buffer(res.body.suffix, 'base64');
            expect(buf[0]).to.be(0x0f);
            expect(buf[1]).to.be(0x00);
            expect(buf.length).to.be(38);

            res = req("/reads/htsnexus_test/NA12878?format=CRAM&referenceName=20&start=5000000&end=6020000", _);
            expect(res.body.byteRanges[0].start).to.be(617115);
            expect(res.body.byteRanges[0].end).to.be(1094993);
        });

        it("should suppress CRAM header slice prefix on request", function(_) {
            let res = req("/reads/htsnexus_test/NA12878?format=CRAM&referenceName=20&start=6000000&end=6020000&noHeaderPrefix", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.urls[0]).to.be.a('string');
            expect(res.body.format).to.be('CRAM');
            expect(res.body.reference).to.be('GRCh37');
            expect(res.body.byteRanges[0].start).to.be(617115);
            expect(res.body.byteRanges[0].end).to.be(1094993);
            expect(res.body.prefix).to.be(undefined);
        });

        it("should serve the byte range for a whole reference sequence", function(_) {
            let res = req("/reads/htsnexus_test/NA12878?format=CRAM&referenceName=20", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.urls[0]).to.be.a('string');
            expect(res.body.format).to.be('CRAM');
            expect(res.body.reference).to.be('GRCh37');
            expect(res.body.byteRanges[0].start).to.be(617115);
            expect(res.body.byteRanges[0].end).to.be(1310237);

            expect(res.body.prefix).to.be.a('string');
            let buf = new Buffer(res.body.prefix, 'base64');
            expect(buf.slice(0,4).toString()).to.be('CRAM');

            expect(res.body.suffix).to.be.a('string');
            buf = new Buffer(res.body.suffix, 'base64');
            expect(buf[0]).to.be(0x0f);
            expect(buf[1]).to.be(0x00);
            expect(buf.length).to.be(38);
        });

        it("should serve the byte range for unmapped reads", function(_) {
            let res = req("/reads/htsnexus_test/NA12878?format=CRAM&referenceName=*", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.urls[0]).to.be.a('string');
            expect(res.body.format).to.be('CRAM');
            expect(res.body.reference).to.be('GRCh37');
            expect(res.body.byteRanges[0].start).to.be(1310237);
            expect(res.body.byteRanges[0].end).to.be(1661488);

            expect(res.body.prefix).to.be.a('string');
            let buf = new Buffer(res.body.prefix, 'base64');
            expect(buf.slice(0,4).toString()).to.be('CRAM');

            expect(res.body.suffix).to.be.a('string');
            buf = new Buffer(res.body.suffix, 'base64');
            expect(buf[0]).to.be(0x0f);
            expect(buf[1]).to.be(0x00);
            expect(buf.length).to.be(38);
        });

        it("should serve empty result sets", function(_) {
            let res = req("/reads/htsnexus_test/NA12878?format=CRAM&referenceName=20&start=1&end=10000", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.format).to.be('CRAM');
            expect(res.body.byteRanges).to.be(undefined);

            expect(res.body.prefix).to.be.a('string');
            let buf = new Buffer(res.body.prefix, 'base64');
            expect(buf.slice(0,4).toString()).to.be('CRAM');

            expect(res.body.suffix).to.be.a('string');
            buf = new Buffer(res.body.suffix, 'base64');
            expect(buf[0]).to.be(0x0f);
            expect(buf[1]).to.be(0x00);
            expect(buf.length).to.be(38);

            res = req("/reads/htsnexus_test/NA12878?format=CRAM&referenceName=XXX", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.byteRanges).to.be(undefined);
        });
    });

    after(function(_) {
        if (server) {
            server.stop({timeout: 1000}, _);
        }
    });
});
