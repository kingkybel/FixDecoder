FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PORT=8081

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 \
    python3-pip \
    g++ \
    make \
    cmake \
    libtinyxml2-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY tools/fix-web-ui/requirements.txt /app/requirements.txt
RUN pip3 install --break-system-packages --no-cache-dir -r /app/requirements.txt

COPY tools/fix-web-ui/app.py /app/app.py
COPY tools/fix-web-ui/templates /app/templates
COPY tools/fix-web-ui/static /app/static
COPY tools/fix-web-ui/fix_web_parser.cc /app/fix_web_parser.cc

# Copy dictionary and generated metadata from repository context.
COPY include /app/include
COPY src /app/src
COPY data/generated_src /app/data/generated_src
COPY data/quickfix /app/data/quickfix

RUN mkdir -p /app/bin && \
    g++ -std=c++23 -O2 \
      -I/app/include -I/app/data/generated_src \
      /app/fix_web_parser.cc /app/src/fix_dictionary.cc /app/src/fix_decoder.cc \
      -ltinyxml2 \
      -o /app/bin/fix_web_parser

EXPOSE 8081

CMD ["python3", "/app/app.py"]
