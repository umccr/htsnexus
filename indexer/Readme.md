# htsnexus indexer

The htsnexus indexer utility is responsible for populating the SQLite database used by the htsnexus server. This typically includes scanning the data files to create a block-level genomic range index, although this is not required.

Only BAM and CRAM files are supported right now. A little refactoring and further work will be needed to adapt the utility for VCF/BCF files too.

### Build

```
cmake . && make
```

Dependencies:

* gcc 4.8+
* cmake 2.8+
* SQLite3

### Usage

```
htsnexus_index_bam [options] <index.db> <namespace> <accession> <local_file> <url>
  index.db    SQLite3 database (will be created if nonexistent)
  namespace   accession namespace
  accession   accession identifier
  local_file  filename to local copy of BAM
  url         BAM URL to serve to clients
The BAM file is added to the database (without a block-level range index)
based on the above information.
Options:
  --reference <id>  generate the block-level range index and associate it with
                    this (arbitrary, server-specific) reference genome ID

```
