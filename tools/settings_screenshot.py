#!/usr/bin/env python3
"""Screenshot the settings page with example data (for the README).

Serves the page embedded in include/webpage.h on localhost with a mocked
/api/config and /api/status, then captures it with headless Chrome.

    python3 tools/settings_screenshot.py      # writes docs/settings.png
"""

import http.server
import json
import os
import re
import subprocess
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

CONFIG = {
    "nightscout": "https://yoursite.example.com", "tokenSet": True, "showMgdl": False,
    "redLow": 3.9, "yellowLow": 4.5, "yellowHigh": 9.0, "redHigh": 11.0, "staleMinutes": 10,
    "brightness": [40, 100, 10], "nightMode": True, "nightBrightness": 5,
    "nightStart": "22:00", "nightEnd": "07:00", "timeFormat": 0, "dateFormat": 0,
    "tz": "GMT0BST,M3.5.0/1,M10.5.0", "version": "1.1.0", "build": "Oct 14 2026 14:34:15",
    "wifi": [{"ssid": "MyHomeWiFi", "orig": 0, "hasPass": True}],
}
STATUS = {"time": "23:47", "date": "Wed 14 Oct", "temp": "19°C", "hasBg": True, "bg": "5.8",
          "delta": "+0.2", "age": "2 min ago", "stale": False, "range": "InRange"}


def page_html():
    src = open(os.path.join(ROOT, "include", "webpage.h"), encoding="utf-8").read()
    return re.search(r'R"HTML\((.*)\)HTML"', src, re.S).group(1)


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body, kind = {
            "/api/config": (json.dumps(CONFIG), "application/json"),
            "/api/status": (json.dumps(STATUS), "application/json"),
        }.get(self.path, (page_html(), "text/html"))
        data = body.encode()
        self.send_response(200)
        self.send_header("Content-Type", kind + "; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *args):
        pass


def main():
    server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    out = os.path.join(ROOT, "docs", "settings.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                    "--force-device-scale-factor=2", "--window-size=500,1240",
                    "--virtual-time-budget=4000", f"--screenshot={out}",
                    f"http://127.0.0.1:{server.server_port}/"],
                   check=True, capture_output=True)
    server.shutdown()
    print(out)


if __name__ == "__main__":
    main()
