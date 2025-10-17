#!/usr/bin/env python3
"""
Health Endpoint Server for ODR-AudioEnc
StreamDAB - Phase 2.1

Provides HTTP health check endpoint for instance discovery.
Runs alongside ODR-AudioEnc process in multi-instance containers.
"""

import os
import sys
import time
import json
import socket
from http.server import HTTPServer, BaseHTTPRequestHandler
from datetime import datetime
import threading
import signal

# Global state
start_time = time.time()
instance_id = os.environ.get("INSTANCE_ID", "1")
api_port = int(os.environ.get("API_PORT", "9200"))
tcp_port = int(os.environ.get("TCP_PORT", "8010"))
fifo_path = os.environ.get("FIFO_PATH", f"/tmp/fifo/audio{instance_id}.fifo")
version = os.environ.get("ODR_VERSION", "v3.1.1")

# Health status tracking
last_activity = time.time()
health_status = "healthy"
process_status = "running"


class HealthHandler(BaseHTTPRequestHandler):
    """HTTP request handler for health endpoint"""

    def log_message(self, format, *args):
        """Suppress default logging to reduce noise"""
        pass

    def do_GET(self):
        """Handle GET requests"""
        if self.path == "/health":
            self.send_health_response()
        elif self.path == "/status":
            self.send_status_response()
        elif self.path == "/version":
            self.send_version_response()
        else:
            self.send_error(404, "Not Found")

    def send_health_response(self):
        """Send health check response"""
        global last_activity, health_status, process_status

        uptime = time.time() - start_time

        # Check FIFO exists
        fifo_exists = os.path.exists(fifo_path) if fifo_path != "null" else True

        # Determine health status
        if not fifo_exists:
            health_status = "unhealthy"
        elif uptime < 10:
            health_status = "starting"
        elif time.time() - last_activity > 300:
            health_status = "degraded"
        else:
            health_status = "healthy"

        response = {
            "status": health_status,
            "instance_id": int(instance_id),
            "container_instance_id": int(instance_id),
            "component": "odr-audioenc",
            "version": version,
            "uptime_seconds": round(uptime, 2),
            "api_port": api_port,
            "tcp_port": tcp_port,
            "fifo_path": fifo_path if fifo_path != "null" else None,
            "fifo_exists": fifo_exists,
            "process_status": process_status,
            "timestamp": datetime.utcnow().isoformat() + "Z",
        }

        # Update last activity
        last_activity = time.time()

        self.send_json_response(200, response)

    def send_status_response(self):
        """Send detailed status response"""
        uptime = time.time() - start_time

        response = {
            "status": "running",
            "instance_id": int(instance_id),
            "component": "odr-audioenc",
            "version": version,
            "uptime_seconds": round(uptime, 2),
            "configuration": {
                "api_port": api_port,
                "tcp_port": tcp_port,
                "fifo_path": fifo_path if fifo_path != "null" else None,
            },
            "timestamp": datetime.utcnow().isoformat() + "Z",
        }

        self.send_json_response(200, response)

    def send_version_response(self):
        """Send version information"""
        response = {
            "component": "odr-audioenc",
            "version": version,
            "instance_id": int(instance_id),
        }

        self.send_json_response(200, response)

    def send_json_response(self, status_code, data):
        """Send JSON response with proper headers"""
        json_data = json.dumps(data, indent=2)

        self.send_response(status_code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(json_data)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json_data.encode("utf-8"))


def run_health_server(host="0.0.0.0", port=9200):
    """Run the health endpoint server"""
    server_address = (host, port)

    try:
        httpd = HTTPServer(server_address, HealthHandler)
        print(f"[Health] ODR-AudioEnc health server started")
        print(f"[Health] Instance ID: {instance_id}")
        print(f"[Health] Listening on {host}:{port}")
        print(f"[Health] Endpoints: /health, /status, /version")
        print(f"[Health] TCP Port: {tcp_port}")
        print(f"[Health] FIFO Path: {fifo_path}")

        # Handle shutdown gracefully
        def signal_handler(signum, frame):
            print(f"\n[Health] Shutting down health server...")
            httpd.shutdown()
            sys.exit(0)

        signal.signal(signal.SIGTERM, signal_handler)
        signal.signal(signal.SIGINT, signal_handler)

        httpd.serve_forever()

    except OSError as e:
        if e.errno == 98:
            print(f"[Health] ERROR: Port {port} already in use", file=sys.stderr)
            sys.exit(1)
        else:
            print(f"[Health] ERROR: {e}", file=sys.stderr)
            sys.exit(1)
    except Exception as e:
        print(f"[Health] ERROR: {e}", file=sys.stderr)
        sys.exit(1)


def main():
    """Main entry point"""
    print(f"[Health] Starting ODR-AudioEnc Health Server...")
    print(f"[Health] Instance ID: {instance_id}")
    print(f"[Health] API Port: {api_port}")
    print(f"[Health] TCP Port: {tcp_port}")
    print(f"[Health] Version: {version}")

    run_health_server("0.0.0.0", api_port)


if __name__ == "__main__":
    main()
