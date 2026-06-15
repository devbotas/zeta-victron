import json
import logging
from http.server import BaseHTTPRequestHandler

from DeviceManager import DeviceManager


class HttpWrapper(BaseHTTPRequestHandler):
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

    def log_message(self, format, *args):
        return
