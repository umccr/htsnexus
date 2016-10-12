"use strict";
// htsnexus server command-line entry point

require('streamline').register({});
const Server = require('./server');
const program = require('commander');
const sqlite3 = require('sqlite3');
const fs = require('fs');

program._name = 'server.sh';
program
    .usage('[options] /path/to/database')
    .option('-b, --bind [bind]', 'interface to bind; set 0.0.0.0 to bind all [127.0.0.1]', '127.0.0.1')
    .option('-p, --port [port]', 'port to listen on [48444]', 48444)
    .option('--credentials [creds.json]', 'cloud service credentials')
    .parse(process.argv);
if (program.args.length != 1) {
    program.help();
}

if (program.credentials) {
    let credentials = JSON.parse(fs.readFileSync(program.credentials));
    require('azure').initialize(credentials);
}

var config = {
    bind: program.bind,
    port: parseInt(program.port),
    db: new sqlite3.Database(program.args[0], sqlite3.OPEN_READONLY)
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

