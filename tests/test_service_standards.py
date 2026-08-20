import asyncio
import json
import unittest
from pathlib import Path

from raspi import main


ROOT = Path(__file__).resolve().parents[1]


class ServiceStandardsTests(unittest.TestCase):
    def test_health_is_exact_and_side_effect_free(self):
        self.assertEqual(asyncio.run(main.health()), {"status": "ok"})

    def test_health_route_and_reference_pages_are_registered(self):
        paths = {route.path for route in main.app.routes}
        self.assertIn("/health", paths)
        self.assertIn("/", paths)
        self.assertIn("/monitor", paths)

    def test_systemd_service_has_required_process_recovery(self):
        service = (ROOT / "deploy" / "bardbox-app.service").read_text(encoding="utf-8")
        self.assertIn("Restart=always", service)
        self.assertIn("RestartSec=5", service)

    def test_watchdog_runs_every_minute_and_requires_three_failures(self):
        timer = (ROOT / "deploy" / "bardbox-watchdog.timer").read_text(encoding="utf-8")
        service = (ROOT / "deploy" / "bardbox-watchdog.service").read_text(encoding="utf-8")
        self.assertIn("OnUnitActiveSec=1min", timer)
        self.assertIn("BARDBOX_FAILURE_LIMIT=3", service)
        self.assertIn("/health", service)

    def test_manual_healthcheck_uses_lightweight_endpoint(self):
        script = (ROOT / "scripts" / "healthcheck.sh").read_text(encoding="utf-8")
        self.assertIn('${BASE_URL}/health', script)
        self.assertNotIn('${BASE_URL}/app/health', script)

    def test_example_config_has_empty_data_api_token(self):
        config = json.loads((ROOT / "raspi" / "config" / "app_config.example.json").read_text(encoding="utf-8"))
        self.assertEqual(config["data_api"], {"token": ""})

    def test_reference_ui_demonstrates_standard_components(self):
        dashboard = (ROOT / "raspi" / "templates" / "index.html").read_text(encoding="utf-8")
        landing = (ROOT / "raspi" / "templates" / "landing.html").read_text(encoding="utf-8")
        stylesheet = (ROOT / "raspi" / "static" / "bardbox.css").read_text(encoding="utf-8")
        self.assertIn('class="landing-grid"', landing)
        self.assertIn('class="node-grid"', dashboard)
        self.assertIn('<details class="foldout">', dashboard)
        self.assertIn('<table>', dashboard)
        self.assertIn('class="chart-panel"', dashboard)
        self.assertIn("@media (max-width: 600px)", stylesheet)


if __name__ == "__main__":
    unittest.main()
