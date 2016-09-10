# Dockerfile for the ga4gh-streaming-freebayes container
FROM ubuntu:trusty
RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends --no-install-suggests python-requests curl parallel aria2 pigz dstat
COPY freebayes bamtools vcffirstheader /usr/local/bin/
COPY libbamtools.so.2.3.0 /usr/local/lib/
RUN ldconfig
ADD https://raw.githubusercontent.com/dnanexus-rnd/htsnexus/master/client/htsnexus.py /usr/local/bin/htsnexus
RUN chmod +x /usr/local/bin/htsnexus
RUN mkdir /app
WORKDIR /app/
COPY main.sh subjob.sh hs37d5_interLCR_intervals.4Mbp.regions /app/
ENV SHELL /bin/bash
ENTRYPOINT ["/app/main.sh"]
