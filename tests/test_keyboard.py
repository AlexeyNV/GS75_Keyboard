"""Production C keymap, temporal suppression, runtime and KEYS regressions."""
import ctypes as C
import itertools
import json
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
DLL = C.CDLL(str(Path(sys.argv.pop(1)).resolve()))
DATA = json.loads((ROOT / "tools/matrix_mapper/gs75_matrix_20260921_172228.json").read_text(encoding="utf-8"))
FIXTURES = json.loads((ROOT / "tools/matrix_mapper/ghosting_results/gs75_ghosting_20260921_174434.json").read_text(encoding="utf-8"))
DRIVE = list(range(17, 25))
SENSE = list(range(1, 17)) + [25, 26]
Rows = C.c_uint32 * 8
Keys = C.c_uint32 * 4
LABELS = list(DATA["singles"])
IDS = {label: i + 1 for i, label in enumerate(LABELS)}


class Ghost(C.Structure):
    _fields_ = [("ambiguous", Rows), ("row_pair_count", C.c_uint32),
                ("rectangle_count", C.c_uint32)]


class State(C.Structure):
    _fields_ = [("pressed", Keys), ("suppressed", Keys), ("ambiguous", Keys)]


class Snapshot(C.Structure):
    _fields_ = [("keys", State), ("scan_count", C.c_uint32), ("timestamp_ms", C.c_uint32)]


DLL.Keymap_At.argtypes = [C.c_uint32, C.c_uint32]
DLL.Keymap_At.restype = C.c_int
for name in ("Keymap_Name", "Keymap_Label"):
    getattr(DLL, name).argtypes = [C.c_int]
    getattr(DLL, name).restype = C.c_char_p
DLL.Keyboard_Init.argtypes = [C.POINTER(State)]
DLL.Keyboard_Update.argtypes = [C.POINTER(State), C.POINTER(C.c_uint32), C.POINTER(C.c_uint32)]
DLL.Keyboard_KeyIsSet.argtypes = [C.POINTER(C.c_uint32), C.c_int]
DLL.Keyboard_KeyIsSet.restype = C.c_uint8
DLL.AntiGhost_Analyze.argtypes = [C.POINTER(C.c_uint32), C.POINTER(Ghost)]
DLL.KeyboardRuntime_GetSnapshot.argtypes = [C.POINTER(Snapshot)]
DLL.Test_LogicalSample.argtypes = [C.POINTER(C.c_uint32)]
DLL.Test_PlanLogicalDuringTx.argtypes = [C.POINTER(C.c_uint32), C.c_uint32]
DLL.Test_Feed.argtypes = [C.c_char_p]
DLL.Test_QueueDuringTx.argtypes = [C.c_char_p]
DLL.Test_Output.restype = C.c_char_p
DLL.Test_HarnessErrors.restype = C.c_uint32
for name in ("Keyboard_Init", "Keyboard_Update", "AntiGhost_Analyze",
             "KeyboardRuntime_GetSnapshot", "Test_LogicalSample", "Test_PlanLogicalDuringTx",
             "Test_Reset", "Test_Feed", "Test_QueueDuringTx", "Test_ClearOutput", "Test_Drain"):
    getattr(DLL, name).restype = None


def project(edges):
    rows = Rows()
    for edge in edges:
        src, dst = map(int, re.findall(r"FPC(\d+)", edge))
        if src in DRIVE and dst in SENSE:
            rows[DRIVE.index(src)] |= 1 << SENSE.index(dst)
    return rows


COORDS = {}
for label, record in DATA["singles"].items():
    for row, bits in enumerate(project(record["active_edges"])):
        if bits:
            COORDS[label] = (row, bits.bit_length() - 1)
AT = {coord: label for label, coord in COORDS.items()}


