"use strict";
// create, configure, and start the htsnexus Hapi.js server

const Hapi = require("@hapi/hapi");
const protocol = require("./protocol");


const init = async (config) => {
    const server = new Hapi.Server({
		  host: config.bind || '127.0.0.1',
		  port: config.port || 48444
	});
    
    // route modules
    server.route({
		handler: require('./htsfiles_routes'), 
		options: config
	});

    // 404 catch-all
    server.route({
        method: '*',
        path: '/{p*}',
        handler: protocol.handler(handle404)
    });

    // start!
    await server.start();
    return server;
}

function handle404(request, _) { throw new protocol.Errors.NotFound(); }

process.on('unhandledRejection', (err) => {
    console.log(err);
    process.exit(1);
});

init();
