"""Checked hook that runs the redesigned Options menu in place of the stock one.

dkr_options_menu_loop (runtime-recomp/src/game/options_menu/options_screen.cpp)
draws and runs the menu and returns non-zero; the stock menu_options_loop
then returns at once. If the native menu cannot run (a symbol or call is
unavailable) it returns zero and the stock loop runs unchanged.
"""

import copy
import struct

from compose_legacy_mod_policy import elf_functions, elf_sections
from legacy_character_presentation_policy import reviewed_revision

# menu_options_loop's first instruction (lui v1,0x8012). The stack frame is
# made later (addiu sp,sp,-72 at +0x1C), so returning here is clean.
SITES = {
    "us.v77": ("menu_options_loop", 0x8008435C, 0x3C038012),
    "us.v80": ("menu_options_loop", 0x80084794, 0x3C038012),
}
TEXT = (
    "extern int dkr_options_menu_loop(uint8_t*, recomp_context*); "
    "if (dkr_options_menu_loop(rdram, ctx)) return;"
)


def compose_options_menu(policy, elf, revision):
    if reviewed_revision(elf) != revision or revision not in SITES:
        raise ValueError("The Options menu hook requires a reviewed retail ELF")
    function, address, expected = SITES[revision]
    matches = elf_functions(elf, (2,)).get(function, set())
    if len(matches) != 1:
        raise ValueError(f"Ambiguous options function: {function}")
    entry, _ = next(iter(matches))
    words = {
        base + i: struct.unpack_from(">I", data, i)[0]
        for base, data in elf_sections(elf)
        for i in range(0, len(data), 4)
    }
    if address != entry or words.get(address) != expected:
        raise ValueError(f"Options menu signature changed: {function}")
    result = copy.deepcopy(policy)
    hooks = result.setdefault("functionHooks", [])
    hook = dict(
        function=function,
        beforeVram=hex(address),
        text=TEXT,
        reason="Redesigned Options menu with DKR-R settings; the stock loop runs if it cannot.",
    )
    existing = [item for item in hooks if int(item["beforeVram"], 0) == address]
    if existing and existing != [hook]:
        raise ValueError(f"Conflicting options menu hook: {function}")
    if not existing:
        hooks.append(hook)
    return result
