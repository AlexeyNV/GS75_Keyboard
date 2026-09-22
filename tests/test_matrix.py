"""Run actual C debounce, anti-ghosting and UART code against captured data."""
import ctypes as C
import itertools
import json
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
DLL = C.CDLL(str(Path(sys.argv.pop(1)).resolve()))
ROWS, COLS = 8, 18
DRIVE = list(range(17, 25))
SENSE = list(range(1, 17)) + [25, 26]
RowArray = C.c_uint32 * ROWS


class Ghost(C.Structure):
    _fields_ = [("ambiguous", RowArray), ("row_pair_count", C.c_uint32),
                ("rectangle_count", C.c_uint32)]


class Debounce(C.Structure):
    _fields_ = [("candidate", RowArray), ("stable", RowArray),
                ("changed_at", (C.c_uint32 * COLS) * ROWS)]


class Snapshot(C.Structure):
    _fields_ = [("raw", RowArray), ("debounced", RowArray), ("ghost", Ghost),
                ("ghost_changes", C.c_uint32), ("scan_count", C.c_uint32),
                ("timestamp_ms", C.c_uint32), ("max_scan_us", C.c_uint32)]


DLL.AntiGhost_Analyze.argtypes = [C.POINTER(C.c_uint32), C.POINTER(Ghost)]
DLL.MatrixDebounce_Init.argtypes = [C.POINTER(Debounce)]
DLL.MatrixDebounce_Update.argtypes = [C.POINTER(Debounce), C.POINTER(C.c_uint32), C.c_uint32]
DLL.Test_Feed.argtypes = [C.c_char_p]
DLL.Test_Output.restype = C.c_char_p
DLL.Test_SetSnapshot.argtypes = [C.POINTER(Snapshot)]
DLL.Test_MutateOnSend.argtypes = [C.c_uint8]
DLL.Test_Poll.argtypes = [C.c_uint32]
DLL.Test_ReceiveOnly.argtypes = [C.c_char_p]
DLL.Test_QueueDuringTx.argtypes = [C.c_char_p]
DLL.Test_RxError.argtypes = [C.c_uint32]
DLL.Test_HarnessErrors.restype = C.c_uint32
DLL.Test_IrqPriority.restype = C.c_uint32
DLL.__get_PRIMASK.restype = C.c_uint32
DLL.__set_PRIMASK.argtypes = [C.c_uint32]
DLL.UartRx_Pop.argtypes = [C.POINTER(C.c_uint8)]
DLL.UartRx_Pop.restype = C.c_int
for name in ("AntiGhost_Analyze", "MatrixDebounce_Init", "MatrixDebounce_Update",
             "Test_Feed", "Test_Reset", "Test_ClearOutput", "Test_SetSnapshot",
             "Test_MutateOnSend", "Test_Poll", "Test_ReceiveOnly",
             "Test_QueueDuringTx", "Test_RxError", "Test_Drain"):
    getattr(DLL, name).restype = None


def project(edges):
    rows = RowArray()
    for edge in edges:
        src, dst = map(int, re.findall(r"FPC(\d+)", edge))
        if src in DRIVE and dst in SENSE:
            rows[DRIVE.index(src)] |= 1 << SENSE.index(dst)
    return rows


def analyze(rows):
    result = Ghost()
    DLL.AntiGhost_Analyze(rows, C.byref(result))
    return result


def settle(rows):
    state = Debounce()
    DLL.MatrixDebounce_Init(C.byref(state))
    for tick in range(6):
        DLL.MatrixDebounce_Update(C.byref(state), rows, tick)
    return state.stable


