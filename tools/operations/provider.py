"""Local model protocol fixture with hostile output and unavailable source cases."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import time


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        assert self.path == "/api/chat"
        assert "Authorization" not in self.headers
        prompt = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        assert not prompt.get("tools") and prompt["stream"] is False
        context = json.loads(prompt["messages"][1]["content"])
        source, question = context["source"], context["question"]
        assert "Bearer " not in json.dumps(context)
        if question == "slow":
            time.sleep(2)
        if question == "unavailable":
            self.send_error(503)
            return
        claims = [{"text": "Interpretation of the incident", "sourceId": source["sourceId"], "assumption": True}]
        if question == "missing-source":
            claims[0]["sourceId"] = "foreign-incident"
        if question == "execute-command":
            claims[0]["action"] = "unlock"
        content = {"claims": claims}
        reply = {"model": prompt["model"], "message": {"role": "assistant", "content": json.dumps(content)}, "done": True}
        raw = json.dumps(reply).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)


ThreadingHTTPServer(("0.0.0.0", 11434), Handler).serve_forever()
