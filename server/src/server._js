"use strict";
// create, configure, and start the htsnexus Hapi.js server

const Hapi = require("hapi");
const protocol = require("./protocol");

function handle404(request, _) { throw new protocol.Errors.NotFound(); }

module.exports.Start = (config,_) => {
    const server = new Hapi.Server();
    
    server.connection({ 
        address: config.bind || '127.0.0.1',
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
    server.register({register: require('./htsfiles_routes'), options: config}, _);

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
