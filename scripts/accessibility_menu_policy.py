"""Checked menu speech event hooks; the retail menu code runs unchanged.

Each hook calls dkr_accessibility_menu_event with an event number shared with
runtime-recomp/src/game/accessibility/menu_events.hpp. The native side reads
game state through the named symbol tables, so no address leaves this file.
"""

import copy
import struct

from compose_legacy_mod_policy import elf_functions, elf_sections
from legacy_character_presentation_policy import reviewed_revision

EVENTS = {
    "screen_opened": 1,
    "title_revealed": 2,
    "title_cursor": 3,
}

# Exact retail sites, verified against the decomp (src/menu.c), not a guessed
# delta between revisions.
#   menu_init: the join every path reaches after the screen's own init, before
#     the only return (ori t8,zero,0xD000).
#   menu_title_screen_loop: the SOUND_WHOOSH1 call when the title menu is
#     revealed, and the SOUND_MENU_PICK2 call after gTitleScreenCurrentOption
#     changed.
# The Options screen is the redesigned menu (options_menu_policy.py).
JAL_SOUND_PLAY = 0x0C000741
SITES = {
    "us.v77": [
        ("menu_init", 0x80081590, 0x3418D000, "screen_opened"),
        ("menu_title_screen_loop", 0x80083CA4, JAL_SOUND_PLAY, "title_revealed"),
        ("menu_title_screen_loop", 0x80083FBC, JAL_SOUND_PLAY, "title_cursor"),
    ],
    "us.v80": [
        ("menu_init", 0x800819E0, 0x3418D000, "screen_opened"),
        ("menu_title_screen_loop", 0x800840DC, JAL_SOUND_PLAY, "title_revealed"),
        ("menu_title_screen_loop", 0x800843F4, JAL_SOUND_PLAY, "title_cursor"),
    ],
}


# MIPS branches and jumps: a hook before an instruction in their delay slot
# would run on the wrong path.
def is_branch_or_jump(word):
    opcode = word >> 26
    if opcode in (2, 3, 4, 5, 6, 7, 20, 21, 22, 23):
        return True
    if opcode == 0 and word & 0x3F in (8, 9):
        return True
    return opcode == 1 and (word >> 16) & 0x1F in (0, 1, 2, 3, 16, 17, 18, 19)


def compose_accessibility_menu(policy, elf, revision):
    if reviewed_revision(elf) != revision or revision not in SITES:
        raise ValueError("Menu speech hooks require a reviewed retail ELF")
    symbols = elf_functions(elf, (1, 2))
    words = {
        base + i: struct.unpack_from(">I", data, i)[0]
        for base, data in elf_sections(elf)
        for i in range(0, len(data), 4)
    }
    result = copy.deepcopy(policy)
    hooks = result.setdefault("functionHooks", [])
    for function, address, expected, event in SITES[revision]:
        matches = symbols.get(function, set())
        if len(matches) != 1:
            raise ValueError(f"Ambiguous menu function: {function}")
        entry, size = next(iter(matches))
        if not entry <= address < entry + size or words.get(address) != expected:
            raise ValueError(f"Menu signature changed: {function}")
        if address == entry or is_branch_or_jump(words.get(address - 4, 0)):
            raise ValueError(f"Menu hook would sit in a delay slot: {function}")
        hook = dict(
            function=function,
            beforeVram=hex(address),
            text=(
                "extern void dkr_accessibility_menu_event(uint8_t*, recomp_context*, uint32_t); "
                f"dkr_accessibility_menu_event(rdram, ctx, {EVENTS[event]});"
            ),
            reason="Screen reader speech for menu changes; reads game state only.",
        )
        existing = [item for item in hooks if int(item["beforeVram"], 0) == address]
        if existing and existing != [hook]:
            raise ValueError(f"Conflicting menu speech hook: {function}")
        if not existing:
            hooks.append(hook)
    return result
