# htsnexus

**Experimental web service for accessing and slicing bam/cram/vcf.bgzip/bcf data** <a href="https://travis-ci.org/dnanexus-rnd/htsnexus"><img src="https://travis-ci.org/dnanexus-rnd/htsnexus.svg?branch=master"/></a>

This in an incubating proposal within the [GA4GH data working group](http://ga4gh.org/) towards a pragmatic web protocol for accessing large genomic datasets like reads and variants. Please see [this annotated slide deck](https://docs.google.com/a/dnanexus.com/presentation/d/1iATx04kwPz9V8-x_S4-eXmUbHJQt-AkOu4BL_xaE2nc/edit?usp=sharing) for further introduction and context.

### Try it out!

First, make the htsnexus client tool ready. This is a ~50 SLOC Python script. You'll also need [samtools](http://www.htslib.org/) and [curl](https://curl.haxx.se/).

```
curl https://raw.githubusercontent.com/dnanexus-rnd/htsnexus/master/client/htsnexus.py > htsnexus
chmod +x htsnexus
```

Here are a few things you can do with htsnexus and samtools:

*Download a 1000 Genomes BAM*
```bash
./htsnexus 1000genomes_low_coverage NA20276 > NA20276.bam
```

*Slice a genomic range out of a Platinum Genomes BAM*

```bash
./htsnexus -r chr12:111766922-111817529 platinum NA12878 > NA12878_ALDH2.bam
```

*Count reads on chr21 in an ENCODE ChIP-seq BAM*

```bash
./htsnexus -r chr21 ENCODE ENCFF493UYW | samtools view -c -
```

*Stream reads from Heng Li's bamsvr and display as headered SAM*

```bash
./htsnexus -r 11:10899000-10900000 lh3bamsvr EXA00001 | samtools view -h - | less -S
```

The htsnexus client tool simply emits BAM to standard output, which can be redirected to a file or piped into samtools. It delivers a well-formed BAM file, with the proper header, even when slicing a genomic range. Here are the data accessions currently available:

| namespace | accession |
| --- | --- |
| **1000genomes_low_coverage** <br/> Low-coverage whole-genome sequencing of ASW individuals from 1000 Genomes | NA19625 NA19700 NA19701 NA19703 NA19704 NA19707 NA19711 NA19712 NA19713 NA19818 NA19819 NA19834 NA19835 NA19900 NA19901 NA19904 NA19908 NA19909 NA19913 NA19914 NA19916 NA19917 NA19920 NA19921 NA19922 NA19923 NA19982 NA19984 NA19985 NA20126 NA20127 NA20274 NA20276 NA20278 NA20281 NA20282 NA20287 NA20289 NA20291 NA20294 NA20296 NA20298 NA20299 NA20314 NA20317 NA20318 NA20320 NA20321 NA20322 NA20332 NA20334 NA20336 NA20339 NA20340 NA20341 NA20342 NA20344 NA20346 NA20348 NA20351 NA20355 NA20356 NA20357 NA20359 NA20362 NA20412 |
| **platinum** <br/> Illumina Platinum Genomes stored at EBI | NA12877 NA12878 NA12879 NA12881 NA12882 NA12883 NA12884 NA12885 NA12886 NA12887 NA12888 NA12889 NA12890 NA12891 NA12892 NA12893 |
| **ENCODE** <br/> ChIP-seq data released by the ENCODE DCC in Feb 2016 | ENCFF483JAH ENCFF493UYW ENCFF534KZJ ENCFF563WQV ENCFF572PHE ENCFF713QBG ENCFF777NDR ENCFF840IOC |
| **lh3bamsvr** <br/> Heng's examples | EXA00001 EXA00002 |

(We recognize that a directory API method is needed instead of this Markdown table...)

At this moment only BAM files are supported as proof-of-concept; the other formats can be enabled with reasonable efforts.

### How's it work?

You can get a feel for how htsnexus works by running it in verbose mode:

```
$ ./htsnexus -v 1000genomes_low_coverage NA20276 > /dev/null
Query URL: http://htsnexus.rnd.dnanex.us/bam/1000genomes_low_coverage/NA20276
Response: {
  "url": "https://s3.amazonaws.com/1000genomes/phase3/data/NA20276/alignment/NA20276.mapped.ILLUMINA.bwa.ASW.low_coverage.20120522.bam",
  "namespace": "1000genomes_low_coverage",
  "accession": "NA20276"
}
Piping: ['curl', '-LSs', u'https://s3.amazonaws.com/1000genomes/phase3/data/NA20276/alignment/NA20276.mapped.ILLUMINA.bwa.ASW.low_coverage.20120522.bam']
```

The htsnexus client makes the request to an API server. The server's JSON response gives the client another URL at which it can access the desired data, in this case a BAM file within the AWS mirror of 1000 Genomes. Then, the client delegates to curl to download that file.

How about when we slice a genomic range? This is slightly more complicated.

```
$ ./htsnexus -v -r chr12:111766922-111817529 platinum NA12878 | wc -c
Query URL: http://htsnexus.rnd.dnanex.us/bam/platinum/NA12878?bamHeaderBGZF&range=chr12%3A111766922-111817529
Response: {
  "byteRange": {
    "lo": 81273195157,
    "hi": 81275238266
  },
  "httpRequestHeaders": {
    "range": "bytes=81273195157-81275238265"
  },
  "reference": "hg19",
  "bamHeaderBGZF": "[704 base64 characters]",
  "url": "http://ftp.era.ebi.ac.uk/vol1/ERA172/ERA172924/bam/NA12878_S1.bam",
  "namespace": "platinum",
  "accession": "NA12878"
}
Piping: ['curl', '-LSs', '-H', 'range: bytes=81273195157-81275238265', u'http://ftp.era.ebi.ac.uk/vol1/ERA172/ERA172924/bam/NA12878_S1.bam']
Success
2043663
```

The server tells the client to access the Platinum Genomes BAM file from EBI, but furthermore to access a specific *byte* range containing the desired genomic range. Thus the server handles the genomic range lookup, so the client doesn't need index files. This will benefit genome browsers, which today sometimes have to fetch a ~100 MiB BAI file in order to take a far smaller BAM slice for visualization. (The server also gives the client the BAM header, making it a bit easier to deliver a well-formed BAM.)

The htsnexus server maintains metadata and genomic range indices, but doesn't necessarily have to store or transport the data files themselves. This means it can be operated quite frugally, with heavy lifting offloaded to general-purpose services like S3. Architecturally, one htsnexus server could probably support thousands of clients engaged in a large scale analysis (not saying the current implementation is there). At the same time, should the need arise to generate the requested data on-the-fly, the server can instead direct the client to an endpoint providing that function, such as bamsvr above.

Slicing by htsnexus is imprecise, in that the result may contain some records outside of the requested range. This is a salient shortcoming, but part of an informed tradeoff, and rather easy to compensate for on the client.
