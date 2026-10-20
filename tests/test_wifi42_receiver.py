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
        assert server.demo_state.snapshot() == (0, None)
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
