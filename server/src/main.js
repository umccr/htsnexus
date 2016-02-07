"use strict";
require('streamline').register({});
const Server = require('./server');
const cmd = require("commander");

Server.Start({port: 48444}, (err, server) => {
    if (err) {
        throw err;
    }
    console.log('Server running at:', server.info.uri);
});

