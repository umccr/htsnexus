"use strict";
// create, configure, and start the BAMnexus Hapi.js server

const Hapi = require("hapi");
const protocol = require("./protocol");

function handle404(request, _) { throw new protocol.Errors.ResourceNotFound(); }

module.exports.Start = (config,_) => {
    const server = new Hapi.Server();
    
    server.connection({ 
        host: 'localhost', 
        port: config.port || 48444
    });

    // request object initialization
    server.ext({
        type: 'onRequest',
        method: function (request, reply) {
            request.logDetails = {};
            return reply.continue();
        }
    });

    // route modules
    server.register(require('./bam_redirect'), _);

    // 404 catch-all
    server.route({
        method: '*',
        path: '/{p*}',
        handler: protocol.handler(handle404)
    });

    // start!
    server.start(_);
    return server;
}
