#!/usr/bin/env python3
#
# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Two-process scenario driver for the socket-api sample (used by ctest).
#
# What it does
# ------------
# For a two-process scenario (tcp/udp/timeout/multicast) it starts the wasm
# server app under iwasm, waits for the server port to become free, runs the
# client app, checks the client output for the expected success marker,
# kills the server and reports PASS/FAIL. addr_resolve is single-process and
# just runs the client.
#
# When to use
# -----------
# Standalone:  python3 socket_test.py <scenario> <build-dir>
# Via ctest:   the socket-api CMakeLists.txt registers socket_api_tcp,
#              socket_api_udp, socket_api_timeout (default) and optionally
#              socket_api_multicast / socket_api_addr_resolve.
#
# Inputs
# ------
#   <scenario>  one of: tcp | udp | timeout | multicast | addr_resolve
#   <build-dir> the sample build directory containing iwasm and the .wasm apps
#
# Outputs
# -------
#   stdout: "PASS [socket-api <scenario>]" on success, failure diagnostics
#           otherwise; exit code 0 on success, 1 on failure.

import argparse
import shlex
import socket
import subprocess
import sys
import time

SERVER_CMD = {
    "tcp": "./iwasm --addr-pool=0.0.0.0/15 tcp_server.wasm",
    "udp": "./iwasm --addr-pool=0.0.0.0/15 udp_server.wasm",
    "timeout": "./iwasm --addr-pool=0.0.0.0/15 timeout_server.wasm",
    "multicast": "./iwasm --addr-pool=0.0.0.0/0,::/0 multicast_client.wasm 224.0.0.1",
}

CLIENT_CMD = {
    "tcp": "./iwasm --addr-pool=127.0.0.1/15 tcp_client.wasm",
    "udp": "./iwasm --addr-pool=127.0.0.1/15 udp_client.wasm",
    "timeout": "./iwasm --addr-pool=127.0.0.1/15 timeout_client.wasm",
    # multicast: the host-side sender (native binary) while the wasm
    # multicast_client runs as the server above.
    "multicast": "./multicast_server 224.0.0.1",
    "addr_resolve": "./iwasm --allow-resolve=*.com addr_resolve.wasm github.com",
}

# Marker printed by the client app on success.
CLIENT_PASS_MARKER = {
    "tcp": "[Client] BYE",
    "udp": "[Client] BYE",
    "timeout": "Success. Closing socket",
    "multicast": "Datagram sent",
    "addr_resolve": "IPv4 address:",
}

SERVER_STARTUP_DELAY = 1.0  # seconds

# addr_resolve is a single-process scenario (no server to start).
NEEDS_SERVER = {
    "tcp": True,
    "udp": True,
    "timeout": True,
    "multicast": True,
    "addr_resolve": False,
}

# The wasm server apps all bind port 1234. They cannot set SO_REUSEADDR
# (wasi-sdk 25 does not provide a working setsockopt for it), so after a
# previous server exits the port lingers in TIME_WAIT for a while. Wait for
# the port to become bindable before starting the next server.
SERVER_PORT = 1234
PORT_REUSE_WAIT = 65.0  # seconds, upper bound (TIME_WAIT is ~60s on Linux)


def wait_for_port_free(timeout=PORT_REUSE_WAIT):
    deadline = time.time() + timeout
    while time.time() < deadline:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            # No SO_REUSEADDR: if the port is stuck in TIME_WAIT (the wasm
            # server cannot set it), bind() fails with EADDRINUSE. The probe
            # socket is never listened on, so closing it leaves no TIME_WAIT.
            s.bind(("0.0.0.0", SERVER_PORT))
            return True
        except OSError:
            time.sleep(1)
        finally:
            s.close()
    return False


def run_client(cmd, cwd):
    prc = subprocess.run(shlex.split(cmd), cwd=cwd, check=False,
                         capture_output=True, text=True)
    return prc


def main():
    parser = argparse.ArgumentParser(
        description=("Run one socket-api scenario: start the wasm server "
                     "under iwasm, run the client, assert the expected "
                     "output. See the module docstring for details."),
        epilog=("exit code 0 = PASS, 1 = FAIL (client failed or output "
                "marker missing)"))
    parser.add_argument("scenario",
                        choices=["tcp", "udp", "timeout", "multicast",
                                 "addr_resolve"],
                        help="scenario to run (two-process scenarios start "
                             "a server, addr_resolve is single-process)")
    parser.add_argument("build_dir", type=str,
                        help="sample build directory containing iwasm and "
                             "the .wasm apps")
    args = parser.parse_args()

    server = None
    try:
        if NEEDS_SERVER[args.scenario]:
            if not wait_for_port_free():
                print("port {} still busy; giving up".format(SERVER_PORT))
                return 1
            server = subprocess.Popen(shlex.split(SERVER_CMD[args.scenario]),
                                      cwd=args.build_dir,
                                      stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE)
            time.sleep(SERVER_STARTUP_DELAY)
        prc = run_client(CLIENT_CMD[args.scenario], args.build_dir)
        if prc.returncode != 0:
            print("client {} failed with return code {}".format(
                args.scenario, prc.returncode))
            print(prc.stdout)
            print(prc.stderr)
            return 1
        if CLIENT_PASS_MARKER[args.scenario] not in prc.stdout:
            print("client {} output missing marker '{}':".format(
                args.scenario, CLIENT_PASS_MARKER[args.scenario]))
            print(prc.stdout)
            return 1
        print("PASS [socket-api {}]".format(args.scenario))
        return 0
    finally:
        if server is not None:
            server.kill()


if __name__ == "__main__":
    sys.exit(main())
