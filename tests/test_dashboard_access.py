import base64
from unittest.mock import patch

import pytest
from fastapi import FastAPI
from fastapi.testclient import TestClient
from software.app.dashboard_access import DashboardAccess

ENV = {'BARDBOX_REQUIRE_DASHBOARD_AUTH': '1', 'BARDBOX_DASHBOARD_ORIGIN': 'https://rkc.example.edu',
       'BARDBOX_ADMIN_USER': 'admin', 'BARDBOX_ADMIN_PASSWORD': 'admin-test-password',
       'BARDBOX_VIEWER_USER': 'viewer', 'BARDBOX_VIEWER_PASSWORD': 'viewer-test-password'}

@pytest.fixture
def client():
    app = FastAPI()
    app.add_middleware(DashboardAccess, config_prefix="BARDBOX", disabled_paths=("/twilio/sms",))
    @app.api_route('/{path:path}', methods=['GET', 'POST'])
    def endpoint(path: str):
        return {'reached': path}
    with patch.dict('os.environ', ENV):
        yield TestClient(app, base_url=ENV['BARDBOX_DASHBOARD_ORIGIN'])

@pytest.mark.parametrize('path', ['/', '/readings/latest', '/admin', '/logs', '/drivers', '/docs', '/alerts/set-arm/x'])
def test_anonymous_denied(client, path):
    assert client.get(path).status_code == 401

@pytest.mark.parametrize('path', ['/', '/time', '/app/info', '/readings/latest', '/static/example.css'])
def test_viewer_reads(client, path):
    response = client.get(path, auth=('viewer', ENV['BARDBOX_VIEWER_PASSWORD']))
    assert response.status_code == 200
    assert response.headers['cache-control'] == 'no-store'
    assert response.headers['x-frame-options'] == 'DENY'

@pytest.mark.parametrize('path', ['/admin', '/logs', '/drivers', '/app/health', '/docs', '/openapi.json'])
def test_viewer_cannot_read_admin_data(client, path):
    assert client.get(path, auth=('viewer', ENV['BARDBOX_VIEWER_PASSWORD'])).status_code == 403

@pytest.mark.parametrize('path', ['/alerts/set-arm/x', '/alerts/set-door-alarm/x', '/admin/freezers/x/settings', '/alerts/test'])
def test_viewer_cannot_write_even_with_csrf_headers(client, path):
    assert client.post(path, auth=('viewer', ENV['BARDBOX_VIEWER_PASSWORD']), headers={
        'Origin': ENV['BARDBOX_DASHBOARD_ORIGIN'], 'X-Bardbox-Request': '1'}).status_code == 403

@pytest.mark.parametrize('headers', [{}, {'Origin': 'https://evil.example', 'X-Bardbox-Request': '1'},
                                    {'Origin': ENV['BARDBOX_DASHBOARD_ORIGIN']}])
def test_admin_cross_origin_write_denied(client, headers):
    assert client.post('/alerts/set-arm/x', auth=('admin', ENV['BARDBOX_ADMIN_PASSWORD']), headers=headers).status_code == 403

def test_admin_same_origin_write(client):
    assert client.post('/alerts/set-arm/x', auth=('admin', ENV['BARDBOX_ADMIN_PASSWORD']), headers={
        'Origin': ENV['BARDBOX_DASHBOARD_ORIGIN'], 'X-Bardbox-Request': '1'}).status_code == 200

@pytest.mark.parametrize('header', ['Basic !!!', 'Basic ' + base64.b64encode(b'no-colon').decode(), 'Bearer anything', 'Basic '+ 'a'*3000])
def test_malformed_auth(client, header):
    assert client.get('/', headers={'Authorization': header}).status_code == 401

def test_tls_and_host_required(client):
    assert client.get('http://rkc.example.edu/').status_code == 403
    assert client.get('https://wrong.example.edu/').status_code == 403
    assert client.get('http://rkc.example.edu/', headers={'X-Forwarded-Proto': 'https'}).status_code == 403

def test_health_and_webhook(client):
    assert client.get('http://127.0.0.1/health').status_code == 200
    assert client.post('/twilio/sms', auth=('admin', ENV['BARDBOX_ADMIN_PASSWORD'])).status_code == 403

@pytest.mark.parametrize('overrides', [ {'BARDBOX_ADMIN_PASSWORD': ''}, {'BARDBOX_VIEWER_PASSWORD': ''},
    {'BARDBOX_REQUIRE_DASHBOARD_AUTH': 'typo'}, {'BARDBOX_DASHBOARD_ORIGIN': 'http://rkc.example.edu'},
    {'BARDBOX_DASHBOARD_ORIGIN': 'https://rkc.example.edu/'}, {'BARDBOX_VIEWER_USER': 'admin'}])
def test_bad_config_fails_closed(client, overrides):
    with patch.dict('os.environ', overrides):
        assert client.get('/').status_code == 503

def test_legacy_mode_unchanged(client):
    with patch.dict('os.environ', {'BARDBOX_REQUIRE_DASHBOARD_AUTH': '0'}):
        assert client.post('http://localhost/alerts/set-arm/x').status_code == 200

@pytest.mark.parametrize('origin', ['https://rkc.example.edu:bad', 'https://rkc.example.edu:99999'])
def test_invalid_origin_port(client, origin):
    with patch.dict('os.environ', {'BARDBOX_DASHBOARD_ORIGIN': origin}):
        assert client.get('/').status_code == 503

def test_non_ascii_configuration_rejected(client):
    with patch.dict('os.environ', {'BARDBOX_ADMIN_PASSWORD': 'non-ascii-é'}):
        assert client.get('/').status_code == 503

def test_duplicate_authorization_denied(client):
    assert client.get('/', headers=[('Authorization', 'Basic aaa'), ('Authorization', 'Basic bbb')]).status_code == 400
