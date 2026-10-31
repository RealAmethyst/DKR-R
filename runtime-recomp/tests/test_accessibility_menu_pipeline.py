"""No-ROM signature/bounds/delay-slot/conflict regressions for menu speech hooks."""

from pathlib import Path
import re
import struct
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import accessibility_menu_policy as pipeline  # noqa: E402

NOP = 0x00000000
BRANCH = 0x10000004  # b +4


class AccessibilityMenuPipelineTests(unittest.TestCase):
    def compose(self, revision, *, opcode_changed=False, outside=False, before=NOP, policy=None):
        sites = pipeline.SITES[revision]
        symbols = {}
        for name, address, _, _ in sites:
            low, high = symbols.get(name, (address, address))
            symbols[name] = (min(low, address), max(high, address))
        functions = {
            name: {(high + 8, 16) if outside else (low - 8, high + 16 - (low - 8))}
            for name, (low, high) in symbols.items()
        }
        sections = [
            (address - 4, struct.pack(">II", before, word ^ int(opcode_changed)))
            for _, address, word, _ in sites
        ]
        with (
            patch.object(pipeline, "reviewed_revision", return_value=revision),
            patch.object(pipeline, "elf_functions", return_value=functions),
            patch.object(pipeline, "elf_sections", return_value=sections),
        ):
            return pipeline.compose_accessibility_menu(policy or {}, Path("not-a-ROM"), revision)

    def test_both_revisions_and_idempotence(self):
        for revision in pipeline.SITES:
            result = self.compose(revision)
            self.assertEqual(len(result["functionHooks"]), len(pipeline.SITES[revision]))
            self.assertEqual(result, self.compose(revision, policy=result))

    def test_signature_bounds_and_delay_slots_fail_closed(self):
        for revision in pipeline.SITES:
            with self.assertRaises(ValueError):
                self.compose(revision, opcode_changed=True)
            with self.assertRaises(ValueError):
                self.compose(revision, outside=True)
            with self.assertRaises(ValueError):
                self.compose(revision, before=BRANCH)

    def test_occupied_site_is_refused(self):
        address = pipeline.SITES["us.v77"][0][1]
        other = {"functionHooks": [{"function": "menu_init", "beforeVram": hex(address), "text": "other();"}]}
        with self.assertRaises(ValueError):
            self.compose("us.v77", policy=other)

    def test_unreviewed_image_is_rejected(self):
        with patch.object(pipeline, "reviewed_revision", return_value=None):
            with self.assertRaises(ValueError):
                pipeline.compose_accessibility_menu({}, Path("not-a-ROM"), "us.v77")

    def test_event_numbers_match_the_native_enum(self):
        header = (ROOT / "runtime-recomp/src/game/accessibility/menu_events.hpp").read_text(encoding="utf-8")
        body = re.search(r"enum class MenuEvent[^{]*\{(.*?)\};", header, re.S).group(1)
        native = {re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower(): int(value)
                  for name, value in re.findall(r"(\w+)\s*=\s*(\d+)", body)}
        self.assertEqual(native, pipeline.EVENTS)


if __name__ == "__main__":
    unittest.main()
