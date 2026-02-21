# FIX Web UI Tool

[Back to main README](../../README.md)

A Dockerized webserver that lets you paste a FIX message and inspect parsed output.

## Features

- Configurable listen port (default `8081` via `PORT` env var)
- Decodes message fields using project dictionaries
- Shows parsed fields similar to `example_usage` (`tag`, `name`, `type`, raw value, typed value)
- For malformed messages, shows fields parsed up to first error and explains the error

## Build

From repository root:

```bash
docker build -t fix-web-ui -f tools/fix-web-ui/Dockerfile .
```

## Run

Default port (`8081`):

```bash
docker run --rm -p 8081:8081 fix-web-ui
```

Custom port (`8090`):

```bash
docker run --rm -e PORT=8090 -p 8090:8090 fix-web-ui
```

Then open:

- `http://localhost:8081` (or your configured port)

![Invalid FIX message](images/FIX_message_parser.png)
