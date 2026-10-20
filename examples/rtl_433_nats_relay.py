#!/usr/bin/env python3

"""NATS monitoring relay for rtl_433."""

# With `-F syslog`, rtl_433 sends each decoded reading over UDP as a syslog
# message, with the reading itself as JSON at the end of the message.
# This program receives those messages, takes out the JSON, and publishes
# each reading to a NATS server.
#
# Each device gets its own NATS subject, built from the prefix, model,
# channel and id, e.g.:
#   rtl_433.Acurite-Tower.A.1234
# Subscribe to all readings with e.g. `nats sub 'rtl_433.>'`.
#
# Usage: start this script, then rtl_433 with syslog output, e.g.
#   rtl_433_nats_relay.py &
#   rtl_433 -F syslog::1433
#
# Behaviour on errors:
# - If the NATS server is unreachable, the relay keeps retrying.  Readings
#   are buffered in memory meanwhile (up to 2 MiB) and sent on reconnect.
#   Once the buffer is full, further readings are dropped and counted.
# - If the server rejects the credentials, the relay exits with an error,
#   as retrying will not help.
#
# Dependencies:
#   nats-py; see https://pypi.org/project/nats-py/ (pip install nats-py)
#   For --creds also nkeys (pip install 'nats-py[nkeys]')

import argparse
import asyncio
import json
import logging
import socket
import sys

import nats

UDP_IP = "127.0.0.1"
UDP_PORT = 1433
NATS_SERVER = "nats://127.0.0.1:4222"
NATS_PREFIX = "rtl_433"

# Characters with a special meaning in NATS subjects: '.' separates tokens,
# '*' and '>' are wildcards, and whitespace is not allowed.
UNSAFE_SUBJECT_CHARS = ". \t*>/"


def sanitize(value):
    """Make a value safe to use as one token of a NATS subject."""
    token = str(value)
    for char in UNSAFE_SUBJECT_CHARS:
        token = token.replace(char, "_")
    return token


def subject_for(prefix, reading):
    """Build the subject for a reading from the fields that identify a device."""
    tokens = [prefix]
    for key in ("model", "channel", "id"):
        if key in reading:
            tokens.append(sanitize(reading[key]))
    return ".".join(tokens)


def extract_payload(datagram):
    """Return the json payload of a syslog datagram from rtl_433."""
    line = datagram.decode("utf-8")
    if line.startswith("<"):
        # Syslog fields are "<PRI>VER", timestamp, hostname, app, pid, msgid,
        # structured data, then the payload, which may itself contain spaces.
        line = line.split(None, 7)[-1]
    # Otherwise assume a plain json datagram without a syslog header.
    return line


def is_auth_error(error):
    """Return True if a NATS error means the credentials were rejected."""
    # nats-py raises AuthorizationError when the server revokes access, but
    # only passes on the server's error text when the initial connect fails.
    return (isinstance(error, nats.errors.AuthorizationError)
            or "authorization violation" in str(error).lower())


class RelayError(Exception):
    """An error the relay can not recover from."""


class SyslogReceiver(asyncio.DatagramProtocol):
    """Receive rtl_433 syslog datagrams and queue the readings they contain."""

    def __init__(self, queue):
        self.queue = queue

    def datagram_received(self, datagram, addr):
        try:
            payload = extract_payload(datagram)
            reading = json.loads(payload)
        except ValueError:
            return  # Not json, ignore it.
        if isinstance(reading, dict):
            self.queue.put_nowait((reading, payload))


