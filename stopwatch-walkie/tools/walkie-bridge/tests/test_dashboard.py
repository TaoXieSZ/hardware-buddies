from __future__ import annotations

import json
import threading
import urllib.error
import urllib.request

import pytest

from dashboard import DashboardServer, DashboardState, MAX_EVENTS, start_dashboard


def test_snapshot_events_are_monotonic_bounded_immutable_and_gap_aware():
    state = DashboardState(capacity=3, clock=lambda: 123.456)
    state.publish("watch.connected", connection_id="one")
    returned = state.snapshot()
    returned["watch"]["connected"] = False
    assert state.snapshot()["watch"]["connected"] is True

    state.publish("utterance.started", utterance_id="utt")
    state.publish("asr.started", utterance_id="utt", bytes=20)
    state.publish("asr.completed", utterance_id="utt", bytes=20, latency_ms=7, text="hello")
    assert state.events(0)["gap"] is True
    page = state.events(1)
    assert [event["sequence"] for event in page["events"]] == [2, 3, 4]
    assert page["next_sequence"] == 4
    page["events"][0]["type"] = "mutated"
    assert state.events(1)["events"][0]["type"] == "utterance.started"


def test_concurrent_publish_and_read_is_safe():
    state = DashboardState()
    failures = []

    def writer(prefix):
        try:
            for index in range(100):
                state.publish("asr.completed", utterance_id=f"{prefix}-{index}", text="并发", latency_ms=index)
                state.snapshot()
        except Exception as exc:  # pragma: no cover - assertion below captures thread failures
            failures.append(exc)

    threads = [threading.Thread(target=writer, args=(str(i),)) for i in range(4)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert failures == []
    assert state.snapshot()["latest_sequence"] == 400
    assert len(state.events(200)["events"]) <= 100


def test_projection_is_allowlisted_bounded_and_excludes_sensitive_fields():
    state = DashboardState()
    secret = "sk-ws-secret-material"
    state.publish(
        "asr.completed", text="你" * 200, utterance_id="utt", api_key=secret,
        cwd="/private/work", surface_id="surface-secret", pcm=b"audio", raw_payload={"token": secret})
    state.publish("control.snapshot", healthy=True, revision=3, sessions=[{
        "agent": "codex", "label": "codex test", "project_label": "hardware-buddies",
        "state": "idle", "session_key": "opaque-secret", "cwd": "/private/work",
        "surface": "surface-secret", "capabilities": {"steer": True, "permission_reply": False},
    }])
    encoded = json.dumps({"snapshot": state.snapshot(), "events": state.events(0)}, ensure_ascii=False)
    assert len(state.snapshot()["pipeline"]["transcript"].encode("utf-8")) <= 160
    for forbidden in (secret, "/private/work", "surface-secret", "opaque-secret", "audio"):
        assert forbidden not in encoded
    assert "connection_id" not in encoded


def _request(base, path="/", method="GET"):
    request = urllib.request.Request(base + path, method=method)
    try:
        response = urllib.request.urlopen(request, timeout=2)
        return response.status, dict(response.headers), response.read()
    except urllib.error.HTTPError as exc:
        return exc.code, dict(exc.headers), exc.read()


@pytest.fixture
def dashboard_server(tmp_path):
    state = DashboardState()
    for name, body in (("index.html", "<html>dashboard</html>"), ("dashboard.css", "body{}"),
                       ("dashboard.js", "void 0")):
        (tmp_path / name).write_text(body)
    server = DashboardServer(state, port=0, asset_dir=tmp_path).start()
    try:
        yield state, server, f"http://127.0.0.1:{server.port}"
    finally:
        server.stop()


def test_http_status_events_assets_headers_and_read_only_contract(dashboard_server):
    state, _server, base = dashboard_server
    state.publish("route.failed", code="target_required", candidates=["codex test"])

    status, headers, body = _request(base, "/")
    assert status == 200 and b"dashboard" in body
    assert headers["Cache-Control"] == "no-store"
    assert "default-src 'self'" in headers["Content-Security-Policy"]
    assert headers["X-Content-Type-Options"] == "nosniff"

    status, _headers, body = _request(base, "/api/status")
    assert status == 200 and json.loads(body)["pipeline"]["error_code"] == "target_required"
    status, _headers, body = _request(base, "/api/events?after=0")
    assert status == 200 and json.loads(body)["events"][-1]["type"] == "route.failed"
    assert _request(base, "/api/events?after=bad")[0] == 400
    assert _request(base, "/missing")[0] == 404
    assert _request(base, "/api/status", method="POST")[0] == 405


def test_server_stop_releases_port_and_repeated_start_is_clean(tmp_path):
    for name in ("index.html", "dashboard.css", "dashboard.js"):
        (tmp_path / name).write_text(name)
    first = DashboardServer(DashboardState(), port=0, asset_dir=tmp_path).start()
    port = first.port
    first.stop()
    second = DashboardServer(DashboardState(), port=port, asset_dir=tmp_path).start()
    second.stop()


def test_bind_and_asset_failures_are_isolated_and_bounded(tmp_path):
    for name in ("index.html", "dashboard.css", "dashboard.js"):
        (tmp_path / name).write_text(name)
    state = DashboardState()
    first = DashboardServer(state, port=0, asset_dir=tmp_path).start()
    try:
        failed_state = DashboardState()
        assert start_dashboard(failed_state, first.port) is None
        assert failed_state.snapshot()["bridge"]["dashboard"] == "failed"

        missing_assets = tmp_path / "missing"
        isolated = DashboardServer(state, port=0, asset_dir=missing_assets).start()
        try:
            base = f"http://127.0.0.1:{isolated.port}"
            assert _request(base, "/")[0] == 500
            assert _request(base, "/api/status")[0] == 200
        finally:
            isolated.stop()
    finally:
        first.stop()


def test_capacity_never_exceeds_contract():
    state = DashboardState(capacity=MAX_EVENTS + 50)
    for index in range(MAX_EVENTS + 25):
        state.publish("utterance.started", utterance_id=str(index))
    assert len(state.events(25, limit=MAX_EVENTS)["events"]) == 100
