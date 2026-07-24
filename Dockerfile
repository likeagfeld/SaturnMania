# Jo Engine build environment for Sonic Mania (Saturn).
#
# The Jo Engine repo bundles a prebuilt x86-64 glibc sh-none-elf GCC 8.2.0
# toolchain (Compiler/LINUX/bin). This image supplies the Linux runtime that
# toolchain needs (gcc's mpfr/gmp/mpc + zlib), plus make and mkisofs for the
# ISO step. The repo itself is bind-mounted at run time, not copied in.
# Pinned to linux/amd64: the bundled sh-none-elf GCC binaries are x86-64
# glibc executables, so the image must be amd64 even on arm64 hosts (Apple
# Silicon runs it via emulation). No-op on Windows/x86-64 Docker Desktop.
FROM --platform=linux/amd64 debian:bookworm-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        make \
        genisoimage \
        libmpfr6 \
        libgmp10 \
        libmpc3 \
        zlib1g \
        bash && \
    ln -sf /usr/bin/genisoimage /usr/bin/mkisofs && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /work
