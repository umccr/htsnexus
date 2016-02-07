"use strict";
const Joi = require("joi");
const expect = require("expect.js");
const request = require("request");
const url = require("url");
const sqlite3 = require('sqlite3');
const Server = require("../src/server");

// series of sql statements to create the test database
var schema = [
    "create table htsfiles (_dbid text primary key, format text not null, namespace text not null, \
        accession text not null, url text not null)",
    "create unique index htsfiles_namespace_accession on htsfiles(namespace,accession)",
    "create table htsfiles_index_meta (_dbid text primary key, reference text not null, \
        header text not null, bamHeaderBGZF blob not null, foreign key(_dbid) references htsfiles(_dbid))",
    "create table htsfiles_index_entries (_dbid text not null, byteLo integer not null, byteHi integer not null, \
        seq text, seqLo integer not null, seqHi integer not null, foreign key(_dbid) references htsfiles_index_meta(_dbid))",
    "create index htsfiles_index1 on htsfiles_index_entries(_dbid,seq,seqLo,seqHi)",
    "create index htsfiles_index2 on htsfiles_index_entries(_dbid,seq,seqHi)",
    "insert into htsfiles values('ENCFF621SXE','bam','ENCODE','ENCFF621SXE', \
        'https://www.encodeproject.org/files/ENCFF621SXE/@@download/ENCFF621SXE.bam')"
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
