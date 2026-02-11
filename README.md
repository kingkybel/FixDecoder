# FixDecoder

A C++ FIX decoding library and CLI tool.

`FixDecoder` loads QuickFIX XML dictionaries, parses raw FIX messages, and decodes fields into typed C++ values.
It can return:
- `fix::DecodedMessage` for field-by-field inspection (`tag`, `name`, `type`, typed value)
- `fix::DecodedObject` for enum/tag-indexed access (`msg[FieldTag::kX]`)
It supports:
- `FIX.4.0`
- `FIX.4.1`
- `FIX.4.2`
- `FIX.4.3`
- `FIX.4.4`
- `FIX.5.0` / `FIX.5.0SP1` / `FIX.5.0SP2` (transport `FIXT.1.1` with `1128` handling)
- `FIXT.1.1` session/admin messages

The project includes:
- `fixdecoder` static library
- `example_usage` CLI executable
- `fix_controller_demo` CLI executable for session-level FIX protocol exchanges
- GoogleTest-based test suite with version-specific sample messages

## How It Works

The decoder follows four steps:

1. Dictionary loading
- `fix::Dictionary` parses one QuickFIX XML file.
- `fix::DictionarySet` loads all XML dictionaries in a directory.

2. Message normalization and parsing
- `fix::Decoder` accepts SOH (`0x01`) and `|` separated FIX messages.
- It normalizes delimiters and splits the message into `(tag, value)` fields.

3. FIX version-first decoder selection
- The decoder reads tag `8` (`BeginString`) first.
- If tag `1128` (`ApplVerID`) is present, it maps that application version and uses it for decode-map selection.
- It then selects the generated per-version decoder map in `data/generated/*_decoder_map.h`.

4. Typed decode and object materialization
- Each tag is resolved through generated `decoderTagFor(tag)` and decoded to a typed C++ value (`bool`, `std::int64_t`, `float`, `double`, `std::string_view`).
- `decode(...)` returns `fix::DecodedMessage` (ordered field list plus dictionary metadata).
- `decodeObject(...)` returns `fix::DecodedObject` (map-like object for enum/tag lookup).

## Repository Layout

- `include/` public headers (`fix_decoder.h`, `fix_dictionary.h`, `fix_msgtype_key.h`)
- `src/` library sources and example executable (`examples.cc`)
- `scripts/` helper scripts to fetch dictionaries and sample messages
- `data/quickfix/` dictionary XML files
- `data/samples/valid/` valid sample messages per FIX version
- `test/` GoogleTest suite

## Build and Installation

Build and install were tested on Ubuntu 24.04.

### Dependencies

Required:
- CMake `>= 3.26`
- C++23 compiler (`g++` or `clang++`)

Default behavior:
- `tinyxml2` and `googletest` are fetched automatically with CMake `FetchContent`.

If you want to use system-installed dependencies instead:
- Configure with `-DFIXDECODER_FETCH_TINYXML2=OFF -DFIXDECODER_FETCH_GOOGLETEST=OFF`.

### Build

```bash
git clone https://github.com/kingkybel/FixDecoder.git
cd FixDecoder

# Only needed if your environment requires explicit submodule checkout.
git submodule update --init --recursive

cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel $(nproc)
```

### Install

```bash
# Change this to your preferred install location.
INSTALL_PREFIX=/usr/local

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX}
cmake --build build --parallel $(nproc)
sudo cmake --install build
```

Current install target installs public headers to:
- `${INSTALL_PREFIX}/include/dkyb`

Example include after install:

```c++
#include <dkyb/fix_decoder.h>
```

## Dictionary and Sample Data Setup

Fetch dictionary XML files:

```bash
./scripts/fetch_quickfix_dicts.sh
```

Fetch per-version valid sample messages used by tests:

```bash
./scripts/fetch_sample_fix_messages.sh
```

## Usage

### Example executable (`example_usage`)

Run with all built-in demo messages:

```bash
./build/RelWithDebInfo/bin/example_usage
```

