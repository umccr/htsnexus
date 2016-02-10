# htsnexus indexer

The htsnexus indexer utility is responsible for populating the SQLite database used by the htsnexus server. This typically includes scanning the data files to create a block-level genomic range index, although this is not required.

Only BAM files are supported right now. A little refactoring and further work will be needed to adapt the utility for CRAM and VCF/BCF files.

### Build

```
cmake . && make
```

Dependencies:

* gcc 4.8+
* cmake 2.8+
* SQLite3
