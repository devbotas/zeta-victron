#!/usr/bin/env python3
import argparse
import json
import logging
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import os  # Library to detect import libraries
import sys  # system command library

# Victron packages
sys.path.insert(
    1,
    os.path.join(
        os.path.dirname(__file__),
        "/opt/victronenergy/dbus-systemcalc-py/ext/velib_python",
    ),
)

from gi.repository import GLib
from dbus.mainloop.glib import DBusGMainLoop
from dbus.bus import BusConnection
from vedbus import VeDbusService

SERVICE_PREFIX = "com.victronenergy.solarcharger"
PRODUCT_NAME = "Virtual MPPT HTTP Push"
PRODUCT_ID = 0xA042
FIRMWARE_VERSION = "1.0"
HARDWARE_VERSION = "virtual-http"
DBUS_SYSTEM_ADDRESS = "unix:path=/var/run/dbus/system_bus_socket"
STALE_TIMEOUT_SECONDS = 5.0
DEVICE_CLEANUP_INTERVAL_MS = 1000

logging.basicConfig(level=logging.INFO)


def map_state(state_text):
    mapping = {'Off': 0, 'Fault': 2, 'Bulk': 3, 'Absorption': 4, 'Float': 5, 'Equalize': 7}
    return mapping.get(state_text, 0)


def get_value_or_throw(key: str, values: dict):
    if key in values:
        return values[key]
    else:
        raise ValueError(f'Missing key {key} in data.')


class VirtualMppt:
    def __init__(self, values: dict):
        self.device_instance = int(get_value_or_throw('deviceInstance', values))
        self.bus = BusConnection(DBUS_SYSTEM_ADDRESS)
        self.service = VeDbusService(
            f"{SERVICE_PREFIX}.REST_{self.device_instance}",
            bus=self.bus,
            register=False,
        )
        self.last_update = 0.0
        self.lock = threading.Lock()
        self._add_paths(values)
        self.service.register()
        logging.info("Registered D-Bus service for instance %s", self.device_instance)

    def _add_paths(self, values: dict):
        s = self.service

        # Checking if all required values are here
        product_id = int(get_value_or_throw('productId', values), 16)
        fw_version = get_value_or_throw('firmwareVersion', values)
        serial_number = get_value_or_throw('serialNumber', values)

        # Service related paths
        s.add_path('/Mgmt/ProcessName', __file__)
        s.add_path('/Mgmt/ProcessVersion', 'rest-1.0')
        s.add_path('/Mgmt/Connection', 'REST')

        # General device paths
        s.add_path('/DeviceInstance', self.device_instance)
        s.add_path('/ProductId', product_id)
        s.add_path('/ProductName', "Victron MPPT " + serial_number)
        s.add_path('/FirmwareVersion', fw_version)
        s.add_path('/Connected', 1)
        s.add_path('/Serial', serial_number)
        s.add_path('/CustomName', "(Custom name)", writeable=True)

        # Function-related paths
        s.add_path('/State', 0)
        s.add_path('/Mode', 1, writeable=True)
        s.add_path('/ErrorCode', 0)
        s.add_path('/NrOfTrackers', 1)
        s.add_path('/Dc/0/Voltage', 0.0)
        s.add_path('/Dc/0/Current', 0.0)
        s.add_path('/Dc/0/Power', 0.0)
        s.add_path('/Pv/V', 0.0)
        s.add_path('/Yield/Power', 0)
        s.add_path('/Yield/System', 0.0)

        # Currently it is unclear what dbus paths for these should be, need to spy a real device.
        # s.add_path('/History/Daily/0/Yield', 0.0)
        # s.add_path('/History/Daily/1/Yield', 0.0)
        # s.add_path('/History/Daily/0/MaxPower', 0)
        # s.add_path('/History/Daily/1/MaxPower', 0)

    def update_from_payload(self, payload: dict):
        with self.lock:
            # Checking if all required values are here
            state_text = get_value_or_throw('stateText', payload)
            battery_voltage = float(get_value_or_throw('batteryVoltageV', payload))
            battery_current = float(get_value_or_throw('batteryCurrentA', payload))
            error_code = float(get_value_or_throw('errorCode', payload))
            panel_voltage = float(get_value_or_throw('panelVoltageV', payload))
            panel_power = float(get_value_or_throw('panelPowerW', payload))
            # history_yield_today = float(_get_value_or_throw('yieldTodayKWh', payload))
            # history_yield_yesterday = float(_get_value_or_throw('yieldYesterdayKWh', payload))
            # history_max_power_today = int(_get_value_or_throw('maxPowerTodayW', payload))
            # history_max_power_yesterday = int(_get_value_or_throw('maxPowerYesterdayW', payload))

            # If it did not throw at this point, then we have the data and can update dbus endpoints.
            self.service['/Connected'] = 1
            self.service['/State'] = state_text
            self.service['/ErrorCode'] = error_code
            self.service['/Dc/0/Voltage'] = battery_voltage
            self.service['/Dc/0/Current'] = battery_current
            self.service['/Dc/0/Power'] = round(battery_voltage * battery_current, 1)
            self.service['/Pv/V'] = panel_voltage
            self.service['/Yield/Power'] = panel_power

            # Currently it is unclear what dbus paths for these should be, need to spy a real device.
            # self.service['/History/Daily/0/Yield'] = history_yield_today
            # self.service['/History/Daily/1/Yield'] = history_yield_yesterday
            # self.service['/History/Daily/0/MaxPower'] = history_max_power_today
            # self.service['/History/Daily/1/MaxPower'] = history_max_power_yesterday

            self.last_update = time.time()

    def get_age(self):
        return time.time() - self.last_update if self.last_update else 1e9

    def unregister(self):
        try:
            self.service.__del__()
        except Exception:
            pass
        try:
            del self.service
        except Exception:
            pass
        logging.info("Unregistered D-Bus service for instance %s", self.device_instance)