class MatrixTests(unittest.TestCase):
    def test_captured_single_keys(self):
        data = json.loads((ROOT / "tools/matrix_mapper/gs75_matrix_20260921_172228.json").read_text())
        self.assertEqual(data["baseline"]["edges"], [])
        self.assertEqual(len(data["singles"]), 102)
        coordinates = set()
        for key, record in data["singles"].items():
            with self.subTest(key=key):
                edges = record["active_edges"]
                self.assertEqual(len(edges), 2)
                src, dst = re.findall(r"FPC\d+", edges[0])
                self.assertEqual(set(edges), {src + "->" + dst, dst + "->" + src})
                rows = project(edges)
                self.assertEqual(sum(int(row).bit_count() for row in rows), 1)
                self.assertEqual(list(settle(rows)), list(rows))
                self.assertEqual(analyze(rows).row_pair_count, 0)
                coord = tuple(rows)
                self.assertNotIn(coord, coordinates)
                coordinates.add(coord)
        # The real scanner must use the same ordering as these fixtures.
        scanner = (ROOT / "Core/Src/matrix.c").read_text()
        for name, expected in (("drive_pins", DRIVE), ("sense_pins", SENSE)):
            body = re.search(name + r"\[.*?\]\s*=\s*\{(.*?)\};", scanner, re.S)[1]
            self.assertEqual([int(n) for n in re.findall(r"FPC(\d+)_GPIO_Port", body)], expected)

    def test_all_captured_ghosting_cases_without_suppression(self):
        data = json.loads((ROOT / "tools/matrix_mapper/ghosting_results/gs75_ghosting_20260921_174434.json").read_text())
        self.assertEqual(data["matrix"]["rows"], DRIVE)
        self.assertEqual(data["matrix"]["cols"], SENSE)
        self.assertEqual(data["baseline"]["edges"], [])
        self.assertEqual(len(data["results"]), 20)
        for record in data["results"]:
            with self.subTest(rectangle=record["rectangle_id"]):
                self.assertEqual(record["status"], "captured")
                self.assertTrue(record["expected_ghost_seen"])
                raw = project(record["observed_edges"])
                before = list(raw)
                stable = settle(raw)
                ghost = analyze(stable)
                self.assertEqual(list(stable), before)  # Phantom corner retained.
                self.assertEqual(list(raw), before)
                self.assertGreaterEqual(ghost.rectangle_count, 1)
                self.assertEqual(list(ghost.ambiguous), before)
                self.assertEqual(sum(int(row).bit_count() for row in stable), 4)

    def test_all_rectangle_positions(self):
        # Every placement, including columns FPC25/FPC26.
        for r1, r2 in itertools.combinations(range(ROWS), 2):
            for c1, c2 in itertools.combinations(range(COLS), 2):
                rows = RowArray()
                rows[r1] = rows[r2] = (1 << c1) | (1 << c2)
                ghost = analyze(rows)
                self.assertEqual(ghost.row_pair_count, 1)
                self.assertEqual(ghost.rectangle_count, 1)
                self.assertEqual(list(ghost.ambiguous), list(rows))

    def test_non_rectangles_and_full_matrix(self):
        for rows in (RowArray(), RowArray((1 << COLS) - 1),
                     RowArray(*([1] * ROWS)), RowArray(1, 2, 4, 8),
                     RowArray(3, 1)):  # Incomplete L: not yet a measured rectangle.
            self.assertEqual(analyze(rows).rectangle_count, 0)
        rows = RowArray(*([0xFFFFFFFF] * ROWS))
        ghost = analyze(rows)
        self.assertEqual(ghost.row_pair_count, 28)
        self.assertEqual(ghost.rectangle_count, 4284)
        self.assertEqual(list(ghost.ambiguous), [(1 << COLS) - 1] * ROWS)
        DLL.AntiGhost_Analyze(RowArray(), C.byref(ghost))
        self.assertEqual(ghost.rectangle_count, 0)
        self.assertEqual(list(ghost.ambiguous), [0] * ROWS)

    def test_independent_press_release_bounce(self):
        state, raw = Debounce(), RowArray()
        DLL.MatrixDebounce_Init(C.byref(state))
        raw[0] = 1
        for tick in range(5):
            raw[1] = tick % 2  # Bouncing second key must not delay first.
            DLL.MatrixDebounce_Update(C.byref(state), raw, tick)
            self.assertEqual(state.stable[0], 0)
        raw[1] = 0
        DLL.MatrixDebounce_Update(C.byref(state), raw, 5)
        self.assertEqual(state.stable[0], 1)
        self.assertEqual(state.stable[1], 0)
        for tick, value in ((6, 0), (7, 1), (8, 0), (9, 0), (10, 0), (11, 0), (12, 0)):
            raw[0] = value
            DLL.MatrixDebounce_Update(C.byref(state), raw, tick)
            self.assertEqual(state.stable[0], 1)
        DLL.MatrixDebounce_Update(C.byref(state), raw, 13)
        self.assertEqual(state.stable[0], 0)

    def test_same_row_independence_and_tick_wrap(self):
        state, raw = Debounce(), RowArray()
        DLL.MatrixDebounce_Init(C.byref(state))
        for offset in range(6):
            raw[7] = (1 << 17) | ((offset % 2) << 16)
            DLL.MatrixDebounce_Update(C.byref(state), raw, (0xFFFFFFFD + offset) & 0xFFFFFFFF)
            self.assertEqual(state.stable[7], (1 << 17) if offset == 5 else 0)


