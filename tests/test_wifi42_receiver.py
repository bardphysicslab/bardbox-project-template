"""The synthetic receiver is isolated from application ingestion/archive."""
from scripts.wifi42_receiver import is_wifi42


def sample(value=42):
    return {"format": "bardbox-delivery-candidate", "channels": [
        {"sensor": "demo", "measurement": "answer", "unit": "1",
         "status": "valid", "value": value}]}


def test_only_valid_synthetic_42_is_accepted():
    assert is_wifi42(sample())
    assert is_wifi42(sample(42.0))
    for value in (None, True, "42", 0, 41, float("inf"), float("nan"), 10**1000):
        assert not is_wifi42(sample(value))
    for value in (None, [], {}, {"format": "normalized", "channels": []}):
        assert not is_wifi42(value)
    bad = sample()
    bad["channels"][0]["status"] = "missing"
    assert not is_wifi42(bad)
    bad = sample()
    bad["channels"].append(bad["channels"][0])
    assert not is_wifi42(bad)


def test_view_tracks_only_authenticated_valid_uploads():
    import http.client
    import json
    from http.server import ThreadingHTTPServer
    from threading import Thread
    from scripts.wifi42_receiver import DemoState, Handler

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    server.demo_token = 'private-test-token'
    server.demo_state = DemoState()
    thread = Thread(target=server.serve_forever, daemon=True)
    thread.start()

    def request(method, path='/demo/wifi42', payload=None, token=None):
        client = http.client.HTTPConnection(*server.server_address, timeout=3)
        headers = {'Content-Type': 'application/json'}
        if token:
            headers['Authorization'] = 'Bearer ' + token
        try:
            client.request(method, path, json.dumps(payload) if payload else None, headers)
            response = client.getresponse()
            return response.status, dict(response.getheaders()), response.read().decode()
        finally:
            client.close()

    try:
        code, headers, page = request('GET')
        assert code == 200 and 'No upload received yet' in page
        assert headers['Cache-Control'] == 'no-store'
        assert 'default-src' in headers['Content-Security-Policy']
        assert request('POST', payload=sample(), token='wrong')[0] == 403
        assert request('POST', payload=sample(41), token=server.demo_token)[0] == 422
        assert server.demo_state.snapshot() == (0, None, None)
        status, _, body = request('POST', payload=sample(), token=server.demo_token)
        assert status == 204 and body == ''
        assert request('POST', payload=sample(), token=server.demo_token)[0] == 204
        code, _, page = request('GET')
        assert code == 200 and '<div class="value">42</div>' in page
        assert '<dd>2</dd>' in page and 'UTC' in page
        assert server.demo_token not in page and 'Authorization' not in page
        assert request('GET', '/missing')[0] == 404
        assert request('GET', '/')[0] == 200
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=3)


def counter(value=7, utc="2026-10-21T00:00:15.000Z", status="ntp"):
    return {"format": "bardbox-delivery-candidate", "recorded_utc": utc, "recorded_utc_status": status,
            "channels": [{"sensor": "demo", "measurement": "counter", "unit": "1", "type": "integer",
                          "status": "valid", "value": value}]}


def test_counter_and_recorded_utc_are_validated():
    from scripts.wifi42_receiver import demo_reading, recorded_time
    assert demo_reading(counter()) == ("counter", 7)
    assert recorded_time(counter()) == ("ntp", "2026-10-21T00:00:15.000Z")
    assert recorded_time(counter(utc=None, status="unsynced")) == ("unsynced", None)
    assert recorded_time(counter(utc=None, status="disabled")) == ("disabled", None)
    assert recorded_time(sample()) == ("not_recorded", None)  # old queued 42 records
    for value in (-1, True, 7.0, "7", None, 2**63):
        assert demo_reading(counter(value)) is None
    for utc, status in (("2026-10-21 00:00:15", "ntp"), (None, "ntp"), ("2026-10-21T00:00:15.000Z", "unsynced"),
                        ("<script>", "ntp"), (None, "later"), ("2026-02-30T00:00:00.000Z", "ntp"),
                        ("2026-13-01T00:00:00.000Z", "ntp"), ("2026-10-21T24:00:00.000Z", "ntp"),
                        ("2025-02-29T00:00:00.000Z", "ntp")):
        assert recorded_time(counter(utc=utc, status=status)) is None


def test_view_shows_counter_with_recorded_utc_and_arrival():
    from scripts.wifi42_receiver import viewing_page
    page = viewing_page(3, "2026-10-21 00:00:20 UTC", ("counter", 7, "ntp", "2026-10-21T00:00:15.000Z")).decode()
    assert '<div class="value">7</div>' in page and "The counter" in page
    assert "2026-10-21T00:00:15.000Z" in page and "Arrived at this Mac" in page
    page = viewing_page(1, "x", ("counter", 1, "unsynced", None)).decode()
    assert "had no synchronized clock" in page
