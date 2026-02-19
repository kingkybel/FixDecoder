FROM ubuntu:24.04

ARG PREFERRED_GXX_VERSION=14
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    cmake \
    git \
    make \
    && selected_ver="${PREFERRED_GXX_VERSION}" \
    && preferred_candidate="$(apt-cache policy "g++-${selected_ver}" | awk '/Candidate:/ {print $2}')" \
    && if [ -z "${preferred_candidate}" ] || [ "${preferred_candidate}" = "(none)" ]; then \
         selected_pkg="$(apt-cache pkgnames | awk '/^g\\+\\+-[0-9]+$/ {print $0}' | sort -V | tail -1)"; \
         if [ -z "${selected_pkg}" ]; then echo "No versioned g++ package found" >&2; exit 1; fi; \
         selected_ver="${selected_pkg#g++-}"; \
       fi \
    && apt-get install -y --no-install-recommends "gcc-${selected_ver}" "g++-${selected_ver}" \
    && update-alternatives --install /usr/bin/gcc gcc "/usr/bin/gcc-${selected_ver}" 100 \
    && update-alternatives --install /usr/bin/g++ g++ "/usr/bin/g++-${selected_ver}" 100 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
