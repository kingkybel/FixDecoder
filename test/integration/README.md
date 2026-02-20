# FIX Controller Container Tests

This directory contains Docker-based integration tests for the session-level FIX controller.
Unit tests live separately under `test/unit/`.

Supported base-image families for builder/runtime are Ubuntu/Debian, Fedora, and Alpine.

## Files

- `compose.yml`: starts two `exchange` containers and one `client` container on one bridge network.
- `Dockerfile.builder`: toolchain image used by one-shot build container.
- `Dockerfile.runtime`: lightweight runtime image used by exchange/client containers.
- `Dockerfile.profiler`: Alpine profiler UI image with `perf` + FlameGraph tooling and local HTTP server.
- `container-scripts/*.sh`: container-side helper scripts used by `profile_hotpaths.sh` (build, test, coverage, ownership).
- `run_all_scenarios.sh`: runs all supported docker scenarios sequentially and fails fast on errors.
- `profile_hotpaths.sh`: builds an instrumented binary, runs timed client/exchange traffic, and produces hotspot + heatmap reports.

## Build Once, Run Many

The docker setup now separates:

- `fix-builder` container: compiles `fix_controller_demo` once into named volume `fix-controller-bin`.
- runtime containers (`fix-exchange-*`, `fix-client-*`): only run `/app/fix_controller_demo` from that volume.

One-time build:

```bash
docker compose -f test/integration/compose.yml --profile build-bin up --build --exit-code-from fix-builder fix-builder
```

Build for a specific OS/compiler tuple (image names include tuple):

```bash
FIX_BASE_IMAGE=ubuntu:24.04 FIX_COMPILER_FAMILY=g++ FIX_COMPILER_VERSION= \
FIX_BUILDER_IMAGE=fix-controller-builder:ubuntu-24.04-gxx-latest \
FIX_RUNTIME_IMAGE=fix-controller-runtime:ubuntu-24.04-gxx-latest \
docker compose -f test/integration/compose.yml --profile build-bin up --build --exit-code-from fix-builder fix-builder
```

Run scenarios after that without rebuilding:

```bash
docker compose -f test/integration/compose.yml --profile single-client up --abort-on-container-failure --exit-code-from fix-client-1
```

## Run Full Sequential Regression

From repository root:

```bash
./test/integration/run_all_scenarios.sh
```

Only build container images + binary artifact (no scenario execution):

```bash
./test/integration/run_all_scenarios.sh --build-only
```

Logging:

- A per-run log file is written to `test/integration/logs/`.
- Each test combination includes timestamp, profile, selected env/version, and `PASS`/`FAIL` outcome.
- Override output path with `LOG_FILE`, for example:

```bash
LOG_FILE=/tmp/fix_scenarios.log ./test/integration/run_all_scenarios.sh
```

Optional overrides for the performance step:

```bash
FIX_CONVERSATION_MESSAGES=300 FIX_PERF_PAYLOAD_SIZE=2048 FIX_RUNTIME_SECONDS=60 ./test/integration/run_all_scenarios.sh
```

## Profile Critical Paths

Run a timed, instrumented exchange/client performance flow and generate reports:

```bash
./test/integration/profile_hotpaths.sh --duration 60
```

The profiling script uses the same core selector flags as `run_all_scenarios.sh` where applicable:

```bash
./test/integration/profile_hotpaths.sh -o ubuntu:24.04 -c clang -v 18 -f 11 Fix-40 -d 1m
```

Artifacts are written under `test/integration/profiles/hotpaths_<timestamp>/`:

- `hotpaths_report.txt`: function/file coverage summary (quick hotspot indicator)
- `heatmap.txt`: line execution counts
- `heatmap_html/index.html`: browsable line heatmap
- `merged.profdata`, `coverage_export.json`, and raw `*.profraw` files

The script defaults to `clang` because heatmaps are generated with `llvm-profdata` + `llvm-cov`.
During profiling runs, realistic sample payloads are looped continuously until the requested duration is reached.
After the duration expires, the script stops the containers, finalizes profiling reports, and prints commands to open heatmaps and launch the local profiler UI container.
By default it also records `perf.data` and generates `flame.svg` per profiled FIX version (best effort).

