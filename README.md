# IoTFoundry Endpoint Template Code

![License](https://img.shields.io/github/license/PICMG/iot-foundry-endpoint)
![Coverage](https://img.shields.io/codecov/c/github/PICMG/iot-foundry-endpoint)
![Issues](https://img.shields.io/github/issues/PICMG/iot-foundry-endpoint)
![Forks](https://img.shields.io/github/forks/PICMG/iot-foundry-endpoint)
![Stars](https://img.shields.io/github/stars/PICMG/iot-foundry-endpoint)
![Last Commit](https://img.shields.io/github/last-commit/PICMG/iot-foundry-endpoint)

This project implements template code for creating IoTFoundry endpoints that support PLDM over MCTP.  Maintining this architectural-neutral set of code decouples platform dependent development from common endpoint functions, which eases testing, ensures greater compatibility, and encourages the development of endpoints on a wide variety of hardware architectures.

Although this template code is not intended to stand alone, test functions and a simple mock-platform c file enable unit testing of this project's code when performing code maintenance and feature updates.

This repository is part of the IoTFoundry family of open source projects.  For more information about IoTFoundry, please visit the main IoTFoundry site at: [https://picmg.github.io/iot-foundry/](https://picmg.github.io/iot-foundry/)

## Repository Resources
* .\src - the source files for implementing the mctp endpoint
* .\include - the header files that expose the public interface for the MCTP/PLDM library functions.

## System Requirements
The following are system requirements for buidling/testing teh code in this library.

- Linux with the gnu toolchain and make tools installed.

## Architecture
The IoTFoundry MCTP implementation separates receive and transmit responsibilities to keep the framer simple and predictable. On the receive path a small streaming framer reads bytes from the platform serial interface and assembles logical frames in a single pre-allocated buffer; it recognizes SOF/EOF and escape sequences, validates length and FCS, and accepts only frames addressed to the endpoint (or broadcast/all-endpoints). 

When a complete, valid frame is available, the framer transitions to an awaiting-response state and the upper-layer processing code consumes the frame from the same buffer. This design minimizes buffer usage by reusing the same array for both inbound assembly and outbound responses and intentionally avoids concurrent parsing of multiple complete frames in the baseline half-duplex configuration.

The transmit path is implemented as a non-blocking, reentrant sender that writes bytes to the platform only when `platform_serial_can_write()` indicates capacity. Transmit state (current index, total length, and any pending escape continuation) is tracked so partial writes resume correctly on the next opportunity. For constrained devices the default behavior is half-duplex: once a frame begins transmitting its bytes complete on the wire before another frame starts, ensuring no interleaving of bytes between frames. 

Optionally (compile-time) a single prioritized event transmit buffer can be enabled; this additional static slot holds an endpoint-originated datagram and is given preference at frame boundaries when selecting the next frame to send. The event slot does not preempt a frame already in progress and it uses the same on-wire formatting and escaping rules as the primary transmit buffer, keeping the runtime behavior predictable while adding minimal memory overhead.

## Testing

The test runner in `tests/` builds the MCTP implementation together with the
platform mock (`platform_mock.c`) so the protocol code can be exercised on
the host using `gcc`.

From the project root you can build and run the tests with:

```
make -C tests        # builds the test executable (target: test_mctp)
make -C tests run    # builds (if needed) and runs the test executable
```

You can also run the test binary directly after building:

```
./tests/test_mctp
```

To generate coverage information (gcov):

```
make -C tests coverage
```

To remove test artifacts:

```
make -C tests clean
```

The test target compiles `../src/mctp.c` and `../src/fcs.c` together with
`platform_mock.c` and `test_mctp.c`, so no extra configuration is required to
use the mock platform for unit tests.

## Creating a new IoTFoundry Platform

If you are developing a new platform integration for IoTFoundry, create a
separate repository that implements the platform-specific glue (for example
`platform.c`) and any board-specific configuration. The platform repository
should implement the functions declared in `platform.h` so the core MCTP/PLDM
code in this repo can be reused without modification.

A common pattern is to fetch the portable MCTP source and headers from this
repository at build time (for example using `wget`) and then build/link them
together with the platform-specific `platform.c`. Build and test procedures
should be tailored to the platform (native host tests may not apply). When
designing platform code prefer generic implementations that cover a family of
devices (e.g., write Arduino support to work across common Arduino models).

A minimal, recommended layout for a platform-specific repository:

```
my-platform-repo/
├─ README.md
├─ LICENSE
├─ platform.c          # platform glue implementing functions from include/platform.h
├─ include/            # public headers for platform consumers
│  └─ core/            # core headers copied from this project (place core headers here)
├─ src/                # optional platform helpers and drivers
├─ core/               # core C sources copied from this project (place core .c files here)
│  ├─ mctp.c
│  └─ fcs.c
├─ tests/              # platform-specific tests and mocks (if applicable)
└─ ci/                 # CI pipelines / scripts
```
Example Makefile snippet (platform repo) that downloads this repo's sources and builds with a local `platform.c`:

```makefile
# fetch core sources from IoTFoundry template and place them under `core/` and `include/core/`
CORE_URL=https://raw.githubusercontent.com/PICMG/iotfoundry_endpoint_template/main
CORE_SRCS=mctp.c fcs.c
CORE_HDRS=mctp.h platform.h

download-core:
      mkdir -p core include/core
      # download C sources into core/
      $(foreach f,$(CORE_SRCS),wget -q -O core/$(f) $(CORE_URL)/src/$(f);)
      # download header files into include/core/
      $(foreach h,$(CORE_HDRS),wget -q -O include/core/$(h) $(CORE_URL)/include/$(h);)

all: download-core platform_build

platform_build:
      # build platform-specific binary linking core/*.c and your platform.c
      $(CC) -Iinclude -Iinclude/core -o my_platform platform.c core/mctp.c core/fcs.c
```

A more robust approach that avoids naming files explicitly is to download
the repository tarball and extract only the `src/` and `include/` trees,
then use Makefile wildcards when compiling. Pin the ref (`CORE_REF`) to a
tag or commit SHA to avoid breakage when upstream changes.

```makefile
CORE_REPO = https://github.com/PICMG/iotfoundry_endpoint_template
CORE_REF  = main  # pin to a release tag or commit SHA for stability

download-core:
      mkdir -p core include/core /tmp/core-tmp
      curl -L $(CORE_REPO)/archive/refs/heads/$(CORE_REF).tar.gz \
            | tar -xz -C /tmp/core-tmp
      # move extracted src/ and include/ into our layout
      mv /tmp/core-tmp/*/src/* core/ || true
      mv /tmp/core-tmp/*/include/* include/core/ || true
      rm -rf /tmp/core-tmp

# Build using wildcards so new files are picked up automatically
CPPFLAGS += -Iinclude -Iinclude/core
SRCS := $(wildcard core/*.c)
OBJS := $(SRCS:.c=.o)

all: my_platform

my_platform: $(OBJS) platform.o
      $(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
      $(CC) $(CPPFLAGS) -c -o $@ $<
```
When a platform repository downloads core sources from this project during
its build, it is useful to ignore those downloaded files so they are not
accidentally committed. Add the following to your platform repo's
`.gitignore` (adjust paths as needed):

```gitignore
# core C sources placed under `core/`
core/
core/*.c

# core headers placed under `include/core/`
include/core/
include/core/*.h
```

Consult `platform.h` to see the minimal set of functions you must provide and
document any additional platform requirements in your platform repository's
README. Tests for platform repositories should focus on hardware-specific
behaviour and may provide their own mocks or CI tailored to the target device.

Keep `platform.c` generic where possible so it can support families of
devices (e.g., multiple Arduino variants). Use `vendor/` for fetched core
sources and add it to `.gitignore` to avoid committing generated/downloaded
files.
