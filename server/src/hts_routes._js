"use strict";

const protocol = require('./protocol');
const Errors = protocol.Errors;

class HTSRoutes {
    constructor(db) {
        if (!db) {
            throw new Error("hts_routes: no SQLite3 database provided")
        }
        this.db = db;
    }

    bam(request, _) {
        let info = this.db.get("select * from htsfiles where format='bam' and namespace = ? and accession = ?",
                               request.params.namespace, request.params.accession, _);
        if (!info) {
            throw new Errors.ResourceNotFound();
        }
        return {url: info.url};
    }
}

module.exports.register = (server, config, next) => {
    let impl = new HTSRoutes(config.db);
    server.route({
        method: 'GET',
        path:'/bam/{namespace}/{accession}', 
        handler: protocol.handler((request, _) => impl.bam(request, _))
    });
    return next();
}

module.exports.register.attributes = {
    name: "htsnexus routes",
    version: "0.0.1"
}
