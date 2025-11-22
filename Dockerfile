# # Dockerfile — use Ubuntu 24.04 so glibc/GCC are recent enough for libstdc++ symbols
# FROM ubuntu:24.04

# # noninteractive so apt won't prompt
# ENV DEBIAN_FRONTEND=noninteractive

# RUN apt-get update \
#  && apt-get install -y --no-install-recommends \
#     build-essential \
#     g++ \
#     make \
#     ca-certificates \
#     wget \
#  && rm -rf /var/lib/apt/lists/*

# WORKDIR /app

# # copy project
# COPY . .

# # build the server inside the container (uses Makefile)
# RUN make

# EXPOSE 8080

# # use Railway provided $PORT, or default 8080 locally
# CMD ["sh", "-c", "./hp-file-server ${PORT:-8080} ./storage 8"]
# Use stock gcc toolchain so build happens inside the container (no host/binary mismatch)
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    g++ \
    git \
    ca-certificates \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# copy everything
COPY . .

# remove any prebuilt binary if present (safety)
RUN rm -f hp-file-server || true

# build
RUN make clean && make

# expose the port Railway expects (we'll allow override via PORT env)
ENV PORT=8080
EXPOSE 8080

# Create a storage folder and ensure index exists (Railway uses container filesystem)
RUN mkdir -p storage

# Start server: allow overriding args via CMD/ENV on Railway. Arguments: <port> <root> <threads>
CMD ["sh", "-c", "./hp-file-server ${PORT} ./storage 8"]
