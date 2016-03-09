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

*Download an ENCODE ChIP-Seq BAM*
```bash
./htsnexus ENCODE ENCFF904PIO > ENCFF904PIO.bam
```

*Slice a genomic range out of a Platinum Genomes BAM*

```bash
./htsnexus -r chr12:111766922-111817529 platinum NA12878 > NA12878_ALDH2.bam
```

*Count reads on chr21 in a 1000 Genomes BAM*

```bash
./htsnexus -r 21 1000genomes_low_coverage NA20276 | samtools view -c -
```

*id. with CRAM (samtools 1.2+ needed)*

```bash
./htsnexus -r 21 1000genomes_low_coverage HG01102 cram | samtools view -c -
```

*Stream reads from Heng Li's bamsvr and display as headered SAM*

```bash
./htsnexus -r 11:10899000-10900000 lh3bamsvr EXA00001 | samtools view -h - | less -S
```

At the moment only BAM and CRAM are supported as proof-of-concept; VCF/BCF are coming. The htsnexus client tool simply emits BAM/CRAM to standard output, which can be redirected to a file or piped into samtools. It delivers a well-formed BAM/CRAM file, with the proper header, even when slicing a genomic range. Here are the data accessions currently available:

| namespace | accession | format |
| --- | --- | --- |
| **platinum** <br/> Illumina Platinum Genomes stored at EBI | NA12877 NA12878 NA12879 NA12881 NA12882 NA12883 NA12884 NA12885 NA12886 NA12887 NA12888 NA12889 NA12890 NA12891 NA12892 NA12893 | bam |
| **ENCODE** <br/> ChIP-seq data released by the ENCODE DCC in Jan 2016 | ENCFF014ABI ENCFF024MPE ENCFF070QUN ENCFF090MZL ENCFF124VCI ENCFF137WND ENCFF180VYU ENCFF308BKD ENCFF373VCV ENCFF465GPJ ENCFF572JRO ENCFF630NYB ENCFF743FRI ENCFF800DAY ENCFF862PIC ENCFF866OLR ENCFF904PIO ENCFF929AIJ ENCFF946BKE ENCFF951SEJ | bam |
| **lh3bamsvr** <br/> Heng's examples | EXA00001 EXA00002 | bam |
| **1000genomes_low_coverage** <br/> Low-coverage whole-genome sequencing from the 1000 Genomes Project | <a href="http://ftp.1000genomes.ebi.ac.uk/vol1/ftp/phase3/data/">2,535 individual accessions</a> (example usage above) | bam, cram |

(We recognize that a directory API method is needed instead of this Markdown table...)

### How's it work?

You can get a feel for how htsnexus works by running it in verbose mode:

```
$ ./htsnexus -v 1000genomes_low_coverage NA20276 > /dev/null
Query URL: http://htsnexus.rnd.dnanex.us/1000genomes_low_coverage/NA20276/bam
Response: {
  "url": "https://s3.amazonaws.com/1000genomes/phase3/data/NA20276/alignment/NA20276.mapped.ILLUMINA.bwa.ASW.low_coverage.20120522.bam",
  "httpRequestHeaders": {
    "referer": "http://htsnexus.rnd.dnanex.us/1000genomes_low_coverage/NA20276/bam"
  }
}
Piping: ['curl', '-LSs', '-H', 'referer: http://htsnexus.rnd.dnanex.us/1000genomes_low_coverage/NA20276/bam', 'https://s3.amazonaws.com/1000genomes/phase3/data/NA20276/alignment/NA20276.mapped.ILLUMINA.bwa.ASW.low_coverage.20120522.bam']
Success
```

The htsnexus client makes the request to an API server. The server's JSON response gives the client another URL at which it can access the desired data, in this case a BAM file within the AWS mirror of 1000 Genomes. Then, the client delegates to curl to download that file.

How about when we slice a genomic range? This is slightly more complicated.

```
$ ./htsnexus -v -r chr12:111766922-111817529 platinum NA12878 | wc -c
Query URL: http://htsnexus.rnd.dnanex.us/platinum/NA12878/bam?range=chr12%3A111766922-111817529
Response: {
  "url": "http://ftp.era.ebi.ac.uk/vol1/ERA172/ERA172924/bam/NA12878_S1.bam",
  "httpRequestHeaders": {
    "referer": "http://htsnexus.rnd.dnanex.us/platinum/NA12878/bam?range=chr12%3A111766922-111817529"
  },
  "reference": "hg19",
  "prefix": "[704 base64 characters]",
  "byteRange": {
    "start": 81272945657,
    "end": 81275405961
  },
  "suffix": "[40 base64 characters]"
}
Piping: ['curl', '-LSs', '-H', 'referer: http://htsnexus.rnd.dnanex.us/platinum/NA12878/bam?range=chr12%3A111766922-111817529', '-H', 'range: bytes=81272945657-81275405960', 'http://ftp.era.ebi.ac.uk/vol1/ERA172/ERA172924/bam/NA12878_S1.bam']
Success
2460858
```

The server tells the client to access the Platinum Genomes BAM file from EBI, but furthermore to access a specific *byte* range containing the desired genomic range. Thus the server handles the genomic range lookup, so the client doesn't need index files. This will benefit genome browsers, which today sometimes have to fetch a ~100 MiB BAI file in order to take a far smaller BAM slice for visualization. (The server also gives the client the BAM header in the 'prefix', making it a bit easier to deliver a well-formed BAM.) CRAM slicing works in just the same way, differing only in the server-side index construction method, which the client is abstracted from.

Here's a diagram illustrating this core mechanic:

![](https://raw.githubusercontent.com/wiki/dnanexus-rnd/htsnexus/htsnexus_core_mechanic.png)

The htsnexus server maintains metadata and genomic range indices, but doesn't necessarily have to store or transport the data files themselves. This means it can be operated quite frugally, with heavy lifting offloaded to general-purpose services like S3. Architecturally, one htsnexus server could probably support thousands of clients engaged in a large scale analysis (not saying the current implementation is there). At the same time, should the need arise to generate the requested data on-the-fly, the server can instead direct the client to an endpoint providing that function, such as bamsvr above.

Slicing by htsnexus is imprecise, in that the result may contain some records outside of the requested range. This is a salient shortcoming, but part of an informed tradeoff, and rather easy to compensate for on the client.
