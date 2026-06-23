import logging
import threading
import time
from dbus.bus import BusConnection

import os
import sys

# Victron local packages
sys.path.insert(
    1,
    os.path.join(
        os.path.dirname(__file__),
        "/opt/victronenergy/dbus-systemcalc-py/ext/velib_python",
    ),
)
from vedbus import VeDbusService


def get_value_or_throw(key: str, values: dict):
    if key in values:
        return values[key]
    else:
        raise ValueError(f'Missing key {key} in data.')


class VirtualMppt:
    def __init__(self, values: dict):
        self.device_instance = int(get_value_or_throw('deviceInstance', values))
        self.bus = BusConnection("unix:path=/var/run/dbus/system_bus_socket")
        self.service = VeDbusService(
            f"com.victronenergy.solarcharger.REST_{self.device_instance}",
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
        s.add_path('/CustomName', "Victron MPPT " + serial_number, writeable=True)

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

    def update(self, values: dict):
        with self.lock:
            # Checking if all required values are here
            state_text = get_value_or_throw('stateText', values)
            battery_voltage = float(get_value_or_throw('batteryVoltageV', values))
            battery_current = float(get_value_or_throw('batteryCurrentA', values))
            error_code = float(get_value_or_throw('errorCode', values))
            panel_voltage = float(get_value_or_throw('panelVoltageV', values))
            panel_power = float(get_value_or_throw('panelPowerW', values))
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
