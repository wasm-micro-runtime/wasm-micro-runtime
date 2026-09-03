# Socket sample 
this sample demonstrates the use of WASI API to interact with sockets.

> ❗ **Important:** This sample was ported/adapted from the http_get zephyr sample. The original sample can be found [here]( https://github.com/zephyrproject-rtos/zephyr/blob/main/samples/net/sockets/http_get/src/http_get.c).

> 🛠️ **Work in progress:** The sample is functional but be aware that just a small part of WASI socket API was tested.
> Actual Zephyr APIs: 
> * socket creation = `zsock_socket`
> * socket connection = `zsock_connect`
> * socket emission = `zsock_sendto`
> * socket reception = `zsock_recvfrom`
> * socket destruction = `zsock_close`
>
> With the sockets most API are in fact provided by the runtime instead of WASI because of the lack of socket support in WASI preview1.

## Setup
1. Connect a network cable to the board ethernet port.
2. Configure the network interface on the host machine
    ```
    Internet Protocol Version 4 (TCP/IPv4) Properties:
        IP Address:         192.0.2.10
        Subnet Mask:        255.255.255.0
        Default Gateway:    192.0.2.2
    ```
3. Start a simple HTTP server on the host machine.
    ```bash
    python3 -m http.server --bind 0.0.0.0
    ```
4. Disable any firewall that may block the connection.

## Configuration

The sample supports Zephyr 3.7 or newer. The common fine-grained POSIX options
are enabled in [prj.conf](./prj.conf). Starting with Zephyr 4.3, those options
require `CONFIG_POSIX_SYSTEM_INTERFACES`, so CMake automatically adds
[zephyr_4.3_plus.conf](./zephyr_4.3_plus.conf) when building against Zephyr 4.3
or newer.

To configure the server side IP address and port modify the following lines in the `http_get.c` file.

1. The `HTTP_HOST` and `HTTP_PORT` macros define the server IP address and port.
    ```c
    /* HTTP server to connect to */
    #define HTTP_HOST "192.0.2.10"
    /* Port to connect to, as string */
    #define HTTP_PORT "8000"
    /* HTTP path to request */
    #define HTTP_PATH "/"

    // ...

    #define REQUEST "GET " HTTP_PATH " HTTP/1.0\r\nHost: " HTTP_HOST "\r\n\r\n"
    ```
    > 📄 **Notes:** These macros are used to build the request string, but they are not used to instantiate the address structure. Because at one point we didn't want to use `inet_pton` to convert the string to an address and it remained like this.

2. The `addr` structure is used to store the server address.
    ```c
    addr.sin_port = htons(8000);
        addr.sin_addr.s_addr =
            htonl(0xC000020A); // hard coded IP address for 192.0.2.10
    ```

To configure the authorized IP address(es) modify the following lines in the `main.c` file. WAMR will only allow the IP addresses in the pool to connect to the server.
```c
#define ADDRESS_POOL_SIZE 1
    const char *addr_pool[ADDRESS_POOL_SIZE] = {
        "192.0.2.10/24",
    };
```
See the [platform README](../README.md) for environment setup, workspace layout
and flashing. Completing the request needs a real network interface. The sample
builds and runs on `native_sim`, but without a reachable peer the connect fails,
the module exits with code 2 and the Zephyr application reports
`ERROR: the HTTP request reported code 2`.

## Test status

The scenarios are declared in [sample.yaml](./sample.yaml); twister decides the
verdict from the console output. Last run with
[build_and_run.py](../build_and_run.py) on 2026-08-06:

| Scenario | Simulator | Result |
| --- | --- | --- |
| `sample.wamr.simple_http` | `native_sim` | built (`build_only`) |

The scenario is `build_only` because completing the request needs a reachable
HTTP server on the host side. Running it without one gets as far as
`ERROR: connect to 192.0.2.10:8000 failed with errno 73` and the host then
returns 2. `qemu_arc/qemu_arc_hs` is not in `platform_allow`: the sample
enables `CONFIG_FILE_SYSTEM_LITTLEFS`, for which that board has no flash
partition.

## Run Command
* **Zephyr Build**

    The runtime comes from the `wasm-micro-runtime` Zephyr module and is
    configured with the `CONFIG_WAMR_*` options in [prj.conf](./prj.conf); no
    environment variables or wasi-sdk paths are needed to build the
    application.

    It builds and runs on `native_sim`. The request only succeeds when the host
    side is set up as described above; otherwise the runtime, the WASI socket
    layer and the module still execute, and the failing connect is reported:

    ```bash
    python3 ../build_and_run.py simple-http
    ```

    For a real board, replace the board identifier and add a
    `boards/<board-identifier>.conf` with the board specific settings:

    ```bash
    west build . -b nucleo_h563zi -p always
    ```

* **WebAssembly Module**

    [wasm-app/http_get.c](./wasm-app/http_get.c) is the only tracked form of
    the module: the build compiles it — together with `wasi_socket_ext.c` from
    [lib-socket](../../../../core/iwasm/libraries/lib-socket), which provides
    the socket API — to `http_get.wasm` and generates the `http_get.h` that
    [src/main.c](./src/main.c) embeds, both under the build directory. Editing
    the C file is enough, the next `west build` regenerates the header. See
    [wasm-app/CMakeLists.txt](./wasm-app/CMakeLists.txt) for the compile and
    link options.

    The wasi-sdk providing the compiler is looked up in `/opt/wasi-sdk` and
    `/opt/wasi-sdk-*`, where the Docker image installs it; set `WASISDK_ROOT`
    or `WASI_SDK_DIR` if it lives elsewhere. Its wasi-libc uses the reference
    types proposal, hence `CONFIG_WAMR_REF_TYPES=y` in [prj.conf](./prj.conf).

    Failures are reported as `ERROR: ...` with a distinct exit code per
    operation: 1 socket, 2 connect, 3 send, 4 receive. A completed request
    ends with `PASS: the HTTP request completed`. See
    [Reporting results](../README.md#reporting-results).

## Output
The output should be similar to the following:
```bash
*** Booting Zephyr OS build v3.6.0-4305-g2ec8f442a505 ***
[00:00:00.061,000] <inf> net_config: Initializing network
[00:00:00.067,000] <inf> net_config: Waiting interface 1 (0x2000a910) to be up...
[00:00:03.158,000] <inf> phy_mii: PHY (0) Link speed 100 Mb, full duplex

[00:00:03.288,000] <inf> net_config: Interface 1 (0x2000a910) coming up
[00:00:03.295,000] <inf> net_config: IPv4 address: 192.0.2.1
global heap size: 131072
Wasm file size: 36351
main found
[wasm-mod] Preparing HTTP GET request for http://192.0.2.10:8000/
[wasm-mod] sock = 3
[wasm-mod] connect rc = 0
[wasm-mod] send rc = 36
[wasm-mod] Response:

HTTP/1.0 200 OK
Server: SimpleHTTP/0.6 Python/3.10.10
Date: Fri, 14 Jun 2024 07:26:56 GMT
Content-type: text/html; charset=utf-8
Content-Length: 2821

# Skip the HTML content

[wasm-mod] len = 0 break

[wasm-mod] Connection closed
main executed
wasi exit code: 0
PASS: the HTTP request completed
elapsed: 405ms
```