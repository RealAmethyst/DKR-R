"""Checked menu speech event hooks; the retail menu code runs unchanged.

Each hook calls dkr_accessibility_menu_event with an event number shared with
runtime-recomp/src/game/accessibility/menu_events.hpp, a value the retail code
holds in a register at the site (such as the controller the event belongs
to), and the addresses of text literals the screen draws without a symbol,
each verified here. The native side reads game state through the named symbol
tables, so no other address leaves this file.
"""

import copy
import struct

from compose_legacy_mod_policy import elf_functions, elf_sections
from legacy_character_presentation_policy import reviewed_revision

EVENTS = {
    "screen_opened": 1,
    "title_revealed": 2,
    "title_cursor": 3,
    "character_joined": 4,
    "character_moved": 5,
    "character_confirmed": 6,
    "character_unconfirmed": 7,
    "character_left": 8,
    "game_select_cursor": 9,
    "track_grid_moved": 10,
    "track_setup_opened": 11,
    "track_setup_changed": 12,
    "track_rules_shown": 13,
    "track_grid_returned": 14,
    "file_slots_ready": 15,
    "file_root": 16,
    "file_copy_source": 17,
    "file_copy_target": 18,
    "file_erase_source": 19,
    "file_confirm": 20,
    "file_slot_source": 21,
    "file_slot_target": 22,
    "file_name_opened": 23,
    "file_name_typed": 24,
    "file_name_deleted": 25,
    "file_name_cursor": 26,
}

