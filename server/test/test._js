"use strict";
const Joi = require("joi");
const expect = require("expect.js");
const request = require("request");
const url = require("url");
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
        server = Server.Start({port: 48444}, _);
    });

    describe("nonexistent route", function() {
        it("should return ResourceNotFound", function(_) {
            let res = req("/bogus", _);
            expect(res.statusCode).to.be(404);
            expect(res.body.error.type).to.be("ResourceNotFound");
        });
    });

    describe("/bam", function() {
        it("should reply", function(_) {
            let res = req("/bam/ENCODE/ENC123456", _);
            expect(res.statusCode).to.be(200);
            expect(res.body.message).to.be("hello world");
        });
    });

    after(function(_) {
        if (server) {
            server.stop({timeout: 1000}, _);
        }
    });
});
