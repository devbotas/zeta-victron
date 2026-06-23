#!/usr/bin/env python3

import argparse
import logging
import threading

from http.server import ThreadingHTTPServer
from DeviceManager import DeviceManager
from HttpWrapper import HttpWrapper

from gi.repository import GLib
from dbus.mainloop.glib import DBusGMainLoop

logging.basicConfig(level=logging.INFO)


def main():
    parser = argparse.ArgumentParser(description='Dynamic virtual MPPT manager for Venus OS')
    parser.add_argument('--host', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=8090)
    parser.add_argument('--token', default='change-me')
    parser.add_argument('--stale-timeout', type=float, default=5.0)
    args = parser.parse_args()

    # Don't know what this line does.
    DBusGMainLoop(set_as_default=True)

    # Spinning up DeviceManager.
    manager = DeviceManager(args.stale_timeout)

    # Spinning up HTTP monitor.
    HttpWrapper.manager = manager
    HttpWrapper.api_token = args.token
    server = ThreadingHTTPServer((args.host, args.port), HttpWrapper)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    # Periodically checking if there are outdated devices and removing them.
    GLib.timeout_add(1000, manager.run_cleanup)

    # Done, running forever.
    logging.info('Listening on http://%s:%s/api/update-solar-charger', args.host, args.port)
    GLib.MainLoop().run()


if __name__ == '__main__':
    main()