# Exact retail sites, verified against the decomp (src/menu.c), not a guessed
# delta between revisions. Research: docs/notes/2026-10-31-menus-into-a-race.md.
#   menu_init: the join every path reaches after the screen's own init, before
#     the only return (ori t8,zero,0xD000).
#   menu_title_screen_loop: the SOUND_WHOOSH1 call when the title menu is
#     revealed, and the SOUND_MENU_PICK2 call after gTitleScreenCurrentOption
#     changed.
#   charselect_new_player: the SOUND_SELECT2 call after a controller joined
#     (s1 is the controller).
#   charselect_move: the move sound after gPlayersCharacterArray[s4] changed.
#   charselect_input: the voice calls after a player confirmed or took a
#     confirmation back (s4 is the controller), and the lui after
#     gNumberOfActivePlayers was lowered for a player leaving with B, before
#     that player's cursor is cleared.
#   charselect_pick: the voice call after a player took a confirmation back at
#     "OK?" (s0 is the controller).
#   menu_game_select_loop: the SOUND_MENU_PICK2 calls after gMenuCurIndex
#     moved down and up.
#   trackmenu_input: the SOUND_MENU_PICK2 call after the grid cursor moved,
#     and the branch after trackmenu_assets(1) opened race setup.
#   func_80092188: the lui after set_current_text loaded the battle rules and
#     after trackmenu_assets(0) went back to the grid; the SOUND_CAR_REV2 call
#     once every player chose a vehicle; and the back, select and change sounds
#     after the stage switch, when the frame's setup state is final.
#   menu_file_select_loop: the store after gSavefileInfo was filled for the
#     open screen, the store after name entry was cancelled, and the store
#     after filename_init opened name entry.
#   fileselect_input_root: the SOUND_MENU_PICK2 call after the cursor moved,
#     and the branches after COPY or ERASE was chosen.
#   fileselect_input_copy and fileselect_input_erase: each sound and branch
#     whose outcome is fixed there (cursor moves, a chosen slot, "OK?", going
#     back, and the branch after the copy or erase was done); each event names
#     what the screen shows next, so state still written in a delay slot is
#     not needed.
#   filename_enter: the sounds after a letter was added and after a letter was
#     removed (DEL, B, and B with the name full), and the SOUND_MENU_PICK2 call
#     before the new carousel index (v0) is stored.
# The Options screen is the redesigned menu (options_menu_policy.py).
JAL_SOUND_PLAY = 0x0C000741
V0, S0, S1, S4 = 2, 16, 17, 20
CHARACTER_OK = ("character_ok",)
TRACK_OK = ("track_ok",)
NAME_TEXT = ("name_characters", "name_space", "name_delete", "name_ok")
# function, address, expected word, event, value register, literals
SITES = {
    "us.v77": [
        ("menu_init", 0x80081590, 0x3418D000, "screen_opened", None, ()),
        ("menu_title_screen_loop", 0x80083CA4, JAL_SOUND_PLAY, "title_revealed", None, ()),
        ("menu_title_screen_loop", 0x80083FBC, JAL_SOUND_PLAY, "title_cursor", None, ()),
        ("charselect_new_player", 0x8008B480, JAL_SOUND_PLAY, "character_joined", S1, ()),
        ("charselect_move", 0x8008C0E0, JAL_SOUND_PLAY, "character_moved", S4, ()),
        ("charselect_input", 0x8008B9B4, JAL_SOUND_PLAY, "character_confirmed", S4, CHARACTER_OK),
        ("charselect_input", 0x8008B848, JAL_SOUND_PLAY, "character_unconfirmed", S4, ()),
        ("charselect_input", 0x8008B880, 0x3C06800E, "character_left", S4, CHARACTER_OK),
        ("charselect_pick", 0x8008B714, JAL_SOUND_PLAY, "character_unconfirmed", S0, ()),
        ("menu_game_select_loop", 0x8008CA78, JAL_SOUND_PLAY, "game_select_cursor", None, ()),
        ("menu_game_select_loop", 0x8008CAA8, JAL_SOUND_PLAY, "game_select_cursor", None, ()),
        ("trackmenu_input", 0x80090E3C, JAL_SOUND_PLAY, "track_grid_moved", None, ()),
        ("trackmenu_input", 0x80090C78, 0x1000000C, "track_setup_opened", None, TRACK_OK),
        ("func_80092188", 0x8009220C, 0x3C028012, "track_rules_shown", None, ()),
        ("func_80092188", 0x800923D4, 0x3C028012, "track_grid_returned", None, ()),
        ("func_80092188", 0x80092730, JAL_SOUND_PLAY, "track_setup_changed", None, TRACK_OK),
        ("func_80092188", 0x80092B28, JAL_SOUND_PLAY, "track_setup_changed", None, TRACK_OK),
        ("func_80092188", 0x80092B40, JAL_SOUND_PLAY, "track_setup_changed", None, TRACK_OK),
        ("func_80092188", 0x80092B60, JAL_SOUND_PLAY, "track_setup_changed", None, TRACK_OK),
        ("menu_file_select_loop", 0x8008DF74, 0xAFA80044, "file_slots_ready", None, ()),
        ("menu_file_select_loop", 0x8008E0AC, 0x240F0044, "file_root", None, ()),
        ("fileselect_input_root", 0x8008D8A0, JAL_SOUND_PLAY, "file_root", None, ()),
        ("fileselect_input_root", 0x8008D74C, 0x10000057, "file_copy_source", None, ()),
        ("fileselect_input_root", 0x8008D788, 0x10000048, "file_erase_source", None, ()),
        ("fileselect_input_copy", 0x8008D93C, JAL_SOUND_PLAY, "file_root", None, ()),
        ("fileselect_input_copy", 0x8008DA28, JAL_SOUND_PLAY, "file_slot_source", None, ()),
        ("fileselect_input_copy", 0x8008D9CC, 0x100000A7, "file_copy_target", None, ()),
        ("fileselect_input_copy", 0x8008DB28, JAL_SOUND_PLAY, "file_slot_target", None, ()),
        ("fileselect_input_copy", 0x8008DA70, 0x1000007E, "file_copy_source", None, ()),
        ("fileselect_input_copy", 0x8008DABC, JAL_SOUND_PLAY, "file_confirm", None, ("file_copy_ok",)),
        ("fileselect_input_copy", 0x8008DC44, 0x10000009, "file_root", None, ()),
        ("fileselect_input_copy", 0x8008DC58, JAL_SOUND_PLAY, "file_copy_target", None, ()),
        ("fileselect_input_erase", 0x8008DCF4, JAL_SOUND_PLAY, "file_root", None, ()),
        ("fileselect_input_erase", 0x8008DD40, JAL_SOUND_PLAY, "file_confirm", None, ("file_erase_ok",)),
        ("fileselect_input_erase", 0x8008DDA8, JAL_SOUND_PLAY, "file_slot_source", None, ()),
        ("fileselect_input_erase", 0x8008DE3C, 0x10000008, "file_root", None, ()),
        ("fileselect_input_erase", 0x8008DE50, JAL_SOUND_PLAY, "file_erase_source", None, ()),
        ("menu_file_select_loop", 0x8008E264, 0xAFA00040, "file_name_opened", None, NAME_TEXT),
        ("filename_enter", 0x80097FD4, JAL_SOUND_PLAY, "file_name_typed", None, NAME_TEXT),
        ("filename_enter", 0x80098040, JAL_SOUND_PLAY, "file_name_deleted", None, NAME_TEXT),
        ("filename_enter", 0x80098104, JAL_SOUND_PLAY, "file_name_deleted", None, NAME_TEXT),
        ("filename_enter", 0x800981B8, JAL_SOUND_PLAY, "file_name_deleted", None, NAME_TEXT),
        ("filename_enter", 0x80098154, JAL_SOUND_PLAY, "file_name_cursor", V0, NAME_TEXT),
    ],
    "us.v80": [
        ("menu_init", 0x800819E0, 0x3418D000, "screen_opened", None, ()),
        ("menu_title_screen_loop", 0x800840DC, JAL_SOUND_PLAY, "title_revealed", None, ()),
        ("menu_title_screen_loop", 0x800843F4, JAL_SOUND_PLAY, "title_cursor", None, ()),
        ("charselect_new_player", 0x8008B938, JAL_SOUND_PLAY, "character_joined", S1, ()),
        ("charselect_move", 0x8008C598, JAL_SOUND_PLAY, "character_moved", S4, ()),
        ("charselect_input", 0x8008BE6C, JAL_SOUND_PLAY, "character_confirmed", S4, CHARACTER_OK),
        ("charselect_input", 0x8008BD00, JAL_SOUND_PLAY, "character_unconfirmed", S4, ()),
        ("charselect_input", 0x8008BD38, 0x3C06800E, "character_left", S4, CHARACTER_OK),
        ("charselect_pick", 0x8008BBCC, JAL_SOUND_PLAY, "character_unconfirmed", S0, ()),
        ("menu_game_select_loop", 0x8008CF30, JAL_SOUND_PLAY, "game_select_cursor", None, ()),
        ("menu_game_select_loop", 0x8008CF60, JAL_SOUND_PLAY, "game_select_cursor", None, ()),
        ("trackmenu_input", 0x800912F4, JAL_SOUND_PLAY, "track_grid_moved", None, ()),
        ("trackmenu_input", 0x80091130, 0x1000000C, "track_setup_opened", None, TRACK_OK),
        ("func_80092188", 0x80092728, 0x3C028012, "track_rules_shown", None, ()),
        ("func_80092188", 0x800928F0, 0x3C028012, "track_grid_returned", None, ()),
        ("func_80092188", 0x80092C80, JAL_SOUND_PLAY, "track_setup_changed", None, TRACK_OK),
        ("func_80092188", 0x80093070, JAL_SOUND_PLAY, "track_setup_changed", None, TRACK_OK),
        ("func_80092188", 0x80093088, JAL_SOUND_PLAY, "track_setup_changed", None, TRACK_OK),
        ("func_80092188", 0x800930A8, JAL_SOUND_PLAY, "track_setup_changed", None, TRACK_OK),
        ("menu_file_select_loop", 0x8008E42C, 0xAFA80044, "file_slots_ready", None, ()),
        ("menu_file_select_loop", 0x8008E564, 0x240F0044, "file_root", None, ()),
        ("fileselect_input_root", 0x8008DD58, JAL_SOUND_PLAY, "file_root", None, ()),
        ("fileselect_input_root", 0x8008DC04, 0x10000057, "file_copy_source", None, ()),
        ("fileselect_input_root", 0x8008DC40, 0x10000048, "file_erase_source", None, ()),
        ("fileselect_input_copy", 0x8008DDF4, JAL_SOUND_PLAY, "file_root", None, ()),
        ("fileselect_input_copy", 0x8008DEE0, JAL_SOUND_PLAY, "file_slot_source", None, ()),
        ("fileselect_input_copy", 0x8008DE84, 0x100000A7, "file_copy_target", None, ()),
        ("fileselect_input_copy", 0x8008DFE0, JAL_SOUND_PLAY, "file_slot_target", None, ()),
        ("fileselect_input_copy", 0x8008DF28, 0x1000007E, "file_copy_source", None, ()),
        ("fileselect_input_copy", 0x8008DF74, JAL_SOUND_PLAY, "file_confirm", None, ("file_copy_ok",)),
        ("fileselect_input_copy", 0x8008E0FC, 0x10000009, "file_root", None, ()),
        ("fileselect_input_copy", 0x8008E110, JAL_SOUND_PLAY, "file_copy_target", None, ()),
        ("fileselect_input_erase", 0x8008E1AC, JAL_SOUND_PLAY, "file_root", None, ()),
        ("fileselect_input_erase", 0x8008E1F8, JAL_SOUND_PLAY, "file_confirm", None, ("file_erase_ok",)),
        ("fileselect_input_erase", 0x8008E260, JAL_SOUND_PLAY, "file_slot_source", None, ()),
        ("fileselect_input_erase", 0x8008E2F4, 0x10000008, "file_root", None, ()),
        ("fileselect_input_erase", 0x8008E308, JAL_SOUND_PLAY, "file_erase_source", None, ()),
        ("menu_file_select_loop", 0x8008E71C, 0xAFA00040, "file_name_opened", None, NAME_TEXT),
        ("filename_enter", 0x80098510, JAL_SOUND_PLAY, "file_name_typed", None, NAME_TEXT),
        ("filename_enter", 0x8009857C, JAL_SOUND_PLAY, "file_name_deleted", None, NAME_TEXT),
        ("filename_enter", 0x80098640, JAL_SOUND_PLAY, "file_name_deleted", None, NAME_TEXT),
        ("filename_enter", 0x800986F4, JAL_SOUND_PLAY, "file_name_deleted", None, NAME_TEXT),
        ("filename_enter", 0x80098690, JAL_SOUND_PLAY, "file_name_cursor", V0, NAME_TEXT),
    ],
}

