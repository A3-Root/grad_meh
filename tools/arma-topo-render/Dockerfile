FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        gdal-bin \
        imagemagick \
        inkscape \
        python3 \
        python3-gdal \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
