FROM ubuntu:trusty
RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends --no-install-suggests python-requests curl parallel
COPY freebayes vcffirstheader /usr/local/bin/
ADD https://raw.githubusercontent.com/dnanexus-rnd/htsnexus/master/client/htsnexus.py /usr/local/bin/htsnexus
RUN chmod +x /usr/local/bin/htsnexus
RUN mkdir /app
WORKDIR /app/
COPY main.sh /app/
ENTRYPOINT ["/app/main.sh"]
