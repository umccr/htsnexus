"use strict";
const Joi = require("joi");
const expect = require("expect.js");
const request = require("request");
const url = require("url");
const sqlite3 = require('sqlite3');
const Server = require("../src/server");

// series of sql statements to create the test database
var schema = [
    "create table bam (_dbid text primary key, namespace text not null, accession text not null, \
        url text not null, rangeIndexed integer not null, bamHeaderBGZF blob)",
    "create unique index bamu on bam(namespace,accession)",
    "create table bam_range_index(_dbid text not null, byteLo integer not null, byteHi integer not null, \
        seq text, seqLo integer not null, seqHi integer not null)",
    "create index bamri on bam_range_index(_dbid,seq,seqLo,seqHi)",
    "create index bamri2 on bam_range_index(_dbid,seq,seqHi)",
    "insert into bam values('ENCFF621SXE','ENCODE','ENCFF621SXE', \
        'https://www.encodeproject.org/files/ENCFF621SXE/@@download/ENCFF621SXE.bam',0,null)"
];

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
        let db = new sqlite3.Database(':memory:');
        schema.forEach_(_, (_, stmt) => db.run(stmt, _));
        server = Server.Start({port: 48444, db: db}, _);
    });

    describe("nonexistent route", function() {
        it("should return ResourceNotFound", function(_) {
            let res = req("/bogus", _);
            expect(res.statusCode).to.be(404);
            expect(res.body.error.type).to.be("ResourceNotFound");
        });
    });

    describe("/bam", function() {
        it("should throw ResourceNotFound for a nonexistent item", function(_) {
            let res = req("/bam/ENCODE/ENC123456", _);
            expect(res.statusCode).to.be(404);
            expect(res.body.error.type).to.be("ResourceNotFound");
        });

        it("should provide the URL for a BAM", function(_) {
            let res = req("/bam/ENCODE/ENCFF621SXE", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.url).to.be.a('string');
        });
    });

    after(function(_) {
        if (server) {
            server.stop({timeout: 1000}, _);
        }
    });
});