class DebugTests(unittest.TestCase):
    def setUp(self):
        DLL.Test_Reset()

    def tearDown(self):
        # No RX was missed by the harness and no TX ran inside an ISR/lock.
        self.assertEqual(DLL.Test_HarnessErrors(), 0)

    def output(self):
        return DLL.Test_Output().decode()

    def test_overflow_and_line_endings(self):
        DLL.Test_Feed(b"X" * 64 + b"PING")
        self.assertEqual(self.output(), "ERR: BUFFER OVERFLOW\r\n")
        DLL.Test_Feed(b"\r\nPING\n")
        self.assertEqual(self.output(), "ERR: BUFFER OVERFLOW\r\nPONG\r\n")
        DLL.Test_ClearOutput()
        DLL.Test_Feed(b"X" * 63 + b"\nPING\r")
        self.assertEqual(self.output(), "ERR: UNKNOWN COMMAND\r\nPONG\r\n")

    def test_snapshot_during_blocking_dump(self):
        snapshot = Snapshot()
        snapshot.raw[0] = 1
        snapshot.debounced[7] = 1 << 17
        snapshot.scan_count = 42
        DLL.Test_SetSnapshot(C.byref(snapshot))
        DLL.Test_MutateOnSend(1)
        DLL.Test_Feed(b"DUMP\n")
        result = self.output()
        self.assertIn("MATRIX_STATE,DEBOUNCED,8,18\r\n", result)
        self.assertIn("SCAN,42,", result)
        self.assertIn("EDGE,FPC24,FPC26\r\nEDGE_COUNT,1\r\nMATRIX_END\r\n", result)
        self.assertNotIn("EDGE,FPC17,FPC01", result)

    def test_raw_and_ghost_commands_preserve_ambiguous_keys(self):
        snapshot = Snapshot()
        snapshot.raw[0] = snapshot.raw[1] = 3
        snapshot.debounced[0] = snapshot.debounced[1] = 3
        snapshot.ghost = analyze(snapshot.debounced)
        DLL.Test_SetSnapshot(C.byref(snapshot))
        DLL.Test_Feed(b"RAW\nDUMP\nGHOST\n")
        result = self.output()
        self.assertEqual(result.count("EDGE_COUNT,4\r\n"), 2)
        self.assertIn("GHOST,RAW,ACTIVE,ROW_PAIRS,1,RECTANGLES,1", result)
        self.assertIn("GHOST,DEBOUNCED,ACTIVE,ROW_PAIRS,1,RECTANGLES,1", result)
        self.assertTrue(result.endswith("GHOST_END\r\n"))

    def test_diagnostic_rate_limit_and_clear(self):
        snapshot = Snapshot()
        snapshot.debounced[0] = snapshot.debounced[1] = 3
        snapshot.ghost = analyze(snapshot.debounced)
        snapshot.ghost_changes = 1
        DLL.Test_SetSnapshot(C.byref(snapshot))
        DLL.Test_Poll(99)
        self.assertEqual(self.output(), "")
        DLL.Test_Poll(100)
        self.assertIn("GHOST_EVENT,1,", self.output())
        DLL.Test_ClearOutput()
        DLL.Test_Poll(200)
        self.assertEqual(self.output(), "")
        snapshot.ghost = Ghost()
        snapshot.ghost_changes = 2
        DLL.Test_SetSnapshot(C.byref(snapshot))
        DLL.Test_Poll(300)
        self.assertIn("GHOST_EVENT,2,", self.output())
        self.assertIn("GHOST,DEBOUNCED,CLEAR", self.output())

    def test_info(self):
        DLL.Test_Feed(b"INFO\r\n")
        for field in ("SYSCLK=96000000", "MATRIX_ROWS=8", "MATRIX_COLS=18",
                      "SCAN_HZ=1000", "DEBOUNCE_MS=5", "ANTI_GHOST=DETECT_ONLY"):
            self.assertIn(field + "\r\n", self.output())

    def test_isr_only_queues_and_priority_preempts_scan(self):
        self.assertLess(DLL.Test_IrqPriority(), 15)
        DLL.Test_ReceiveOnly(b"PING\r\nINFO\n")
        self.assertEqual(self.output(), "")
        DLL.Test_Drain()
        self.assertTrue(self.output().startswith("PONG\r\nGS75 Matrix 8x18 Firmware"))
        byte = C.c_uint8()
        # The consumer preserves a caller's existing interrupt mask.
        getattr(DLL, "__set_PRIMASK")(1)
        self.assertEqual(DLL.UartRx_Pop(C.byref(byte)), 0)
        self.assertEqual(getattr(DLL, "__get_PRIMASK")(), 1)
        getattr(DLL, "__set_PRIMASK")(0)

    def test_command_received_during_automatic_ghost_event(self):
        snapshot = Snapshot()
        snapshot.debounced[0] = snapshot.debounced[1] = 3
        snapshot.ghost = analyze(snapshot.debounced)
        snapshot.ghost_changes = 1
        DLL.Test_SetSnapshot(C.byref(snapshot))
        DLL.Test_QueueDuringTx(b"PING\r\nINFO\r\nRAW\nGHOST\nDUMP\n")
        DLL.Test_Poll(100)  # All RX arrives during the GHOST_EVENT TX burst.
        before = self.output()
        self.assertIn("GHOST_EVENT,1,", before)
        self.assertNotIn("PONG", before)
        self.assertNotIn("MATRIX_BEGIN", before)
        DLL.Test_Drain()
        after = self.output()[len(before):]
        self.assertTrue(after.startswith("PONG\r\nGS75 Matrix 8x18 Firmware"))
        self.assertIn("MATRIX_STATE,RAW,8,18", after)
        self.assertIn("MATRIX_STATE,DEBOUNCED,8,18", after)
        self.assertIn("GHOST_BEGIN", after)
        self.assertNotIn("ERR:", after)

    def test_partial_command_spans_ghost_tx(self):
        snapshot = Snapshot()
        snapshot.ghost_changes = 2
        DLL.Test_SetSnapshot(C.byref(snapshot))
        DLL.Test_Feed(b"PI")
        DLL.Test_QueueDuringTx(b"NG\r\n")
        DLL.Test_Poll(100)
        self.assertNotIn("PONG", self.output())
        DLL.Test_Drain()
        self.assertTrue(self.output().endswith("PONG\r\n"))
        self.assertNotIn("ERR:", self.output())

    def test_commands_arrive_during_full_matrix_dump(self):
        snapshot = Snapshot()
        snapshot.debounced = RowArray(*([(1 << COLS) - 1] * ROWS))
        DLL.Test_SetSnapshot(C.byref(snapshot))
        DLL.Test_QueueDuringTx(b"PING\nINFO\nRAW\nGHOST\n")
        DLL.Test_Feed(b"DUMP\n")
        first = self.output()
        self.assertEqual(first.count("EDGE,FPC"), 144)
        self.assertTrue(first.endswith("EDGE_COUNT,144\r\nMATRIX_END\r\n"))
        self.assertNotIn("PONG", first)  # No nested command execution from IRQ.
        DLL.Test_Drain()
        self.assertTrue(self.output()[len(first):].startswith("PONG\r\n"))
        self.assertTrue(self.output().endswith("GHOST_END\r\n"))
        self.assertNotIn("ERR:", self.output())

    def test_long_command_during_tx_resynchronizes_at_crlf(self):
        DLL.Test_QueueDuringTx(b"X" * 64 + b"PING\r\nPING\n")
        DLL.Test_Feed(b"INFO\n")
        self.assertNotIn("ERR:", self.output())
        DLL.Test_ClearOutput()
        DLL.Test_Drain()
        self.assertEqual(self.output(), "ERR: BUFFER OVERFLOW\r\nPONG\r\n")

    def test_ring_capacity_and_wrap(self):
        # 255 usable slots; cycle head/tail through wrap several times.
        for _ in range(4):
            DLL.Test_ReceiveOnly(b"\n" * 250 + b"PING\n")
            self.assertEqual(self.output(), "")
            DLL.Test_Drain()
            self.assertEqual(self.output(), "PONG\r\n")
            DLL.Test_ClearOutput()

    def test_ring_overflow_during_tx_drops_damaged_stream(self):
        snapshot = Snapshot()
        snapshot.debounced = RowArray(*([(1 << COLS) - 1] * ROWS))
        DLL.Test_SetSnapshot(C.byref(snapshot))
        DLL.Test_QueueDuringTx(b"X" * 256 + b"PING\n")
        DLL.Test_Feed(b"DUMP\n")
        self.assertNotIn("ERR:", self.output())  # ISR does not print.
        DLL.Test_ClearOutput()
        DLL.Test_Drain()
        self.assertEqual(self.output(), "ERR: RX OVERFLOW\r\n")
        # The delimiter during the lost stream was itself dropped.
        # Only a subsequently received delimiter re-establishes framing.
        DLL.Test_Feed(b"PING\r\nPING\n")
        self.assertEqual(self.output(), "ERR: RX OVERFLOW\r\nPONG\r\n")

    def test_hardware_rx_errors_recover_without_executing_suffix(self):
        for flag in (1, 2, 4, 8):  # PE, FE, NE, ORE.
            with self.subTest(flag=flag):
                DLL.Test_Reset()
                DLL.Test_Feed(b"PI")
                DLL.Test_RxError(flag)
                self.assertEqual(self.output(), "")
                DLL.Test_Drain()
                self.assertEqual(self.output(), "ERR: UART RX ERROR\r\n")
                DLL.Test_Feed(b"NG\nPING\n")
                self.assertEqual(self.output(), "ERR: UART RX ERROR\r\nPONG\r\n")


if __name__ == "__main__":
    unittest.main(verbosity=2)