class NatsRelay:
    """Relay rtl_433 readings from a UDP syslog socket to NATS."""

    def __init__(self, args):
        self.args = args
        self.nc = None
        self.readings = asyncio.Queue()
        # Readings dropped while disconnected because the buffer was full.
        self.dropped = 0
        # Resolved with a reason when the relay has to stop.
        self.stop_reason = asyncio.get_running_loop().create_future()

    async def run(self):
        await self.connect()
        transport = await self.listen()
        try:
            await self.relay_readings()
        finally:
            transport.close()
            await self.disconnect()

    async def connect(self):
        self.nc = await self.unless_stopped(nats.connect(**self.connect_options()))
        logging.info("NATS connected to %s", self.nc.connected_url.netloc)

    def connect_options(self):
        options = {
            "servers": self.args.server,
            "name": "rtl_433_nats_relay",
            "max_reconnect_attempts": -1,  # Retry forever.
            "disconnected_cb": self.on_disconnected,
            "reconnected_cb": self.on_reconnected,
            "error_cb": self.on_error,
            "closed_cb": self.on_closed,
        }
        if self.args.user:
            options["user"] = self.args.user
            options["password"] = self.args.password
        if self.args.token:
            options["token"] = self.args.token
        if self.args.creds:
            options["user_credentials"] = self.args.creds
        return options

    async def listen(self):
        """Start receiving syslog datagrams into the readings queue."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.bind((self.args.udp_ip, self.args.udp_port))
        transport, _ = await asyncio.get_running_loop().create_datagram_endpoint(
            lambda: SyslogReceiver(self.readings), sock=sock)
        return transport

    async def relay_readings(self):
        while True:
            reading, payload = await self.unless_stopped(self.readings.get())
            await self.publish(subject_for(self.args.prefix, reading), payload)

    async def publish(self, subject, payload):
        logging.debug("publish %s %s", subject, payload)
        try:
            await self.nc.publish(subject, payload.encode("utf-8"))
        except nats.errors.OutboundBufferLimitError:
            # Disconnected, and the reconnect buffer is full.
            if self.dropped == 0:
                logging.warning("NATS reconnect buffer full, dropping readings")
            self.dropped += 1
        except nats.errors.ConnectionClosedError:
            pass  # on_closed() has set stop_reason, the relay loop will stop.

    async def disconnect(self):
        # drain() sends any buffered readings first, but fails while reconnecting.
        if self.nc.is_connected:
            await self.nc.drain()
        else:
            await self.nc.close()

    def stop(self, reason):
        if not self.stop_reason.done():
            self.stop_reason.set_result(reason)

    async def unless_stopped(self, awaitable):
        """Wait for awaitable, but raise RelayError if the relay has to stop first."""
        task = asyncio.ensure_future(awaitable)
        await asyncio.wait((task, self.stop_reason), return_when=asyncio.FIRST_COMPLETED)
        if self.stop_reason.done():
            task.cancel()
            raise RelayError(self.stop_reason.result())
        return task.result()

    # Callbacks from the NATS client.

    async def on_disconnected(self):
        logging.info("NATS disconnected")

    async def on_reconnected(self):
        logging.info("NATS reconnected to %s", self.nc.connected_url.netloc)
        if self.dropped:
            logging.warning("Dropped %d readings while disconnected", self.dropped)
            self.dropped = 0

    async def on_error(self, error):
        if is_auth_error(error):
            self.stop("NATS authorization failed, check the credentials")
        else:
            logging.warning("NATS error: %s", error)

    async def on_closed(self):
        # The client only closes by itself on errors it will not retry.
        last_error = self.nc.last_error if self.nc else None
        self.stop("NATS connection closed (%s)" % (last_error or "no error"))


async def run_relay(args):
    await NatsRelay(args).run()


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-H", "--udp-ip", default=UDP_IP,
                        help="address to listen on for syslog (default: %(default)s)")
    parser.add_argument("-P", "--udp-port", type=int, default=UDP_PORT,
                        help="port to listen on for syslog (default: %(default)s)")
    parser.add_argument("-s", "--server", action="append",
                        help="NATS server URL, may be repeated (default: %s)" % NATS_SERVER)
    parser.add_argument("-p", "--prefix", default=NATS_PREFIX,
                        help="subject prefix (default: %(default)s)")
    parser.add_argument("--user", help="NATS username")
    parser.add_argument("--password", help="NATS password")
    parser.add_argument("--token", help="NATS auth token")
    parser.add_argument("--creds", help="NATS credentials file")
    parser.add_argument("-d", "--debug", action="store_true", help="enable debug logging")
    args = parser.parse_args()

    if not args.server:
        args.server = [NATS_SERVER]
    if args.creds:
        # nats-py would keep retrying with a missing file, so check it up front.
        try:
            open(args.creds).close()
        except OSError as e:
            parser.error("can not read credentials file: %s" % e)
    return args


def main():
    args = parse_args()
    logging.basicConfig(format="[%(asctime)s] %(levelname)s:%(name)s:%(message)s",
                        datefmt="%Y-%m-%dT%H:%M:%S%z",
                        level=logging.DEBUG if args.debug else logging.INFO)
    try:
        asyncio.run(run_relay(args))
    except KeyboardInterrupt:
        pass
    except RelayError as e:
        logging.error("%s", e)
        sys.exit(1)


if __name__ == "__main__":
    main()
