"use strict";
const Joi = require("joi");
const expect = require("expect.js");
const request = require("request");
const url = require("url");
const sqlite3 = require('sqlite3');
const Server = require("../src/server");

// GET the protocol response from the server started for local testing
function req(route, cb) {
    request("http://localhost:48444" + route, (error, response, body) => {
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

    describe("/bam", function() {
        it("should report NotFound for a nonexistent item", function(_) {
            let res = req("/bam/ENCODE/ENC123456", _);
            expect(res.statusCode).to.be(404);
            expect(res.body.error.type).to.be("NotFound");
        });

        it("should serve the URL for a BAM", function(_) {
            let res = req("/bam/ENCODE/ENCFF621SXE", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.url).to.be.a('string');
        });

        it("should serve the URL and byte range for a genomic range slice", function(_) {
            let res = req("/bam/htsnexus_test/NA12878?range=20:6000000-6020000", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.url).to.be.a('string');
            expect(res.body.reference).to.be('GRCh37');
            expect(res.body.byteRange.lo).to.be(977196);
            expect(res.body.byteRange.hi).to.be(1165273);
            expect(res.body.httpRequestHeaders.range).to.be('bytes=977196-1165272');
            expect(res.body.bamHeaderBGZF).to.be(undefined);

            res = req("/bam/htsnexus_test/NA12878?range=20:5000000-6020000", _);
            expect(res.body.httpRequestHeaders.range).to.be('bytes=977196-1165272');
        });

        it("should serve the bamHeaderBGZF on request", function(_) {
            let res = req("/bam/htsnexus_test/NA12878?range=20:6000000-6020000&bamHeaderBGZF", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.url).to.be.a('string');
            expect(res.body.reference).to.be('GRCh37');
            expect(res.body.httpRequestHeaders.range).to.be('bytes=977196-1165272');
            expect(res.body.bamHeaderBGZF).to.be.a('string');
        });

        it("should serve the byte range for a whole reference sequence", function(_) {
            let res = req("/bam/htsnexus_test/NA12878?range=20&bamHeaderBGZF", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.url).to.be.a('string');
            expect(res.body.reference).to.be('GRCh37');
            expect(res.body.httpRequestHeaders.range).to.be('bytes=977196-2128165');
            expect(res.body.bamHeaderBGZF).to.be.a('string');
        });

        it("should serve the byte range for unmapped reads", function(_) {
            let res = req("/bam/htsnexus_test/NA12878?range=*", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.url).to.be.a('string');
            expect(res.body.reference).to.be('GRCh37');
            expect(res.body.httpRequestHeaders.range).to.be('bytes=2112141-2596770');
            expect(res.body.bamHeaderBGZF).to.be(undefined);
        });

        it("should serve empty result sets", function(_) {
            let res = req("/bam/htsnexus_test/NA12878?range=20:1-10000", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.byteRange).to.be(null);

            res = req("/bam/htsnexus_test/NA12878?range=XXX", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.byteRange).to.be(null);
        });

        it("should reject invalid ranges", function(_) {
            expect(req("/bam/htsnexus_test/NA12878?range=:1-2", _).statusCode).to.be(422);
            expect(req("/bam/htsnexus_test/NA12878?range=$:1-2", _).statusCode).to.be(422);
        });

        it("should report Unable to range-query an unindexed BAM", function(_) {
            let res = req("/bam/ENCODE/ENCFF621SXE?range=1", _);
            expect(res.statusCode).to.be(406);
        });
    });

    after(function(_) {
        if (server) {
            server.stop({timeout: 1000}, _);
        }
    });
});
