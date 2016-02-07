"use strict";

const protocol = require('./protocol');
const Errors = protocol.Errors;

class Redirector {
    constructor(db) {
        if (!db) {
            throw new Error("bam_redirect: no SQLite3 database provided")
        }
        this.db = db;
    }

    bam(request, _) {
        let info = this.db.get("select * from bam where namespace = ? and accession = ?",
                               request.params.namespace, request.params.accession, _);
        if (!info) {
            throw new Errors.ResourceNotFound();
        }
        return {url: info.url};
    }
}

module.exports.register = (server, config, next) => {
    let redirector = new Redirector(config.db);
    server.route({
        method: 'GET',
        path:'/bam/{namespace}/{accession}', 
        handler: protocol.handler((request, _) => redirector.bam(request, _))
    });
    return next();
}

module.exports.register.attributes = {
    name: "BAMnexus BAM redirect route",
    version: "0.0.1"
}
