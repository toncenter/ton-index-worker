#!/bin/bash
set -e

# postgres password
if [ ! -z "$POSTGRES_PASSWORD_FILE" ]; then
    echo "Postgres password file: ${POSTGRES_PASSWORD_FILE}"
    if [ ! -f "${POSTGRES_PASSWORD_FILE}" ]; then
        echo "Password file specified, but not found"
        exit 1
    fi
    POSTGRES_PASSWORD=$(cat ${POSTGRES_PASSWORD_FILE})
elif [ ! -z "$POSTGRES_PASSWORD" ]; then
    echo "Postgres password specified"
else
    echo "Postgres password file not specified!"
    exit 1
fi

POSTGRES_HOST_IP=$(dig +short ${POSTGRES_HOST})
if [[ -z "$POSTGRES_HOST_IP" ]]; then
    POSTGRES_HOST_IP=$POSTGRES_HOST
    echo "PostgreSQL host IP: $POSTGRES_HOST_IP"
fi
echo "Postgres host: $POSTGRES_HOST (ip: $POSTGRES_HOST_IP)"

ulimit -n 1000000
printenv
echo "Running binary ${TON_WORKER_BINARY:-ton-index-postgres}"
TON_WORKER_FROM=$(PGPASSWORD=$POSTGRES_PASSWORD psql -t -h $POSTGRES_HOST -p $POSTGRES_PORT -U $POSTGRES_USER -d $POSTGRES_DBNAME -c "select seqno - 1000 from blocks where workchain =-1 and shard = -9223372036854775808 order by  seqno desc limit 1;")
echo "TON_WORKER_FROM: $TON_WORKER_FROM"

${TON_WORKER_BINARY:-ton-index-postgres} --host $POSTGRES_HOST_IP \
    --port $POSTGRES_PORT \
    --user $POSTGRES_USER \
    --password $POSTGRES_PASSWORD \
    --dbname $POSTGRES_DBNAME \
    --from $TON_WORKER_FROM \
    --db ${TON_WORKER_DBROOT:-/tondb} $@
