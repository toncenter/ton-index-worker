FROM ubuntu:24.04 as builder
RUN DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get update && apt-get -y install tzdata && rm -rf /var/lib/{apt,dpkg,cache,log}/
RUN apt update -y \
    && apt install -y build-essential cmake clang-20 openssl libssl-dev zlib1g-dev \
                   gperf wget git curl ccache libmicrohttpd-dev liblz4-dev \
                   pkg-config libsecp256k1-dev libsodium-dev python3-dev libpq-dev \
                   autoconf libtool libhiredis-dev lsb-release software-properties-common gnupg ninja-build \
    && rm -rf /var/lib/{apt,dpkg,cache,log}/

ENV CC=clang-20
ENV CXX=clang++-20
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

FROM ubuntu:24.04
RUN DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt-get update && apt-get -y install tzdata && rm -rf /var/lib/{apt,dpkg,cache,log}/
RUN apt update -y \
    && apt install -y dnsutils libpq-dev libsecp256k1-dev libsodium-dev libatomic1 postgresql-client \
    && rm -rf /var/lib/{apt,dpkg,cache,log}/

COPY scripts/entrypoint.sh /entrypoint.sh
COPY --from=builder /app/build/external/libpqxx/src/libpqxx.a /usr/lib/libpqxx.a
COPY --from=builder /app/build/external/libpqxx/src/libpqxx-*.a /usr/lib/
COPY --from=builder /app/build/ton-index-postgres-v2/ton-index-postgres-v2 /usr/bin/ton-index-postgres-v2
COPY --from=builder /app/build/ton-smc-scanner/ton-smc-scanner /usr/bin/ton-smc-scanner
COPY --from=builder /app/build/ton-integrity-checker/ton-integrity-checker /usr/bin/ton-integrity-checker
COPY --from=builder /app/build/ton-trace-emulator/ton-trace-emulator /usr/bin/ton-trace-emulator

ENTRYPOINT [ "/entrypoint.sh" ]
