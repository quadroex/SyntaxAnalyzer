import argparse
import json
from pathlib import Path
import subprocess
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit


ROOT = Path(__file__).resolve().parent
EXAMPLES = {path.name: path for path in sorted((ROOT / "examples").glob("*.cpp"))}


class Handler(BaseHTTPRequestHandler):
    def send(self, status, body, content_type="application/json; charset=utf-8"):
        data = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/api/health":
            self.send(200, json.dumps({"service": "MiniCpp", "ok": True}))
        elif path == "/api/examples":
            self.send(200, json.dumps(list(EXAMPLES)))
        elif path.startswith("/examples/") and path[len("/examples/"):] in EXAMPLES:
            self.send(200, EXAMPLES[path[len("/examples/"):]].read_text(encoding="utf-8"), "text/plain; charset=utf-8")
        elif path in ("/", "/index.html", "/app.js", "/style.css"):
            name = "index.html" if path == "/" else path[1:]
            content_type = {"index.html": "text/html", "app.js": "text/javascript", "style.css": "text/css"}[name]
            self.send(200, (ROOT / "web" / name).read_text(encoding="utf-8"), content_type + "; charset=utf-8")
        else:
            self.send(404, json.dumps({"error": "Not found"}))

    def do_POST(self):
        if urlsplit(self.path).path != "/api/analyze":
            self.send(404, json.dumps({"error": "Not found"}))
            return
        origin = self.headers.get("Origin")
        allowed = {f"http://127.0.0.1:{self.server.server_port}", f"http://localhost:{self.server.server_port}"}
        if origin and origin not in allowed:
            self.send(403, json.dumps({"error": "Origin is not allowed"}))
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 200000:
                raise ValueError("Invalid request size")
            request = json.loads(self.rfile.read(length).decode("utf-8"))
            if not isinstance(request, dict):
                raise ValueError("Request must be a JSON object")
            source = request.get("source")
            input_text = request.get("input", "")
            if not isinstance(source, str):
                raise ValueError("Source must be a string")
            if not isinstance(input_text, str):
                raise ValueError("Runtime input must be a string")
            if len(source.encode("utf-8")) > 32768:
                raise ValueError("Source limit is 32768 bytes")
            if len(input_text.encode("utf-8")) > 32768:
                raise ValueError("Runtime input limit is 32768 bytes")
            process = subprocess.run(
                [str(ROOT / "build" / "minicpp"), "analyze", "-", "--input", input_text],
                input=source, text=True, encoding="utf-8", capture_output=True, timeout=8,
            )
            result = json.loads(process.stdout)
            self.send(200, json.dumps(result, ensure_ascii=False))
        except (ValueError, UnicodeError) as error:
            self.send(400, json.dumps({"ok": False, "error": {"stage": "Input", "message": str(error), "line": 1, "column": 1}}))
        except subprocess.TimeoutExpired:
            self.send(200, json.dumps({"ok": False, "error": {"stage": "Runtime", "message": "Analysis time limit exceeded", "line": 1, "column": 1}}))
        except (OSError, subprocess.SubprocessError) as error:
            self.send(500, json.dumps({"ok": False, "error": {"stage": "Server", "message": str(error), "line": 1, "column": 1}}))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    arguments = parser.parse_args()
    if not (ROOT / "build" / "minicpp").exists():
        raise SystemExit("Build the project with make first.")
    try:
        server = ThreadingHTTPServer(("127.0.0.1", arguments.port), Handler)
    except OSError as error:
        raise SystemExit(f"Cannot start server on port {arguments.port}. {error}")
    print(f"MiniCpp is ready at http://127.0.0.1:{arguments.port}", flush=True)
    print("Press Ctrl+C to stop.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
