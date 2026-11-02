FROM ubuntu:24.04@sha256:78d4744ead1f5b19b04d0df558ab182a5300903900755e515b2ca4d10eb2044d

ENV DEBIAN_FRONTEND=noninteractive PYTHONDONTWRITEBYTECODE=1
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential gcc-multilib g++-multilib meson ninja-build cmake \
    pkg-config python3 git nasm curl ca-certificates xz-utils tar patch p7zip-full \
    patchelf util-linux

COPY chimera-build/driver.sh /usr/local/lib/chimera-build/driver.sh
ENTRYPOINT ["sh", "/usr/local/lib/chimera-build/driver.sh"]
