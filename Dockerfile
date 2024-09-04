FROM ubuntu as builder
WORKDIR /app
COPY . .
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y gcc make libjpeg-dev libpng-dev libwebp-dev libcurl4-gnutls-dev libncurses5-dev libexif-dev autoconf-archive pkg-config
RUN ./autogen.sh && ./configure && make

FROM ubuntu
WORKDIR /app
RUN apt-get update && apt-get install -y libc6 libjpeg8 libpng16-16 libwebp7 libcurl3-gnutls libtinfo6
COPY --from=builder /app/src/jp2a /usr/bin/jp2a
ENTRYPOINT ["jp2a"]
