"use strict";
// BAMnexus server command-line entry point

require('streamline').register({});
const Server = require('./server');
const cmd = require("commander");
const sqlite3 = require('sqlite3')

var config = {
    port: 48444,
    db: new sqlite3.Database(':memory:')
}
Server.Start(config, (err, server) => {
    if (err) {
        throw err;
    }
    server.on("response", (request) => {
        let log = {
            remoteAddress: request.info.remoteAddress,
            path: request.path,
            statusCode: request.response.statusCode,
            duration: Date.now() - request.info.received
        }
        if (Object.keys(request.query).length > 0) {
            log.query = request.query;
        }
        if (Object.keys(request.params).length > 0) {
            log.params = request.params;
        }
        if (Object.keys(request.logDetails).length > 0) {
            log.details = request.logDetails;
        }
        console.log(JSON.stringify(log));
    });
    console.log(JSON.stringify({message: 'Server running at: '+ server.info.uri}));
});