class DeviceManager:
    def __init__(self, stale_timeout: float):
        self.stale_timeout = stale_timeout
        self.devices: dict[int, VirtualMppt] = {}
        self.lock = threading.Lock()

    def update_from_payload(self, payload: dict):
        if 'deviceInstance' not in payload:
            raise ValueError('Missing deviceInstance in payload')

        instance = int(payload['deviceInstance'])
        with self.lock:
            device = self.devices.get(instance)
            if device is None:
                device = VirtualMppt(payload)
                self.devices[instance] = device
            device.update_from_payload(payload)
        return instance

    def run_cleanup(self):
        stale = []
        with self.lock:
            for instance, device in list(self.devices.items()):
                if device.get_age() > self.stale_timeout:
                    stale.append(instance)
            for instance in stale:
                device = self.devices.pop(instance)
                device.unregister()
        return True

    def health(self):
        with self.lock:
            return {
                'deviceCount': len(self.devices),
                'instances': sorted(self.devices.keys())
            }


class Handler(BaseHTTPRequestHandler):
    manager: DeviceManager = None
    api_token = None

    def _send(self, status, body):
        data = json.dumps(body).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_PUT(self):
        if self.path != '/api/update-solar-charger':
            self._send(404, {'ok': False, 'error': 'not_found'})
            return

        if self.api_token and self.headers.get('X-Api-Token') != self.api_token:
            self._send(401, {'ok': False, 'error': 'unauthorized'})
            return

        length = int(self.headers.get('Content-Length', '0'))
        raw = self.rfile.read(length)
        try:
            payload = json.loads(raw.decode('utf-8'))
            instance = self.manager.update_from_payload(payload)
            self._send(200, {'ok': True, 'deviceInstance': instance})
        except Exception as exc:
            logging.exception('PUT failed')
            self._send(400, {'ok': False, 'error': str(exc)})

    def do_GET(self):
        if self.path == '/health':
            self._send(200, {'ok': True, **self.manager.health()})
        else:
            self._send(404, {'ok': False, 'error': 'not_found'})

    def log_message(self, format, *args):
        return


def start_http_server(host, port, manager: DeviceManager, api_token):
    Handler.manager = manager
    Handler.api_token = api_token
    server = ThreadingHTTPServer((host, port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def main():
    parser = argparse.ArgumentParser(description='Dynamic virtual MPPT manager for Venus OS')
    parser.add_argument('--host', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=8090)
    parser.add_argument('--token', default='change-me')
    parser.add_argument('--stale-timeout', type=float, default=STALE_TIMEOUT_SECONDS)
    args = parser.parse_args()

    DBusGMainLoop(set_as_default=True)
    manager = DeviceManager(args.stale_timeout)
    start_http_server(args.host, args.port, manager, args.token)
    GLib.timeout_add(DEVICE_CLEANUP_INTERVAL_MS, manager.run_cleanup)
    logging.info('Listening on http://%s:%s/api/update-solar-charger', args.host, args.port)
    GLib.MainLoop().run()


if __name__ == '__main__':
    main()
