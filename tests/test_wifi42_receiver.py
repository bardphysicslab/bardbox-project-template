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
