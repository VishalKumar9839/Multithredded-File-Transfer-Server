# Dockerfile — use Ubuntu 24.04 so glibc/GCC are recent enough for libstdc++ symbols
FROM ubuntu:24.04

# noninteractive so apt won't prompt
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    build-essential \
    g++ \
    make \
    ca-certificates \
    wget \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# copy project
COPY . .

# build the server inside the container (uses Makefile)
RUN make

EXPOSE 8080

# use Railway provided $PORT, or default 8080 locally
CMD ["sh", "-c", "./hp-file-server ${PORT:-8080} ./storage 8"]