def electrical(labels):
    """Independent physical switch graph -> measured drive/sense closure."""
    adj = {}
    for label in labels:
        r, c = COORDS[label]
        a, b = ("r", r), ("c", c)
        adj.setdefault(a, set()).add(b)
        adj.setdefault(b, set()).add(a)
    rows, seen = Rows(), set()
    for start in adj:
        if start in seen:
            continue
        comp, pending = set(), [start]
        while pending:
            node = pending.pop()
            if node not in comp:
                comp.add(node)
                pending.extend(adj[node] - comp)
        seen |= comp
        for kind, r in comp:
            if kind == "r":
                for kind2, c in comp:
                    if kind2 == "c":
                        rows[r] |= 1 << c
    return rows


def rectangle(r1, r2, c1, c2):
    rows = Rows()
    rows[r1] = rows[r2] = (1 << c1) | (1 << c2)
    return rows


def labels_in(bits):
    return {label for label, key in IDS.items()
            if bits[(key - 1) // 32] & (1 << ((key - 1) % 32))}


def mapped(rows):
    return {label for label, (r, c) in COORDS.items() if rows[r] & (1 << c)}


def update(state, rows):
    original = list(rows)
    ghost = Ghost()
    DLL.AntiGhost_Analyze(rows, C.byref(ghost))  # Full matrix, including holes.
    DLL.Keyboard_Update(C.byref(state), rows, ghost.ambiguous)
    assert list(rows) == original
    return ghost


class LogicalTests(unittest.TestCase):
    def fresh(self):
        result = State()
        DLL.Keyboard_Init(C.byref(result))
        return result

    def test_exact_static_map_and_all_102_singles(self):
        seen = set()
        for r in range(8):
            for c in range(18):
                key = DLL.Keymap_At(r, c)
                label = AT.get((r, c))
                self.assertEqual(key, IDS[label] if label else 0)
                if label:
                    self.assertEqual(DLL.Keymap_Label(key).decode(), label)
                    self.assertRegex(DLL.Keymap_Name(key).decode(), r"^KEY_[A-Z0-9_]+$")
                    self.assertNotIn(key, seen)
                    seen.add(key)
        self.assertEqual(len(seen), 102)
        for label in LABELS:
            with self.subTest(label=label):
                state = self.fresh()
                update(state, electrical([label]))
                self.assertEqual(labels_in(state.pressed), {label})
                self.assertEqual(labels_in(state.suppressed), set())
                update(state, Rows())
                self.assertEqual(labels_in(state.pressed), set())
        for r, c in ((8, 0), (0, 18), (0xFFFFFFFF, 0)):
            self.assertEqual(DLL.Keymap_At(r, c), 0)
        for invalid in (0, -1, 103, 9999):
            self.assertFalse(DLL.Keyboard_KeyIsSet(Keys(*([0xFFFFFFFF] * 4)), invalid))

    def test_all_recorded_ordinary_combinations(self):
        self.assertEqual(len(DATA["combos"]), 11)
        for name, record in DATA["combos"].items():
            with self.subTest(combo=name):
                state = self.fresh()
                rows = project(record["observed_edges"])
                self.assertEqual(list(rows), list(electrical(record["keys"])))
                ghost = update(state, rows)
                self.assertEqual(ghost.rectangle_count, 0)
                self.assertEqual(labels_in(state.pressed), set(record["keys"]))
                self.assertEqual(labels_in(state.suppressed), set())

    def test_all_20_fixtures_simultaneous_and_two_held_then_third(self):
        self.assertEqual(len(FIXTURES["results"]), 20)
        for fixture in FIXTURES["results"]:
            rows = project(fixture["observed_edges"])
            physical = fixture["pressed_keys"]
            self.assertEqual(list(rows), list(electrical(physical)))
            candidates = mapped(rows)
            with self.subTest(rectangle=fixture["rectangle_id"], held=0):
                state = self.fresh()
                for _ in range(10):  # Time alone must never accept phantoms.
                    update(state, rows)
                    self.assertEqual(labels_in(state.pressed), set())
                    self.assertEqual(labels_in(state.suppressed), candidates)
                    self.assertEqual(labels_in(state.ambiguous), candidates)
                update(state, Rows())
                self.assertEqual(labels_in(state.suppressed), set())
            for held in itertools.combinations(physical, 2):
                with self.subTest(rectangle=fixture["rectangle_id"], held=held):
                    state = self.fresh()
                    update(state, electrical(held))
                    self.assertEqual(labels_in(state.pressed), set(held))
                    update(state, rows)
                    self.assertEqual(labels_in(state.pressed), set(held))
                    self.assertEqual(labels_in(state.suppressed), candidates - set(held))
                    self.assertNotIn(fixture["omitted_key"], labels_in(state.pressed))
                    # Exit ambiguity with a formerly suppressed real key still held.
                    remaining = [physical[2], physical[0]]
                    clear = electrical(remaining)
                    self.assertEqual(update(state, clear).rectangle_count, 0)
                    self.assertEqual(labels_in(state.pressed), set(remaining))
                    self.assertEqual(labels_in(state.suppressed), set())

    def test_all_rectangle_locations_never_invent_new_logical_keys(self):
        # Includes completely mapped, partially mapped and empty rectangles.
        for r1, r2 in itertools.combinations(range(8), 2):
            for c1, c2 in itertools.combinations(range(18), 2):
                state = self.fresh()
                rows = rectangle(r1, r2, c1, c2)
                update(state, rows)
                self.assertEqual(labels_in(state.pressed), set())
                self.assertEqual(labels_in(state.suppressed), mapped(rows))

    def test_unmapped_fourth_corner_participates_in_detection(self):
        for r1, r2 in itertools.combinations(range(8), 2):
            for c1, c2 in itertools.combinations(range(18), 2):
                positions = [(r1, c1), (r1, c2), (r2, c1), (r2, c2)]
                real = [AT[p] for p in positions if p in AT]
                if len(real) == 3:
                    state = self.fresh()
                    rows = electrical(real)
                    ghost = update(state, rows)
                    self.assertEqual(ghost.rectangle_count, 1)
                    self.assertEqual(labels_in(state.pressed), set())
                    self.assertEqual(labels_in(state.suppressed), set(real))
                    state = self.fresh()
                    update(state, electrical(real[:2]))
                    update(state, rows)
                    self.assertEqual(labels_in(state.pressed), set(real[:2]))
                    self.assertEqual(labels_in(state.suppressed), {real[2]})
                    return
        self.fail("Fixture map contains no three-mapped-corner rectangle")

    def test_unmapped_coordinates_never_generate_keys(self):
        rows = Rows()
        for r in range(8):
            for c in range(18):
                if (r, c) not in AT:
                    rows[r] |= 1 << c
        state = self.fresh()
        update(state, rows)
        self.assertEqual(labels_in(state.pressed), set())
        self.assertEqual(labels_in(state.suppressed), set())
        self.assertEqual(labels_in(state.ambiguous), set())

    def test_release_with_remaining_ambiguity_and_hidden_release(self):
        # Rows FPC17/18, columns FPC01/02/05 are all mapped.
        a, b = AT[(0, 0)], AT[(0, 4)]
        bridge, opposite = AT[(0, 1)], AT[(1, 1)]
        state = self.fresh()
        update(state, electrical([a, b]))
        update(state, electrical([a, b, bridge, opposite]))
        self.assertEqual(labels_in(state.pressed), {a, b})
        # Releasing column 5 removes b electrically, but a 2x2 remains.
        self.assertEqual(update(state, electrical([a, bridge, opposite])).rectangle_count, 1)
        self.assertEqual(labels_in(state.pressed), {a})
        self.assertNotIn(b, labels_in(state.suppressed))
        # a is physically released but still electrically compatible through
        # three other corners: keep it, do not pretend that release is known.
        replacement = AT[(1, 0)]
        update(state, electrical([replacement, bridge, opposite]))
        self.assertEqual(labels_in(state.pressed), {a})
        update(state, electrical([replacement, bridge]))
        self.assertEqual(labels_in(state.pressed), {replacement, bridge})

    def test_unambiguous_new_key_outside_rectangle_is_accepted(self):
        rows = rectangle(0, 1, 0, 1)
        outside = next(label for label, (r, c) in COORDS.items()
                       if r not in (0, 1) and c not in (0, 1))
        r, c = COORDS[outside]
        rows[r] |= 1 << c
        state = self.fresh()
        update(state, rows)
        self.assertEqual(labels_in(state.pressed), {outside})
        self.assertEqual(labels_in(state.suppressed), mapped(rows) - {outside})

    def test_released_confirmation_cannot_be_reused_in_later_ambiguity(self):
        state = self.fresh()
        update(state, electrical(["X", "Left Ctrl"]))
        update(state, Rows())
        update(state, electrical(["X", "Left Ctrl", "U"]))
        self.assertEqual(labels_in(state.pressed), set())


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        DLL.Test_Reset()

    def tearDown(self):
        self.assertEqual(DLL.Test_HarnessErrors(), 0)

    def sample(self, labels):
        DLL.Test_LogicalSample(electrical(labels))

    def snapshot(self):
        result = Snapshot()
        DLL.KeyboardRuntime_GetSnapshot(C.byref(result))
        return result

    def output(self):
        return DLL.Test_Output().decode()

    def test_keys_lists_retained_suppressed_and_ambiguous_separately(self):
        self.sample(["X", "Left Ctrl"])
        self.sample(["X", "Left Ctrl", "U"])
        DLL.Test_Feed(b"KEYS\n")
        result = self.output()
        pressed = {line[4:] for line in result.splitlines() if line.startswith("KEY,")}
        suppressed = {line[11:] for line in result.splitlines() if line.startswith("SUPPRESSED,")}
        self.assertEqual(pressed, {"KEY_X", "KEY_LEFT_CTRL"})
        self.assertEqual(suppressed, {"KEY_U", "KEY_PIPE_BACKSLASH"})
        for line in ("KEYS_BEGIN", "POLICY,CONSERVATIVE", "KEY_COUNT,2",
                     "SUPPRESSED_COUNT,2", "AMBIGUOUS_COUNT,4", "KEYS_END"):
            self.assertIn(line + "\r\n", result)
        self.assertEqual(self.snapshot().scan_count, 2)

    def test_history_is_updated_during_blocking_dump(self):
        self.sample(["X"])
        plan = (C.c_uint32 * 16)(*Rows(), *electrical(["X", "Left Ctrl", "U"]))
        DLL.Test_PlanLogicalDuringTx(plan, 2)
        DLL.Test_QueueDuringTx(b"KEYS\n")
        DLL.Test_Feed(b"DUMP\n")
        self.assertTrue(self.output().endswith("MATRIX_END\r\n"))
        # Release then ambiguity occurred entirely while main was sending.
        current = self.snapshot()
        self.assertEqual(current.scan_count, 3)
        self.assertEqual(labels_in(current.keys.pressed), set())
        self.assertEqual(len(labels_in(current.keys.suppressed)), 4)
        DLL.Test_Drain()
        self.assertTrue(self.output().endswith("KEYS_END\r\n"))
        self.assertIn("KEY_COUNT,0\r\nSUPPRESSED,", self.output())
        self.assertNotIn("ERR:", self.output())

    def test_keys_reply_is_one_snapshot_while_state_changes_during_tx(self):
        self.sample(["A"])
        plan = Rows()
        DLL.Test_PlanLogicalDuringTx(plan, 1)
        DLL.Test_Feed(b"KEYS\n")
        self.assertIn("KEY,KEY_A\r\nKEY_COUNT,1\r\n", self.output())
        self.assertEqual(labels_in(self.snapshot().keys.pressed), set())

    def test_startup_with_ambiguous_keys_and_recovery(self):
        self.sample(["X", "Left Ctrl", "U"])
        self.assertEqual(labels_in(self.snapshot().keys.pressed), set())
        self.sample(["X", "U"])
        self.assertEqual(labels_in(self.snapshot().keys.pressed), {"X", "U"})
        self.sample([])
        DLL.Test_Feed(b"KEYS\n")
        self.assertIn("KEY_COUNT,0\r\nSUPPRESSED_COUNT,0\r\nAMBIGUOUS_COUNT,0", self.output())


if __name__ == "__main__":
    unittest.main(verbosity=2)
