"use strict";
// Supporting stuff for our JSON RPC protocol

// protocol errors that can be raised by route handlers
module.exports.Errors = {};

class ProtocolError extends Error {
    constructor(msg) {
        super(msg);
        this.message = msg;
        this.errorType = "InternalError";
        this.statusCode = 500;
    }
}

class Unable extends ProtocolError {
    constructor(msg) {
        super(msg);
        this.errorType = "Unable";
        this.statusCode = 406;
    }
}
module.exports.Errors.Unable = Unable;

class InvalidInput extends ProtocolError {
    constructor(msg) {
        super(msg);
        this.errorType = "InvalidInput";
        this.statusCode = 422;
    }
}
module.exports.Errors.InvalidInput = InvalidInput;

class NotFound extends ProtocolError {
    constructor(msg) {
        super(msg);
        this.errorType = "NotFound";
        this.statusCode = 404;
    }
}
module.exports.Errors.NotFound = NotFound;

// wraps a route handler function f(request, _) for use with Hapi.js e.g.
// server.route({method 'GET', path: '/hello', handler: protocol.handler(f)});
function handler(f) {
    return (request, reply) => {
        f(request, (err,ans) => {
            let rep = null;
            if (err) {
                let body = {type: "InternalError"};
                let statusCode = 500;
                if (err instanceof ProtocolError) {
                    body.type = err.errorType;
                    statusCode = err.statusCode;
                }
                if (err.message) {
                    body.message = err.message;
                }
                rep = reply(JSON.stringify({error: body}));
                rep.statusCode = statusCode;
                if (err.statusCode == 500 && err.stack) {
                    request.logDetails.message = err.message;
                    request.logDetails.stack = err.stack;
                }
            } else {
                rep = reply(JSON.stringify(ans));
                rep.statusCode = 200;
            }
            rep.headers["content-type"] = "application/json";
        });        
    }
}
module.exports.handler = handler;
