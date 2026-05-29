import unittest

from raspi import main


class NodeUidTests(unittest.TestCase):
    def setUp(self):
        self._uid_aliases = dict(main.UID_ALIASES)
        self._driver_uids = dict(main.DRIVER_UIDS)

    def tearDown(self):
        main.UID_ALIASES.clear()
        main.UID_ALIASES.update(self._uid_aliases)
        main.DRIVER_UIDS.clear()
        main.DRIVER_UIDS.update(self._driver_uids)

    def test_valid_new_uid_examples(self):
        for uid in (
            "bb-gol-air-001",
            "bb-gol-air-002",
            "bb-rkc-frz-001",
            "bb-rkc-frz-014",
            "bb-sol-sol-001",
            "bb-csh-air-001",
            "bb-heg-snd-001",
            "bb-prj-air-001",
        ):
            self.assertTrue(main.is_valid_new_node_uid(uid), uid)
            self.assertTrue(main.is_valid_node_uid(uid), uid)

    def test_malformed_new_ids_are_rejected(self):
        for uid in (
            "bb-golab-air-001",
            "bb-gol-air-1",
            "bb-GOL-air-001",
            "bb-gol-air-0001",
            "gol-air-001",
        ):
            self.assertFalse(main.is_valid_new_node_uid(uid), uid)
            self.assertFalse(main.is_valid_node_uid(uid, allow_legacy=False), uid)

    def test_legacy_ids_are_supported_but_not_new_standard(self):
        self.assertFalse(main.is_valid_new_node_uid("bb-0001"))
        self.assertTrue(main.is_valid_node_uid("bb-0001", allow_legacy=True))

    def test_driver_config_registers_legacy_uid_aliases(self):
        drivers = main.load_drivers(
            {
                "drivers": [
                    {
                        "driver": "example",
                        "uid": "bb-gol-air-001",
                        "legacy_uids": ["bb-0001", "rkc-01", "spn1-0001"],
                        "config": {"reported_uid": "bb-0001"},
                    }
                ]
            }
        )
        self.assertEqual(main.canonical_uid("bb-0001"), "bb-gol-air-001")
        self.assertEqual(main.canonical_uid("rkc-01"), "bb-gol-air-001")
        self.assertEqual(main.canonical_uid("spn1-0001"), "bb-gol-air-001")
        self.assertEqual(main.driver_uid(drivers[0]), "bb-gol-air-001")

    def test_top_level_uid_aliases_are_supported(self):
        main.load_drivers(
            {
                "uid_aliases": {
                    "bb-0001": "bb-gol-air-001",
                    "rkc-01": "bb-rkc-frz-001",
                    "spn1-0001": "bb-sol-sol-001",
                },
                "drivers": [],
            }
        )
        self.assertEqual(main.canonical_uid("bb-0001"), "bb-gol-air-001")
        self.assertEqual(main.canonical_uid("rkc-01"), "bb-rkc-frz-001")
        self.assertEqual(main.canonical_uid("spn1-0001"), "bb-sol-sol-001")


if __name__ == "__main__":
    unittest.main()
