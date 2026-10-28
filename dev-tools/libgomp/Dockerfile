# SPDX-License-Identifier: GPL-3.0 with GCC-exception-3.1
#
# Contributors:
#   Human: Himadri Chhaya-Shailesh
#   AI: ChatGPT-6.1
#
# Build environment for the Debian 12 amd64 guests provisioned by create_vm.sh.
ARG BASE_IMAGE=debian:12-slim
FROM ${BASE_IMAGE}

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        gfortran \
        flex \
        bison \
        libgmp-dev \
        libmpfr-dev \
        libmpc-dev \
        systemtap-sdt-dev \
        texinfo \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
CMD ["bash"]
