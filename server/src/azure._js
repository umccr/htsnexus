"use strict";
// helpers for serving Azure blobs from htsnexus
const azureStorage = require('azure-storage');

var storageAccount;
var blobService;

module.exports.initialize = (credentials) => {
    if (credentials.AZURE_STORAGE_ACCOUNT && credentials.AZURE_STORAGE_ACCESS_KEY) {
        blobService = azureStorage.createBlobService(credentials.AZURE_STORAGE_ACCOUNT,
                                                     credentials.AZURE_STORAGE_ACCESS_KEY);
        storageAccount = credentials.AZURE_STORAGE_ACCOUNT;
    }
}

var regexpBlobUrl = new RegExp("https://([a-z0-9]+).blob.core.windows.net/([-a-z0-9]+)/([^?]+)");
// https://blogs.msdn.microsoft.com/jmstall/2014/06/12/azure-storage-naming-rules/
module.exports.isBlobUrl = (url) => {
    if (url.match(regexpBlobUrl)) {
        return true;
    }
    return false;
}

// given a blob URL *without* any SAS signature, add one as the query string.
module.exports.signBlobUrl = (url, expirationMinutes) => {
    const m = url.match(regexpBlobUrl);
    if (!m) {
        throw new Error("azure.signBlobUrl: invalid URL");
    }
    if (!blobService || m[1] !== storageAccount) {
        throw new Error("azure.signBlobUrl: no credentials for storage account " + m[1]);
    }
    const containerName = m[2];
    const blobName = m[3];
    
    const token = blobService.generateSharedAccessSignature(containerName, blobName, {
        AccessPolicy: {
            Permissions: azureStorage.BlobUtilities.SharedAccessPermissions.READ,
            Start: new Date(Date.now() - 1000),
            Expiry: new Date(Date.now() + expirationMinutes*60*1000)
        },
    });

    return blobService.getUrl(containerName, blobName, token);
}
