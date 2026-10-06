FROM ubuntu:22.04
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates git build-essential pkg-config python3 curl wget xz-utils file && rm -rf /var/lib/apt/lists/*
ARG SDK_REV=62b4d098d1472c3534dd098ca2a0e0e10712f1c6
RUN git clone https://github.com/ValveSoftware/steamlink-sdk.git /opt/steamlink-sdk && git -C /opt/steamlink-sdk checkout "$SDK_REV"
ENV STEAMLINK_SDK_PATH=/opt/steamlink-sdk
COPY scripts/autocompile /usr/local/bin/autocompile
RUN chmod 755 /usr/local/bin/autocompile
WORKDIR /src
CMD ["bash", "scripts/build-steamlink.sh"]
