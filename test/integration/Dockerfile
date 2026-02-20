FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . /workspace
RUN rm -rf build && \
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --target fix_controller_demo --parallel 3

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=build /workspace/build/Release/bin/fix_controller_demo /usr/local/bin/fix_controller_demo

ENV FIX_ROLE=exchange
ENV FIX_HOST=fix-exchange-1
ENV FIX_HOSTS=fix-exchange-1
ENV FIX_PORT=5001
ENV FIX_PORTS=5001
ENV FIX_SCENARIO=handshake
ENV FIX_CONVERSATION_MESSAGES=100
ENV FIX_PERF_PAYLOAD_SIZE=512
ENV FIX_RUNTIME_SECONDS=30

ENTRYPOINT ["/usr/local/bin/fix_controller_demo"]
