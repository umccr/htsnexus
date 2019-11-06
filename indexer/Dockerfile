FROM ubuntu:xenial as builder
ADD . /app
RUN apt-get update && apt-get install -y cmake automake autoconf zlib1g-dev libbz2-dev liblzma-dev libcurl4-openssl-dev libvsqlitepp-dev libsqlite3-dev libgnutls-dev libsqlite3-dev sqlite3 libncurses5-dev libssl-dev
RUN cd /app && \
    cmake . && \
    make

FROM ubuntu:xenial 
RUN apt-get update && apt-get install -y libcurl4-openssl-dev libsqlite3-dev
WORKDIR /app
COPY --from=builder /app/htsnexus_index_bam .
CMD ["/app/htsnexus_index_bam"]