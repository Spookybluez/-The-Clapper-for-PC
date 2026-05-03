"""
Tiny local HTTP listener for ESP32 PC sleep/shutdown requests.

Run on the Windows PC while testing:
    python pc_power_listener.py --dry-run

When dry-run looks good, run without --dry-run to allow sleep:
    python pc_power_listener.py

Endpoints:
    POST /sleep     put Windows to sleep
    POST /shutdown  shut Windows down
    GET  /health    verify listener is reachable
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import subprocess
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class PowerHandler(BaseHTTPRequestHandler):
    dry_run = True
    token = ""

    def log_message(self, format: str, *args: object) -> None:
        print("%s - %s" % (self.address_string(), format % args))

    def do_GET(self) -> None:
        if self.path != "/health":
            self.send_json(404, {"ok": False, "error": "not found"})
            return

        self.send_json(200, {"ok": True, "dry_run": self.dry_run})

    def do_POST(self) -> None:
        if not self.is_authorized():
            self.send_json(401, {"ok": False, "error": "unauthorized"})
            return

        if self.path == "/sleep":
            self.handle_sleep()
        elif self.path == "/shutdown":
            self.handle_shutdown()
        else:
            self.send_json(404, {"ok": False, "error": "not found"})

    def is_authorized(self) -> bool:
        if not self.token:
            return True
        return self.headers.get("X-Clapper-Token", "") == self.token

    def handle_sleep(self) -> None:
        print("Received sleep request")
        if not self.dry_run:
            ctypes.windll.powrprof.SetSuspendState(False, True, False)
        self.send_json(200, {"ok": True, "action": "sleep", "dry_run": self.dry_run})

    def handle_shutdown(self) -> None:
        print("Received shutdown request")
        if not self.dry_run:
            subprocess.run(["shutdown", "/s", "/t", "0"], check=False)
        self.send_json(200, {"ok": True, "action": "shutdown", "dry_run": self.dry_run})

    def send_json(self, status: int, payload: dict[str, object]) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--token", default=os.environ.get("CLAPPER_TOKEN", ""))
    args = parser.parse_args()

    PowerHandler.dry_run = args.dry_run
    PowerHandler.token = args.token
    server = ThreadingHTTPServer((args.host, args.port), PowerHandler)
    print(f"Listening on http://{args.host}:{args.port}")
    print(f"dry_run={args.dry_run}")
    print(f"token_required={bool(args.token)}")
    server.serve_forever()


if __name__ == "__main__":
    main()
