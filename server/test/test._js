"use strict";
const joi = require("joi");
const expect = require("expect.js");
const request = require("request");
const url = require("url");
const Server = require("../src/server");

function req(url, cb) {
    request(url, (error, response, body) => {
        if (error) {
            return cb(error);
        }
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

    describe("/hello", function() {
        it("should reply", function(_) {
            let res = req("http://localhost:48444/hello", _);
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
