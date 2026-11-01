from __future__ import annotations

import pytest

from harness.checker.logic_group import LogicGroup


def _lg(**overrides) -> LogicGroup:
    fields = {
        "name": "libplist binary parse-serialize round-trip",
        "description": "Parse a bplist, serialize it back, compare.",
        "entries": ["plist_from_bin", "plist_to_bin"],
        "core": ["parse_bin_node", "write_object"],
        "project": "libplist",
    }
    fields.update(overrides)
    return LogicGroup(**fields)


def test_requires_name():
    with pytest.raises(ValueError, match="name"):
        _lg(name="")


def test_requires_at_least_one_entry():
    with pytest.raises(ValueError, match="entries"):
        _lg(entries=[])


def test_json_round_trip(tmp_path):
    lg = _lg(danger_scores={"plist_from_bin": 3.5})
    path = tmp_path / "lg_1.json"
    lg.save_json(path)
    assert LogicGroup.load_json(path) == lg


def test_from_dict_fills_optional_fields():
    lg = LogicGroup.from_dict({"name": "n", "description": "d", "entries": ["f"]})
    assert lg.core == []
    assert lg.project == ""
    assert lg.danger_scores == {}


def test_from_dict_copies_lists():
    raw = {"name": "n", "description": "d", "entries": ["f"], "core": ["g"]}
    lg = LogicGroup.from_dict(raw)
    lg.entries.append("h")
    lg.core.append("i")
    assert raw["entries"] == ["f"]
    assert raw["core"] == ["g"]


def test_summary_mentions_counts():
    assert _lg().summary() == (
        "LogicGroup('libplist binary parse-serialize round-trip', entries=2, core=2, project='libplist')"
    )