Profiling output ownership is normalized back to your user (`FIX_HOST_UID` / `FIX_HOST_GID`, default current UID/GID) so generated files do not require `sudo chown`.

Start the Alpine profiler UI container:

```bash
FIX_UID=$(id -u) FIX_GID=$(id -g) FIX_PROFILE_UI_PORT=8080 \
docker compose -f test/integration/compose.yml --profile profiler-ui up --build -d fix-profiler-ui
```

Open browser index:

```bash
xdg-open "http://localhost:8080/"
```

This serves `test/integration/profiles/` and opens a single dashboard website at `http://localhost:8080/`.
The dashboard has two tabs:
- Unit-test coverage
- Integration coverage + heatmap (and flamegraph link)

It also auto-generates `flame.svg` next to any discovered `perf.data`.
`profile_hotpaths.sh` updates `test/integration/profiles/latest` after each run, so `http://localhost:8080/` always redirects to the newest dashboard.
To force regeneration:

```bash
FIX_UID=$(id -u) FIX_GID=$(id -g) FIX_FORCE_FLAMEGRAPH_REBUILD=1 \
docker compose -f test/integration/compose.yml --profile profiler-ui up --build -d fix-profiler-ui
```

Generate one flamegraph manually inside the profiler container:

```bash
docker compose -f test/integration/compose.yml exec fix-profiler-ui \
  profiler-ui flamegraph hotpaths_<timestamp>/FIX_4_4/perf.data
```

Record a perf profile manually inside the profiler container:

```bash
docker compose -f test/integration/compose.yml exec fix-profiler-ui \
  profiler-ui record <target_pid> 30 99 hotpaths_<timestamp>/FIX_4_4/perf.data
```

Disable perf/flamegraph during hotpath profiling:

```bash
./test/integration/profile_hotpaths.sh --no-perf
```

Tune perf sample rate (Hz):

```bash
./test/integration/profile_hotpaths.sh --perf-hz 199
```

Realistic message payload seeds are used automatically for `conversation` and `performance` from:

- `/workspace/data/samples/realistic` (inside containers, default)
- override with `FIX_REALISTIC_MESSAGES_DIR`
- override with a single file via `FIX_MESSAGE_FILE`

Filter versions with `--fix-versions` / `-f` (case-insensitive, supports shorthand):

```bash
./test/integration/run_all_scenarios.sh -f 11 Fix-40
./test/integration/run_all_scenarios.sh --fix-versions fixt11 4.4
```

Select distro/compiler for the full scenario run:

```bash
./test/integration/run_all_scenarios.sh -o ubuntu:24.04 -c g++
./test/integration/run_all_scenarios.sh -o ubuntu:24.04 -c gcc -v 14
./test/integration/run_all_scenarios.sh -o fedora:41 -c clang -v 18
./test/integration/run_all_scenarios.sh -o alpine:latest -c g++
```


The script maps this tuple to image names:

- `fix-controller-builder:<os>-<tag>-<compiler>-<version>`
- `fix-controller-runtime:<os>-<tag>-<compiler>-<version>`
- Docker tags cannot contain `+`, so `g++` is encoded as `gxx` in image tags.
- When `--compiler-version` is omitted, the tag uses `latest` and the build uses the newest available compiler package in the selected base image.

### Size comparison

