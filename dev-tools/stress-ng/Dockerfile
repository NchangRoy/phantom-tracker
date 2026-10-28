# SPDX-License-Identifier: GPL-3.0 with GCC-exception-3.1
#
# Contributors:
#   Human: Himadri Chhaya-Shailesh
#   AI: ChatGPT-6.1
#
# Host build environment for the Debian 12 amd64 noise guests.
ARG BASE_IMAGE=debian:12-slim
FROM ${BASE_IMAGE}

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential binutils ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*

# Pin both the release and its content; a changed tag/archive must fail.
RUN curl -fL --retry 3 \
        https://codeload.github.com/ColinIanKing/stress-ng/tar.gz/refs/tags/V0.19.02 \
        -o /tmp/stress-ng.tar.gz \
    && echo '15a07030a14bbfd5991e9ba3fbfbc24ed3ae72a2c946b033c06b820bc16f41c4  /tmp/stress-ng.tar.gz' | sha256sum -c - \
    && mkdir -p /usr/src/stress-ng \
    && tar -xzf /tmp/stress-ng.tar.gz --strip-components=1 -C /usr/src/stress-ng \
    && sha256sum /tmp/stress-ng.tar.gz > /usr/src/source-sha256.txt \
    && rm /tmp/stress-ng.tar.gz

COPY build.sh /usr/local/bin/build-stress-ng
ENV LC_ALL=C TZ=UTC
WORKDIR /build
ENTRYPOINT ["bash", "/usr/local/bin/build-stress-ng"]