Run with explicit arguments:

```bash
./build/RelWithDebInfo/bin/example_usage \
  data/quickfix \
  "8=FIX.4.2|9=65|35=D|49=BUY|56=SELL|34=2|52=20100225-19:41:57.316|11=ABC|21=1|55=IBM|54=1|60=20100225-19:41:57.316|38=100|40=1|10=062|" \
  "8=FIX.4.2|9=61|35=T|55=IBM|38=100|44=123.45|10=000|" \
  "8=FIXT.1.1|9=108|35=D|1128=9|49=BUY|56=SELL|34=2|52=20260211-12:00:00.000|11=DEF|55=MSFT|54=1|60=20260211-12:00:00.000|38=250|40=2|44=420.50|10=000|"
```

Argument order:
- arg1: dictionary directory (default: `data/quickfix`)
- arg2: message for basic `decode(...)` field dump
- arg3: message for `decodeObject(...)` enum/tag lookup
- arg4: message for `FIXT.1.1` + `ApplVerID` routing example

The executable demonstrates:
- `decode(...)` with field names and typed values
- `decodeObject(...)` with enum-based lookup
- `ApplVerID` (`1128`) driven selection under `FIXT.1.1`
- `generator_map` object construction by `MsgType`

### Library usage (minimal)

```c++
#include "fix_decoder.h"

fix::Decoder decoder;
std::string err;
if(!decoder.loadDictionariesFromDirectory("data/quickfix", &err))
{
    // handle error
}

auto decoded = decoder.decode("8=FIX.4.4|35=1|49=TW|56=ISLD|34=2|52=20240210-12:00:00.000|112=HELLO|");
```

### Library usage (enum-based object access)

```c++
#include "fix_decoder.h"
#include "FIX42_decoder_map.h"

fix::Decoder decoder;
auto msg = decoder.decodeObject("8=FIX.4.2|35=T|55=IBM|38=100|44=123.45|");

auto symbol = msg[fix::generated::fix42::FieldTag::kSymbol].as<std::string_view>();
auto qty = msg[fix::generated::fix42::FieldTag::kOrderQty].as<double>();
auto px = msg[fix::generated::fix42::FieldTag::kPrice].as<double>();

if(symbol && qty && px)
{
    // use *symbol, *qty, *px
}
```

## Testing

Run all tests:

```bash
ctest --test-dir build --output-on-failure
```

The test suite includes:
- dictionary and decoder unit tests
- data-driven tests over all supported FIX versions
- invalid-message mutation tests derived from valid sample messages
- controller unit tests for logon handshake, out-of-sync sequence detection, and garbled-message rejection

## Session Controller and Docker Integration Tests

The new session-level controller (`fix::Controller`) is available in:
- `include/fix_controller.h`
- `src/fix_controller.cc`

Containerized peer-to-peer tests are defined in:
- `docker/fix-controller/compose.yml`
- `docker/fix-controller/Dockerfile.acceptor`
- `docker/fix-controller/Dockerfile.initiator`
- Detailed Docker test documentation: [`docker/fix-controller/README.md`](docker/fix-controller/README.md)

Run FIX handshake containers:

```bash
docker compose -f docker/fix-controller/compose.yml up --build --abort-on-container-exit
```

Run out-of-sync or garbled scenarios:

```bash
FIX_SCENARIO=out_of_sync docker compose -f docker/fix-controller/compose.yml up --build --abort-on-container-exit
FIX_SCENARIO=garbled docker compose -f docker/fix-controller/compose.yml up --build --abort-on-container-exit
```

## Notes and Current Scope

- Generated FIX tag enums are `std::uint32_t`, and dictionary/decoder tag storage uses compatible unsigned tag types.
- The decoder focuses on parsing, version-aware typed decoding, and dictionary-based field metadata resolution.
- It is not a full FIX protocol validator (ordering, required fields, checksum/body-length enforcement, session state).
- QuickFIX-style XML dictionaries are expected as dictionary input.
