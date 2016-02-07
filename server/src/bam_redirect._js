"use strict";

const protocol = require('./protocol');
const Errors = protocol.Errors;

function bam(request, _) {
    return {message: "hello world"};
}

module.exports.register = (server, options, next) => {
    server.route({
        method: 'GET',
        path:'/bam/{namespace}/{accession}', 
        handler: protocol.handler(bam)
    });
    return next();
}

module.exports.register.attributes = {
    name: "BAMnexus BAM redirect route",
    version: "0.0.1"
}