| IMAGE                                      | ID           | DISK USAGE | CONTENT SIZE | EXTRA | SMALLEST | BIGGEST |
| ------------------------------------------ | ------------ | ---------- | ------------ | ----- | -------- | ------- |
| fix-controller-builder:alpine-clang-latest | 889600d53af3 |      925MB |        244MB |       |          |         |
| fix-controller-builder:alpine-gxx-latest   | e4a18cb4aca0 |      517MB |        134MB |       |    X     |         |
| fix-controller-builder:fedora-clang-latest | f9d27a89fe8d |     1.47GB |        361MB |       |          |    X    |
| fix-controller-builder:fedora-gxx-latest   | 94c6738dcd48 |      881MB |        220MB |  U    |          |         |
| fix-controller-builder:ubuntu-clang-latest | c7e3e30ed827 |      839MB |        200MB |       |          |         |
| fix-controller-builder:ubuntu-gxx-latest   | 81158ad2bd5b |      702MB |        180MB |       |          |         |
| fix-controller-runtime:alpine-clang-latest | 9ec594e2daad |     14.8MB |       4.16MB |       |    X     |         |
| fix-controller-runtime:alpine-gxx-latest   | e11b41fbf8bb |     14.8MB |       4.16MB |       |    X     |         |
| fix-controller-runtime:fedora-clang-latest | eccbed97e73a |      269MB |       67.5MB |       |          |    X    |
| fix-controller-runtime:fedora-gxx-latest   | c6b1caa8c9a2 |      269MB |       67.5MB |       |          |    X    |
| fix-controller-runtime:ubuntu-clang-latest | bd5c1bd2333b |      122MB |       30.8MB |       |          |         |
| fix-controller-runtime:ubuntu-gxx-latest   | 06908dbd621c |      122MB |       30.8MB |       |          |         |

Version coverage in `run_all_scenarios.sh`:

- Single-client scenarios (`handshake`, `out_of_sync`, `garbled`, `conversation`, `performance`) run for:
  - `FIX.4.0`, `FIX.4.1`, `FIX.4.2`, `FIX.4.3`, `FIX.4.4`, `FIXT.1.1`
- Multi-party scenarios (`multi-exchange`, `multi-client`, `multi-mesh`) run for all versions above.
- Additional curated mixed-version selections are included for multi-party runs (not full cartesian combinations).

## Run Scenarios

From repository root:

```bash
docker compose -f test/integration/compose.yml --profile single-client up --build --abort-on-container-failure --exit-code-from fix-client-1
```

Run out-of-sync sequence test:

```bash
FIX_SCENARIO=out_of_sync \
docker compose -f test/integration/compose.yml --profile single-client up --build --abort-on-container-failure --exit-code-from fix-client-1
```

Run garbled-message test:

```bash
FIX_SCENARIO=garbled \
docker compose -f test/integration/compose.yml --profile single-client up --build --abort-on-container-failure --exit-code-from fix-client-1
```

Run longer conversation (100 request/reply pairs by default):

```bash
FIX_SCENARIO=conversation \
docker compose -f test/integration/compose.yml --profile single-client up --build --abort-on-container-failure --exit-code-from fix-client-1
```

Use a specific realistic message file as payload seed source:

```bash
FIX_SCENARIO=conversation FIX_MESSAGE_FILE=/workspace/data/samples/realistic/FIX44_realistic_200.messages \
docker compose -f test/integration/compose.yml --profile single-client up --build --abort-on-container-failure --exit-code-from fix-client-1
```

Run high-payload performance conversation:

```bash
FIX_SCENARIO=performance FIX_CONVERSATION_MESSAGES=300 FIX_PERF_PAYLOAD_SIZE=2048 FIX_RUNTIME_SECONDS=60 \
docker compose -f test/integration/compose.yml --profile single-client up --build --abort-on-container-failure --exit-code-from fix-client-1
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
docker compose -f test/integration/compose.yml --profile multi-exchange up --build --abort-on-container-failure --exit-code-from fix-client-multi
```

- Enable a second client service:

```bash
docker compose -f test/integration/compose.yml --profile multi-client up --build --abort-on-container-failure --exit-code-from fix-client-2
```

- Run multiple clients, each talking to multiple exchanges:

```bash
docker compose -f test/integration/compose.yml --profile multi-mesh up --build --abort-on-container-failure --exit-code-from fix-client-mesh-1
```

`multi-mesh` defaults to `FIX_SCENARIO=conversation` so clients continue after logon.
Override to performance mode, for example:

```bash
FIX_SCENARIO=performance FIX_CONVERSATION_MESSAGES=300 FIX_PERF_PAYLOAD_SIZE=2048 FIX_RUNTIME_SECONDS=60 \
docker compose -f test/integration/compose.yml --profile multi-mesh up --build --abort-on-container-failure --exit-code-from fix-client-mesh-1
```
