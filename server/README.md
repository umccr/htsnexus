# htsnexus server

The htsnexus server is a [Node.js](https://nodejs.org/en/) application written using the [Hapi.js](http://hapijs.com/) framework and [Streamline.js](https://github.com/Sage/streamlinejs) asynchronous syntax sugar. To serve client requests, it performs lookups in a SQLite3 database of data files and genomic range indices thereof. This database can be created using the [indexer](../indexer) utility.

The "interesting" portions of the server code, enabling the functions of htsnexus, are found mostly in [src/htsfiles_routes._js](src/htsfiles_routes._js); the other source files are mainly the supporting framework.

### Build and test

```make all test```

This will automatically download dependencies including the Node.js runtime and npm packages. (This currently assumes Linux x86-64; it could be generalized for other achitectures or OS X with minor efforts.) Then, it runs some simple unit tests using a tiny test database. More elaborate integration tests of the whole htsnexus system can be found in [../test](../test).

### Starting the server

Once the server has been built and unit-tested, [server.sh](server.sh) is the command-line entry point.

```
$ ./server.sh 

  Usage: server.sh [options] /path/to/database

  Options:

    -h, --help         output usage information
    -b, --bind [bind]  interface to bind; set 0.0.0.0 to bind all [127.0.0.1]
    -p, --port [port]  port to listen on [48444]
```