# Text the screens draw from literals with no symbol: the lui/addiu pair in
# the drawing function that builds the address, and the exact bytes there.
#   charselect_render_text: "OK?" once every joined player is ready.
#   trackmenu_setup_render: "OK?" before a race starts.
#   fileselect_render: "OK?" before copying, and before erasing.
#   filename_render: the name entry characters (gFileNameValidChars, which
#     has no symbol size) and the "SP", "DEL" and "OK" entries.
NAME_CHARACTERS = b"ABCDEFGHIJKLMNOPQRSTUVWXYZ.?    "
LITERALS = {
    "us.v77": {
        "character_ok": ("charselect_render_text", 0x8008B310, 0x8008B31C, b"OK?\0"),
        "track_ok": ("trackmenu_setup_render", 0x80092134, 0x80092140, b"OK?\0"),
        "file_copy_ok": ("fileselect_render", 0x8008D464, 0x8008D470, b"OK?\0"),
        "file_erase_ok": ("fileselect_render", 0x8008D4DC, 0x8008D4E8, b"OK?\0"),
        "name_characters": ("filename_render", 0x80097A64, 0x80097A88, NAME_CHARACTERS),
        "name_space": ("filename_render", 0x80097B84, 0x80097B90, b"SP\0"),
        "name_delete": ("filename_render", 0x80097BB4, 0x80097BC0, b"DEL\0"),
        "name_ok": ("filename_render", 0x80097BDC, 0x80097BE8, b"OK\0"),
    },
    "us.v80": {
        "character_ok": ("charselect_render_text", 0x8008B7C8, 0x8008B7D4, b"OK?\0"),
        "track_ok": ("trackmenu_setup_render", 0x80092650, 0x8009265C, b"OK?\0"),
        "file_copy_ok": ("fileselect_render", 0x8008D91C, 0x8008D928, b"OK?\0"),
        "file_erase_ok": ("fileselect_render", 0x8008D994, 0x8008D9A0, b"OK?\0"),
        "name_characters": ("filename_render", 0x80097FA0, 0x80097FC4, NAME_CHARACTERS),
        "name_space": ("filename_render", 0x800980C0, 0x800980CC, b"SP\0"),
        "name_delete": ("filename_render", 0x800980F0, 0x800980FC, b"DEL\0"),
        "name_ok": ("filename_render", 0x80098118, 0x80098124, b"OK\0"),
    },
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


def function_bounds(symbols, function):
    matches = symbols.get(function, set())
    if len(matches) != 1:
        raise ValueError(f"Ambiguous menu function: {function}")
    entry, size = next(iter(matches))
    return entry, entry + size


def literal_address(symbols, sections, words, spec):
    function, high_at, low_at, expected = spec
    entry, end = function_bounds(symbols, function)
    if not (entry <= high_at < end and entry <= low_at < end):
        raise ValueError(f"Menu literal outside its function: {function}")
    high, low = words.get(high_at, 0), words.get(low_at, 0)
    register = (high >> 16) & 0x1F
    # lui reg,hi ... addiu reg,reg,lo
    if high >> 21 != 0x0F << 5 or low >> 26 != 0x09 or (low >> 21) & 0x1F != register or (low >> 16) & 0x1F != register:
        raise ValueError(f"Menu literal signature changed: {function}")
    address = ((high & 0xFFFF) << 16) + struct.unpack(">h", struct.pack(">H", low & 0xFFFF))[0]
    for base, data in sections:
        if base <= address and address + len(expected) <= base + len(data):
            if data[address - base : address - base + len(expected)] == expected:
                return address
    raise ValueError(f"Menu literal text changed: {function}")


def hook_text(event, register, addresses):
    value = f"(uint32_t)ctx->r{register}" if register is not None else "0"
    declaration = (
        "extern void dkr_accessibility_menu_event(uint8_t*, recomp_context*, uint32_t, uint32_t, "
        "const uint32_t*, uint32_t);"
    )
    if not addresses:
        return f"{declaration} dkr_accessibility_menu_event(rdram, ctx, {EVENTS[event]}, {value}, 0, 0);"
    table = ", ".join(f"{address:#010x}u" for address in addresses)
    return (
        f"{{ {declaration} static const uint32_t literals[] = {{{table}}}; "
        f"dkr_accessibility_menu_event(rdram, ctx, {EVENTS[event]}, {value}, literals, {len(addresses)}); }}"
    )


def compose_accessibility_menu(policy, elf, revision):
    if reviewed_revision(elf) != revision or revision not in SITES:
        raise ValueError("Menu speech hooks require a reviewed retail ELF")
    symbols = elf_functions(elf, (1, 2))
    sections = elf_sections(elf)
    words = {base + i: struct.unpack_from(">I", data, i)[0] for base, data in sections for i in range(0, len(data), 4)}
    literals = {name: literal_address(symbols, sections, words, spec) for name, spec in LITERALS[revision].items()}
    result = copy.deepcopy(policy)
    hooks = result.setdefault("functionHooks", [])
    for function, address, expected, event, register, names in SITES[revision]:
        entry, end = function_bounds(symbols, function)
        if not entry <= address < end or words.get(address) != expected:
            raise ValueError(f"Menu signature changed: {function}")
        if address == entry or is_branch_or_jump(words.get(address - 4, 0)):
            raise ValueError(f"Menu hook would sit in a delay slot: {function}")
        hook = dict(
            function=function,
            beforeVram=hex(address),
            text=hook_text(event, register, [literals[name] for name in names]),
            reason="Screen reader speech for menu changes; reads game state only.",
        )
        existing = [item for item in hooks if int(item["beforeVram"], 0) == address]
        if existing and existing != [hook]:
            raise ValueError(f"Conflicting menu speech hook: {function}")
        if not existing:
            hooks.append(hook)
    return result
