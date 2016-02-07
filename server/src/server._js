"use strict";
const Hapi = require("hapi");

module.exports.Start = (config,_) => {
    const server = new Hapi.Server();
    
    server.connection({ 
        host: 'localhost', 
        port: config.port || 48444
    });

    server.route({
        method: 'GET',
        path:'/hello', 
        handler: function (request, reply) {
            return reply({message: "hello world"});
        }
    });

    server.start(_);
    return server;
}
