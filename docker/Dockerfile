# Aether build image: SaturnSDK GCC (built from source) + genisoimage + python3. No Jo Engine.
FROM --platform=linux/amd64 debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential wget ca-certificates texinfo bison flex libgmp-dev libmpfr-dev libmpc-dev \
        zlib1g-dev git python3 genisoimage xz-utils bzip2 patch file curl gnupg && \
    rm -rf /var/lib/apt/lists/*
ARG SDK_COMMIT=bff0b60
# Patch SaturnSDK scripts to preserve timestamps in extracted sources and keep -fcommon flag.
# extract-source.sh uses 'cp -rv' which does not preserve timestamps, causing make to regenerate autotools files.
# build-*.sh scripts override CFLAGS/CXXFLAGS with -s, wiping out the -fcommon flag needed for pre-C99 sources.
# Apply patches immediately after checkout, before build-elf.sh runs.
RUN git clone https://github.com/SaturnSDK/Saturn-SDK-GCC-SH2 /src/sdk && cd /src/sdk && git checkout ${SDK_COMMIT} && \
    sed -i 's/cp -rv /cp -av /' extract-source.sh && \
    sed -i 's/CFLAGS="-s"/CFLAGS="-s -fcommon"/; s/CXXFLAGS="-s"/CXXFLAGS="-s -fcommon"/' build-*.sh && \
    grep -c 'CFLAGS="-s -fcommon"' build-gcc-bootstrap.sh && \
    grep -c 'cp -av' extract-source.sh
ENV SRCDIR=/src/sdk/source BUILDDIR=/src/sdk/build TARGETMACH=sh-elf \
    BUILDMACH=x86_64-pc-linux-gnu HOSTMACH=x86_64-pc-linux-gnu \
    INSTALLDIR=/opt/saturn/toolchain SYSROOTDIR=/opt/saturn/toolchain/sysroot \
    ROOTDIR=/src/sdk DOWNLOADDIR=/src/sdk/download PROGRAM_PREFIX=saturn-sh2- \
    CFLAGS="-O2 -fcommon"
RUN cd /src/sdk && ./build-elf.sh && rm -rf /src/sdk/build /src/sdk/source /src/sdk/download
ENV PATH=/opt/saturn/toolchain/bin:$PATH
WORKDIR /work
