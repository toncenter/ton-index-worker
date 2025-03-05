FROM ubuntu:22.04 as builder
RUN DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get update && apt-get -y install tzdata && rm -rf /var/lib/{apt,dpkg,cache,log}/
RUN apt update -y \
    && apt install -y build-essential cmake clang openssl libssl-dev zlib1g-dev \
                   gperf wget git curl ccache libmicrohttpd-dev liblz4-dev \
                   pkg-config libsecp256k1-dev libsodium-dev python3-dev libpq-dev \
                   autoconf libtool  lsb-release software-properties-common gnupg ninja-build \
    && rm -rf /var/lib/{apt,dpkg,cache,log}/

RUN wget https://apt.llvm.org/llvm.sh && \
    chmod +x llvm.sh && \
    ./llvm.sh 16 all && \
    rm -rf /var/lib/apt/lists/*

ENV CC=/usr/bin/clang-16
ENV CXX=/usr/bin/clang++-16
ENV CCACHE_DISABLE=1

# building
COPY external/ /app/external/
COPY pgton/ /app/pgton/
COPY ton-index-clickhouse/ /app/ton-index-clickhouse/
COPY ton-index-postgres/ /app/ton-index-postgres/
COPY ton-index-postgres-v2/ /app/ton-index-postgres-v2/
COPY ton-integrity-checker/ /app/ton-integrity-checker/
COPY ton-smc-scanner/ /app/ton-smc-scanner/
COPY ton-trace-emulator/ /app/ton-trace-emulator/
COPY tondb-scanner/ /app/tondb-scanner/
COPY sandbox-cpp/ /app/sandbox-cpp/
COPY CMakeLists.txt /app/

WORKDIR /app/build
RUN cmake -GNinja -DCMAKE_BUILD_TYPE=Release -DSKIP_TESTS=On ..
RUN ninja -j$(nproc)

FROM ubuntu:22.04
RUN DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get update && apt-get -y install tzdata && rm -rf /var/lib/{apt,dpkg,cache,log}/
RUN apt update -y \
    && apt install -y dnsutils libpq-dev libsecp256k1-dev libsodium-dev libatomic1 \
    && rm -rf /var/lib/{apt,dpkg,cache,log}/

COPY scripts/entrypoint.sh /entrypoint.sh
COPY --from=builder /app/build/external/libpqxx/src/libpqxx.so /usr/lib/libpqxx.so
COPY --from=builder /app/build/external/libpqxx/src/libpqxx-*.so /usr/lib/
COPY --from=builder /app/build/ton-index-postgres/ton-index-postgres /usr/bin/ton-index-postgres
COPY --from=builder /app/build/ton-index-postgres-v2/ton-index-postgres-v2 /usr/bin/ton-index-postgres-v2
COPY --from=builder /app/build/ton-index-clickhouse/ton-index-clickhouse /usr/bin/ton-index-clickhouse
COPY --from=builder /app/build/ton-smc-scanner/ton-smc-scanner /usr/bin/ton-smc-scanner
COPY --from=builder /app/build/ton-integrity-checker/ton-integrity-checker /usr/bin/ton-integrity-checker
COPY --from=builder /app/build/ton-trace-emulator/ton-trace-emulator /usr/bin/ton-trace-emulator

ENTRYPOINT [ "/entrypoint.sh" ]
