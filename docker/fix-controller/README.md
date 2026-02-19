# FIX Controller Container Tests

This directory contains Docker-based integration tests for the session-level FIX controller.

## Files

- `compose.yml`: starts two `exchange` containers and one `client` container on one bridge network.
- `Dockerfile.builder`: toolchain image used by one-shot build container.
- `Dockerfile.runtime`: lightweight runtime image used by exchange/client containers.
- `run_all_scenarios.sh`: runs all supported docker scenarios sequentially and fails fast on errors.

## Build Once, Run Many

The docker setup now separates:

- `fix-builder` container: compiles `fix_controller_demo` once into named volume `fix-controller-bin`.
- runtime containers (`fix-exchange-*`, `fix-client-*`): only run `/app/fix_controller_demo` from that volume.

One-time build:

```bash
docker compose -f docker/fix-controller/compose.yml --profile build-bin up --build --exit-code-from fix-builder fix-builder
```

Run scenarios after that without rebuilding:

```bash
docker compose -f docker/fix-controller/compose.yml --profile single-client up --abort-on-container-failure --exit-code-from fix-client-1
```

## Run Full Sequential Regression

From repository root:

```bash
./docker/fix-controller/run_all_scenarios.sh
```

Logging:

- A per-run log file is written to `docker/fix-controller/logs/`.
- Each test combination includes timestamp, profile, selected env/version, and `PASS`/`FAIL` outcome.
- Override output path with `LOG_FILE`, for example:

```bash
LOG_FILE=/tmp/fix_scenarios.log ./docker/fix-controller/run_all_scenarios.sh
```

Optional overrides for the performance step:

```bash
FIX_CONVERSATION_MESSAGES=300 FIX_PERF_PAYLOAD_SIZE=2048 FIX_RUNTIME_SECONDS=60 ./docker/fix-controller/run_all_scenarios.sh
```

Filter versions with `--fix-versions` / `-f` (case-insensitive, supports shorthand):

```bash
./docker/fix-controller/run_all_scenarios.sh -f 11 Fix-40
./docker/fix-controller/run_all_scenarios.sh --fix-versions fixt11 4.4
```

Version coverage in `run_all_scenarios.sh`:

- Single-client scenarios (`handshake`, `out_of_sync`, `garbled`, `conversation`, `performance`) run for:
  - `FIX.4.0`, `FIX.4.1`, `FIX.4.2`, `FIX.4.3`, `FIX.4.4`, `FIXT.1.1`
- Multi-party scenarios (`multi-exchange`, `multi-client`, `multi-mesh`) run for all versions above.
- Additional curated mixed-version selections are included for multi-party runs (not full cartesian combinations).

## Run Scenarios

From repository root:

```bash
docker compose -f docker/fix-controller/compose.yml --profile single-client up --build --abort-on-container-failure --exit-code-from fix-client-1
```

Run out-of-sync sequence test:

```bash
FIX_SCENARIO=out_of_sync \
docker compose -f docker/fix-controller/compose.yml --profile single-client up --build --abort-on-container-failure --exit-code-from fix-client-1
```

Run garbled-message test:

```bash
FIX_SCENARIO=garbled \
docker compose -f docker/fix-controller/compose.yml --profile single-client up --build --abort-on-container-failure --exit-code-from fix-client-1
```

Run longer conversation (100 request/reply pairs by default):

```bash
FIX_SCENARIO=conversation \
docker compose -f docker/fix-controller/compose.yml --profile single-client up --build --abort-on-container-failure --exit-code-from fix-client-1
```

Run high-payload performance conversation:

```bash
FIX_SCENARIO=performance FIX_CONVERSATION_MESSAGES=300 FIX_PERF_PAYLOAD_SIZE=2048 FIX_RUNTIME_SECONDS=60 \
docker compose -f docker/fix-controller/compose.yml --profile single-client up --build --abort-on-container-failure --exit-code-from fix-client-1
```

The client container drives the scenario; logs from both containers show inbound/outbound FIX frames and controller
events.

## Roles and Scale

- Roles are now `exchange` and `client`.
- A client can talk to multiple exchanges using:
  - `FIX_HOSTS` (comma-separated host list)
  - `FIX_PORTS` (comma-separated port list, or one port reused for all hosts)
- Port mapping:
  - Inside Docker network, exchanges listen on container port `5001`.
  - Host-published ports are unique per exchange (`15001`, `15002` by default).
  - Override with `FIX_EXCHANGE_1_HOST_PORT` / `FIX_EXCHANGE_2_HOST_PORT`.
- Per-service protocol version override:
  - `FIX_BEGIN_STRING` (generic)
  - Service-specific variables in compose:
    - `FIX_EXCHANGE_1_BEGIN_STRING`
    - `FIX_EXCHANGE_2_BEGIN_STRING`
    - `FIX_CLIENT_1_BEGIN_STRING`
    - `FIX_CLIENT_MULTI_BEGIN_STRING`
    - `FIX_CLIENT_2_BEGIN_STRING`
    - `FIX_CLIENT_MESH_1_BEGIN_STRING`
    - `FIX_CLIENT_MESH_2_BEGIN_STRING`
- Run one client against multiple exchanges:

```bash
docker compose -f docker/fix-controller/compose.yml --profile multi-exchange up --build --abort-on-container-failure --exit-code-from fix-client-multi
```

- Enable a second client service:

```bash
docker compose -f docker/fix-controller/compose.yml --profile multi-client up --build --abort-on-container-failure --exit-code-from fix-client-2
```

- Run multiple clients, each talking to multiple exchanges:

```bash
docker compose -f docker/fix-controller/compose.yml --profile multi-mesh up --build --abort-on-container-failure --exit-code-from fix-client-mesh-1
```

`multi-mesh` defaults to `FIX_SCENARIO=conversation` so clients continue after logon.
Override to performance mode, for example:

```bash
FIX_SCENARIO=performance FIX_CONVERSATION_MESSAGES=300 FIX_PERF_PAYLOAD_SIZE=2048 FIX_RUNTIME_SECONDS=60 \
docker compose -f docker/fix-controller/compose.yml --profile multi-mesh up --build --abort-on-container-failure --exit-code-from fix-client-mesh-1
```
