# Simulations

SHIRE's simulation layer consists of the Simulith Server, the Director process with its loaded component simulator libraries, and the separate 42 dynamics process.

## Simulith Server

The Server owns simulation time.
Its current tick interval is 10 ms.
For every tick it:

1. broadcasts the current simulated time
2. waits for all configured clients to acknowledge completion
3. delays as needed for the requested speed
4. advances simulated time by one interval.

The DRM expects two clients: FSW and the Director.
The focused CLI environment expects one Director client.
From the attached Server console, use `p` to pause or resume, `+` to request a faster rate, and `-` to request a slower rate.
The implementation accepts attempted rates from 1/64x through 1024x, but host performance determines the achieved rate.

## Simulith Director

The Director scans its `components` directory for shared objects.
A loadable simulator exports `get_component_interface`, which returns the lifecycle defined in `simulith/include/simulith_component.h`:

* `init` allocates and initializes component state
* `tick` advances the component using the current simulation time and 42 context
* `cleanup` releases component state
* `backdoor`, when implemented, accepts commands used only for tests

The Director creates one worker thread per loaded component and ticks the active simulators in parallel.
The build script copies only simulators selected for the active spacecraft into the Director image.
At runtime the Director loads each library with `dlopen` and owns its component state.
The loaded simulators are not separate processes or Simulith Server clients.

## 42 integration

42 runs as a separate container.
The Director exchanges state and actuator commands with it through a Unix domain socket in the shared Simulith volume.
The context exposed to component simulators includes simulation and dynamics time, position, velocity, attitude quaternion, body rates, sun and magnetic field vectors, eclipse state, mass properties, and atmospheric density.

Examples in the current component models include:

* ADCS uses 42 attitude, rate, sun, and magnetic field state and queues magnetorquer and reaction wheel commands back to 42.
* EPS derives simulated solar generation from the 42 sun vector and eclipse flag.
* Demo maps the body frame sun vector into three representative data channels unless random data injection is enabled.

The Director also publishes a 42 truth packet to YAMCS on UDP 50042 every 100 Director ticks.

## Simulated device transport

HWLIB's simulation drivers communicate with the simulator libraries loaded inside the Director over ZeroMQ IPC endpoints in the shared `/tmp` volume.
Endpoint families are reserved for UART, I2C, SPI, and GPIO.
Each endpoint is derived from the configured device handle, bus, address, chip select, or pin.

For example, Demo uses a UART endpoint based on `SIMULITH_UART_BASE_PORT + DEMO_CFG_HANDLE`.
The simulator binds the endpoint, while the FSW side UART driver connects to it.
The same Demo device packet format is used by the simulator, the cFS application, and the developer CLI.

## Fault injection

The Director listens for backdoor datagrams on UDP 50060 and dispatches a valid packet to the named component's `backdoor` callback.
Backdoor behavior is specific to each component.
Demo currently supports changing its device configuration and toggling randomized housekeeping or data.
A backdoor is a test interface, not a flight interface, and should only be used in isolated simulation runs.

## Tests

Simulation tests are present in `simulith/test/` and `comp/<name>/test-sim/`.
Run the selected component simulator tests and combined coverage from the repository root:

```bash
make test-sim
```

`make test-sim` builds the Simulith executables needed by the component tests, but it does not run the separate Simulith core test suite.
Run that suite with `cd simulith && make test`.
A passing unit test does not by itself verify a complete mission scenario.
Record the configuration, revision, command, and result for any formal verification claim.

***
Last reviewed: 20260817
