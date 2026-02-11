# FIX Controller Container Tests

This directory contains Docker-based integration tests for the session-level FIX controller.

## Files

- `compose.yml`: starts two containers (`fix-acceptor`, `fix-initiator`) on one bridge network.
- `Dockerfile.acceptor`: image definition for acceptor peer.
- `Dockerfile.initiator`: image definition for initiator peer.

## Run Scenarios

From repository root:

```bash
docker compose -f docker/fix-controller/compose.yml up --build --abort-on-container-exit
```

Run out-of-sync sequence test:

```bash
FIX_SCENARIO=out_of_sync \
docker compose -f docker/fix-controller/compose.yml up --build --abort-on-container-exit
```

Run garbled-message test:

```bash
FIX_SCENARIO=garbled \
docker compose -f docker/fix-controller/compose.yml up --build --abort-on-container-exit
```

The initiator container drives the scenario; logs from both containers show inbound/outbound FIX frames and controller events.
