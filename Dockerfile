FROM ubuntu AS builder
WORKDIR /app
COPY . .
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y gcc make autoconf libjpeg-dev libpng-dev libwebp-dev libcurl4-gnutls-dev libncurses5-dev libexif-dev pkg-config
RUN ./autogen.sh && ./configure && make

FROM ubuntu
WORKDIR /app
RUN apt-get update && apt-get install -y libc6 libjpeg8 libpng16-16t64 libwebp7 libcurl3t64-gnutls libtinfo6
COPY --from=builder /app/src/jp2a /usr/bin/jp2a
ENTRYPOINT ["jp2a"]
