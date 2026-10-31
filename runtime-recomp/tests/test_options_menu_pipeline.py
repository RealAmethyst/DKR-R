"""No-ROM signature/entry/conflict regressions for the redesigned Options menu hook."""

from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import options_menu_policy as pipeline  # noqa: E402


class OptionsMenuPipelineTests(unittest.TestCase):
    def compose(self, revision, *, word_changed=False, not_entry=False, policy=None):
        function, address, word = pipeline.SITES[revision]
        entry = address - 4 if not_entry else address
        functions = {function: {(entry, 0x400)}}
        sections = [(address, struct.pack(">I", word ^ int(word_changed)))]
        with (
            patch.object(pipeline, "reviewed_revision", return_value=revision),
            patch.object(pipeline, "elf_functions", return_value=functions),
            patch.object(pipeline, "elf_sections", return_value=sections),
        ):
            return pipeline.compose_options_menu(policy or {}, Path("not-a-ROM"), revision)

    def test_both_revisions_and_idempotence(self):
        for revision in pipeline.SITES:
            result = self.compose(revision)
            self.assertEqual(len(result["functionHooks"]), 1)
            self.assertIn("return;", result["functionHooks"][0]["text"])
            self.assertEqual(result, self.compose(revision, policy=result))

    def test_signature_and_entry_fail_closed(self):
        for revision in pipeline.SITES:
            with self.assertRaises(ValueError):
                self.compose(revision, word_changed=True)
            # Returning anywhere but the entry would skip the stack restore.
            with self.assertRaises(ValueError):
                self.compose(revision, not_entry=True)

    def test_occupied_site_is_refused(self):
        _, address, _ = pipeline.SITES["us.v77"]
        other = {"functionHooks": [{"function": "menu_options_loop", "beforeVram": hex(address), "text": "x();"}]}
        with self.assertRaises(ValueError):
            self.compose("us.v77", policy=other)

    def test_unreviewed_image_is_rejected(self):
        with patch.object(pipeline, "reviewed_revision", return_value=None):
            with self.assertRaises(ValueError):
                pipeline.compose_options_menu({}, Path("not-a-ROM"), "us.v77")


if __name__ == "__main__":
    unittest.main()
