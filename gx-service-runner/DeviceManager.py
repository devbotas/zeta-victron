import threading

from VirtualMppt import VirtualMppt


class DeviceManager:
    def __init__(self, stale_timeout: float):
        self.stale_timeout = stale_timeout
        self.devices: dict[int, VirtualMppt] = {}
        self.lock = threading.Lock()

    def update_from_payload(self, payload: dict):
        if 'deviceInstance' not in payload:
            raise ValueError('Missing deviceInstance member in payload')

        instance = int(payload['deviceInstance'])
        with self.lock:
            device = self.devices.get(instance)
            if device is None:
                device = VirtualMppt(payload)
                self.devices[instance] = device
            device.update(payload)
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
