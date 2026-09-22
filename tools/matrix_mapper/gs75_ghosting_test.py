#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
GS75 keyboard ghosting / matrix ambiguity tester.

Uses the existing STM32 diagnostic protocol:
    PING -> PONG
    DUMP -> MATRIX_BEGIN / EDGE,... / EDGE_COUNT,n / MATRIX_END

Input:
    JSON produced by gs75_matrix_mapper.py with a complete "singles" map.

What the script does:
1. Reconstructs the bipartite keyboard matrix from the single-key map.
2. Finds all fully populated 2x2 rectangles.
3. Builds 3-key tests (press three corners, leave the fourth unpressed).
4. Waits until the exact expected electrical connectivity is observed.
5. Reports which unpressed mapped key(s) are electrically indistinguishable
   from pressed keys ("phantom candidates").
6. Saves detailed JSON and CSV results.

No firmware changes are required for this test.

Example:
    py gs75_ghosting_test.py --port COM17 --map gs75_matrix_20260921_172228.json --count 20
"""

from __future__ import annotations

import argparse
import csv
import itertools
import json
import re
import time
from collections import defaultdict, deque
from datetime import datetime
from pathlib import Path
from typing import Dict, FrozenSet, Iterable, List, Optional, Sequence, Set, Tuple

import serial
from serial.tools import list_ports

Edge = Tuple[int, int]
EdgeSet = FrozenSet[Edge]
Pair = Tuple[int, int]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def edge_name(edge: Edge) -> str:
    return f"FPC{edge[0]:02d}->FPC{edge[1]:02d}"


def pair_name(pair: Pair) -> str:
    a, b = sorted(pair)
    return f"FPC{a:02d}<->FPC{b:02d}"


def edge_set_to_strings(edges: Iterable[Edge]) -> List[str]:
    return [edge_name(e) for e in sorted(edges)]


def parse_edge_string(text: str) -> Edge:
    left, right = text.split("->")
    return int(left.replace("FPC", "")), int(right.replace("FPC", ""))


def choose_port(explicit: Optional[str]) -> str:
    if explicit:
        return explicit

    ports = list(list_ports.comports())
    if not ports:
        raise RuntimeError("COM-порты не найдены.")

    print("Доступные COM-порты:")
    for i, p in enumerate(ports, start=1):
        print(f"  {i}. {p.device:8s}  {p.description}")

    if len(ports) == 1:
        print(f"Использую единственный найденный порт: {ports[0].device}")
        return ports[0].device

    while True:
        raw = input("Выберите номер COM-порта: ").strip()
        try:
            idx = int(raw)
            if 1 <= idx <= len(ports):
                return ports[idx - 1].device
        except ValueError:
            pass
        print("Введите номер из списка.")


def autodetect_map_file() -> Path:
    candidates = sorted(
        Path(".").glob("gs75_matrix_*.json"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )
    if not candidates:
        raise FileNotFoundError(
            "Не найден gs75_matrix_*.json. Укажите файл через --map."
        )
    return candidates[0]


# ---------------------------------------------------------------------------
# UART board protocol
# ---------------------------------------------------------------------------

class Board:
    def __init__(self, port: str, baud: int = 115200, timeout: float = 0.10):
        self.ser = serial.Serial(
            port=port,
            baudrate=baud,
            bytesize=8,
            parity=serial.PARITY_NONE,
            stopbits=1,
            timeout=timeout,
            write_timeout=1.0,
        )
        time.sleep(0.25)
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()

    def close(self) -> None:
        if self.ser.is_open:
            self.ser.close()

    def write_line(self, text: str) -> None:
        self.ser.write((text + "\r\n").encode("ascii"))
        self.ser.flush()

    def read_line_until(self, deadline: float) -> Optional[str]:
        while time.monotonic() < deadline:
            raw = self.ser.readline()
            if raw:
                return raw.decode("ascii", errors="replace").strip()
        return None

    def ping(self, timeout: float = 2.0) -> bool:
        self.ser.reset_input_buffer()
        self.write_line("PING")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.read_line_until(deadline)
            if line is None:
                break
            if line == "PONG":
                return True
        return False

    def dump(self, timeout: float = 2.0) -> EdgeSet:
        self.write_line("DUMP")
        deadline = time.monotonic() + timeout

        while True:
            line = self.read_line_until(deadline)
            if line is None:
                raise TimeoutError("Таймаут ожидания MATRIX_BEGIN.")
            if line == "MATRIX_BEGIN":
                break

        edges: Set[Edge] = set()
        reported_count: Optional[int] = None

        while True:
            line = self.read_line_until(deadline)
            if line is None:
                raise TimeoutError("Таймаут ожидания MATRIX_END.")

            if line.startswith("EDGE,"):
                parts = line.split(",")
                if len(parts) != 3:
                    raise ValueError(f"Некорректная строка EDGE: {line}")
                src = int(parts[1].replace("FPC", ""))
                dst = int(parts[2].replace("FPC", ""))
                edges.add((src, dst))

            elif line.startswith("EDGE_COUNT,"):
                reported_count = int(line.split(",", 1)[1])

            elif line == "MATRIX_END":
                break

        if reported_count is not None and reported_count != len(edges):
            print(
                f"ПРЕДУПРЕЖДЕНИЕ: STM32 EDGE_COUNT={reported_count}, "
                f"разобрано EDGE={len(edges)}."
            )

        return frozenset(edges)


# ---------------------------------------------------------------------------
# Matrix reconstruction
# ---------------------------------------------------------------------------

def load_single_key_pairs(path: Path) -> Tuple[dict, Dict[str, Pair]]:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)

    singles = data.get("singles")
    if not isinstance(singles, dict) or not singles:
        raise ValueError("В JSON нет непустого объекта 'singles'.")

    pairs: Dict[str, Pair] = {}

    for key, rec in singles.items():
        strings = rec.get("active_edges", [])
        edges = {parse_edge_string(s) for s in strings}
        nodes = sorted({n for e in edges for n in e})

        if len(nodes) != 2:
            raise ValueError(
                f"Клавиша {key!r}: ожидалась одна пара FPC, "
                f"получено узлов: {nodes}"
            )

        a, b = nodes
        if (a, b) not in edges or (b, a) not in edges:
            raise ValueError(
                f"Клавиша {key!r}: связь несимметрична, "
                "для этого теста требуется обычный двусторонний контакт."
            )

        pairs[key] = (a, b)

    return data, pairs


def bipartition(pairs: Dict[str, Pair]) -> Tuple[List[int], List[int], Dict[int, int]]:
    adj: Dict[int, Set[int]] = defaultdict(set)

    for a, b in pairs.values():
        adj[a].add(b)
        adj[b].add(a)

    color: Dict[int, int] = {}

    for start in adj:
        if start in color:
            continue

        color[start] = 0
        q = deque([start])

        while q:
            u = q.popleft()
            for v in adj[u]:
                if v not in color:
                    color[v] = 1 - color[u]
                    q.append(v)
                elif color[v] == color[u]:
                    raise ValueError(
                        "Карта не является двудольной матрицей: "
                        f"конфликт FPC{u:02d} - FPC{v:02d}"
                    )

    g0 = sorted(n for n, c in color.items() if c == 0)
    g1 = sorted(n for n, c in color.items() if c == 1)

    # For the measured GS75 map, the compact side is the 8-line scan side.
    if len(g0) > len(g1):
        g0, g1 = g1, g0
        color = {n: 1 - c for n, c in color.items()}

    return g0, g1, color


def make_cell_map(
    pairs: Dict[str, Pair],
    color: Dict[int, int],
) -> Dict[Tuple[int, int], str]:
    cells: Dict[Tuple[int, int], str] = {}

    for key, (a, b) in pairs.items():
        if color[a] == color[b]:
            raise ValueError(f"Некорректная пара {key}: {a}, {b}")

        row, col = (a, b) if color[a] == 0 else (b, a)
        cell = (row, col)

        if cell in cells:
            raise ValueError(
                f"Две клавиши используют одну ячейку {cell}: "
                f"{cells[cell]!r} и {key!r}"
            )

        cells[cell] = key

    return cells


def find_rectangles(
    rows: Sequence[int],
    cols: Sequence[int],
    cells: Dict[Tuple[int, int], str],
) -> List[dict]:
    rectangles: List[dict] = []

    for r1, r2 in itertools.combinations(rows, 2):
        for c1, c2 in itertools.combinations(cols, 2):
            coords = [
                (r1, c1),
                (r1, c2),
                (r2, c1),
                (r2, c2),
            ]

            if all(p in cells for p in coords):
                rectangles.append(
                    {
                        "rows": [r1, r2],
                        "cols": [c1, c2],
                        "keys": [cells[p] for p in coords],
                        "coords": coords,
                    }
                )

    return rectangles


# ---------------------------------------------------------------------------
# Electrical connectivity model
# ---------------------------------------------------------------------------

def transitive_closure_from_pairs(pairs: Iterable[Pair]) -> EdgeSet:
    adj: Dict[int, Set[int]] = defaultdict(set)

    for a, b in pairs:
        adj[a].add(b)
        adj[b].add(a)

    result: Set[Edge] = set()
    visited: Set[int] = set()

    for start in list(adj):
        if start in visited:
            continue

        component: Set[int] = set()
        stack = [start]

        while stack:
            node = stack.pop()
            if node in component:
                continue
            component.add(node)
            visited.add(node)
            stack.extend(adj[node])

        for src in component:
            for dst in component:
                if src != dst:
                    result.add((src, dst))

    return frozenset(result)


def components_from_edges(edges: EdgeSet) -> List[Set[int]]:
    adj: Dict[int, Set[int]] = defaultdict(set)
    for a, b in edges:
        adj[a].add(b)
        adj[b].add(a)

    components: List[Set[int]] = []
    seen: Set[int] = set()

    for start in adj:
        if start in seen:
            continue

        comp: Set[int] = set()
        stack = [start]

        while stack:
            u = stack.pop()
            if u in comp:
                continue
            comp.add(u)
            seen.add(u)
            stack.extend(adj[u])

        components.append(comp)

    return components


def electrically_possible_keys(
    observed_edges: EdgeSet,
    key_pairs: Dict[str, Pair],
) -> Set[str]:
    comps = components_from_edges(observed_edges)
    possible: Set[str] = set()

    for key, (a, b) in key_pairs.items():
        for comp in comps:
            if a in comp and b in comp:
                possible.add(key)
                break

    return possible


# ---------------------------------------------------------------------------
# Test generation
# ---------------------------------------------------------------------------

SIMPLE_KEYS = set(
    list("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
    + list("0123456789")
    + [
        "Esc", "Tab", "Enter", "Space", "Backspace",
        "Left Shift", "Right Shift",
        "Left Ctrl", "Right Ctrl",
        "Left Alt", "Right AltGr", "Win", "Fn",
        "Arrow Up", "Arrow Down", "Arrow Left", "Arrow Right",
    ]
)


def key_penalty(key: str) -> int:
    # Smaller is better for manual testing.
    if key in SIMPLE_KEYS:
        return 0
    if key.startswith("F") and key[1:].isdigit():
        return 1
    if key.startswith("Numpad"):
        return 2
    return 1


def build_candidates(rectangles: Sequence[dict]) -> List[dict]:
    candidates: List[dict] = []
    seen_pressed: Set[Tuple[str, ...]] = set()

    for rect_id, rect in enumerate(rectangles, start=1):
        keys = rect["keys"]
        coords = rect["coords"]

        for omitted_idx in range(4):
            omitted_key = keys[omitted_idx]
            pressed_keys = [keys[i] for i in range(4) if i != omitted_idx]
            pressed_sig = tuple(sorted(pressed_keys))

            if pressed_sig in seen_pressed:
                continue
            seen_pressed.add(pressed_sig)

            candidates.append(
                {
                    "rectangle_id": rect_id,
                    "rows": rect["rows"],
                    "cols": rect["cols"],
                    "rectangle_keys": list(keys),
                    "pressed_keys": pressed_keys,
                    "omitted_key": omitted_key,
                    "omitted_coord": list(coords[omitted_idx]),
                    "manual_penalty": sum(key_penalty(k) for k in pressed_keys),
                }
            )

    return candidates


def select_diverse_candidates(candidates: Sequence[dict], count: int) -> List[dict]:
    """
    Greedy selection:
    - prefer easy-to-press keys,
    - cover many different expected phantom keys,
    - cover different rows/columns,
    - avoid repeating exactly the same physical keys too much.
    """
    remaining = list(candidates)
    selected: List[dict] = []

    covered_phantoms: Set[str] = set()
    covered_rows: Set[int] = set()
    covered_cols: Set[int] = set()
    used_keys: Dict[str, int] = defaultdict(int)

    while remaining and len(selected) < count:
        best = None
        best_score = None

        for cand in remaining:
            new_phantom = 1 if cand["omitted_key"] not in covered_phantoms else 0
            new_rows = sum(r not in covered_rows for r in cand["rows"])
            new_cols = sum(c not in covered_cols for c in cand["cols"])
            repetition = sum(used_keys[k] for k in cand["pressed_keys"])

            # Higher score is better.
            score = (
                100 * new_phantom
                + 12 * new_rows
                + 5 * new_cols
                - 8 * cand["manual_penalty"]
                - 2 * repetition
            )

            tie = (
                score,
                -cand["manual_penalty"],
                cand["omitted_key"],
                tuple(cand["pressed_keys"]),
            )

            if best_score is None or tie > best_score:
                best = cand
                best_score = tie

        assert best is not None
        selected.append(best)
        remaining.remove(best)

        covered_phantoms.add(best["omitted_key"])
        covered_rows.update(best["rows"])
        covered_cols.update(best["cols"])
        for key in best["pressed_keys"]:
            used_keys[key] += 1

    return selected


# ---------------------------------------------------------------------------
# Interactive capture
# ---------------------------------------------------------------------------

def most_common_state(states: Sequence[EdgeSet]) -> Tuple[EdgeSet, int]:
    counts: Dict[EdgeSet, int] = defaultdict(int)
    for state in states:
        counts[state] += 1
    return max(counts.items(), key=lambda kv: kv[1])


def capture_baseline(
    board: Board,
    samples: int,
    poll: float,
) -> Tuple[EdgeSet, int]:
    states: List[EdgeSet] = []

    print("\nBASELINE: ничего не нажимайте.")
    for i in range(samples):
        state = board.dump()
        states.append(state)
        print(f"  {i + 1:02d}/{samples}: {len(state)} directed edge(s)")
        time.sleep(poll)

    baseline, votes = most_common_state(states)
    return baseline, votes


def wait_for_expected_state(
    board: Board,
    expected: EdgeSet,
    baseline: EdgeSet,
    timeout: float,
    stable_required: int,
    poll: float,
) -> Tuple[Optional[EdgeSet], EdgeSet]:
    deadline = time.monotonic() + timeout
    stable = 0
    last_state = baseline

    while time.monotonic() < deadline:
        state = board.dump()
        last_state = state

        if state == expected:
            stable += 1
            if stable >= stable_required:
                return state, last_state
        else:
            stable = 0

        time.sleep(poll)

    return None, last_state


def wait_for_release(
    board: Board,
    baseline: EdgeSet,
    timeout: float,
    stable_required: int,
    poll: float,
) -> bool:
    deadline = time.monotonic() + timeout
    stable = 0

    while time.monotonic() < deadline:
        state = board.dump()
        if state == baseline:
            stable += 1
            if stable >= stable_required:
                return True
        else:
            stable = 0
        time.sleep(poll)

    return False


def capture_test(
    board: Board,
    test: dict,
    key_pairs: Dict[str, Pair],
    baseline: EdgeSet,
    timeout: float,
    stable_required: int,
    sample_count: int,
    poll: float,
) -> dict:
    pressed = test["pressed_keys"]
    omitted = test["omitted_key"]

    physical_pairs = [key_pairs[k] for k in pressed]
    expected_active = transitive_closure_from_pairs(physical_pairs)
    expected_full = frozenset(set(baseline) | set(expected_active))

    print("\n" + "=" * 78)
    print("Нажмите и УДЕРЖИВАЙТЕ одновременно:")
    print("    " + " + ".join(pressed))
    print()
    print(f"НЕ нажимайте: {omitted}")
    print(
        "Можно нажимать клавиши по очереди — скрипт начнёт запись только "
        "когда увидит полное ожидаемое состояние."
    )

    detected, last_state = wait_for_expected_state(
        board,
        expected_full,
        baseline,
        timeout,
        stable_required,
        poll,
    )

    if detected is None:
        extra = sorted(set(last_state) - set(expected_full))
        missing = sorted(set(expected_full) - set(last_state))

        print(f"\nОжидаемая комбинация не обнаружена за {timeout:.0f} с.")
        if extra:
            print("Последний снимок: лишние связи:")
            for e in extra:
                print("   ", edge_name(e))
        if missing:
            print("Последний снимок: отсутствующие связи:")
            for e in missing:
                print("   ", edge_name(e))

        return {
            "status": "not_detected",
            "pressed_keys": pressed,
            "omitted_key": omitted,
            "expected_edges": edge_set_to_strings(expected_full),
            "last_edges": edge_set_to_strings(last_state),
        }

    samples: List[EdgeSet] = [detected]
    for _ in range(max(0, sample_count - 1)):
        time.sleep(poll)
        samples.append(board.dump())

    observed, votes = most_common_state(samples)
    observed_active = frozenset(set(observed) - set(baseline))

    possible = electrically_possible_keys(observed_active, key_pairs)
    phantom_candidates = sorted(possible - set(pressed))
    expected_ghost_seen = omitted in phantom_candidates

    print(f"\nЗафиксировано: {votes}/{len(samples)} одинаковых снимков.")
    print(f"Физически нажаты: {', '.join(pressed)}")
    print(f"Оставлена ненажатой: {omitted}")

    if phantom_candidates:
        print("Электрически неоднозначные ненажатые клавиши:")
        for key in phantom_candidates:
            marker = "  <-- ожидаемый 4-й угол" if key == omitted else ""
            print(f"    {key}{marker}")
    else:
        print("Дополнительных электрически возможных клавиш не найдено.")

    if expected_ghost_seen:
        print("РЕЗУЛЬТАТ: GHOST AMBIGUITY CONFIRMED")
    else:
        print("РЕЗУЛЬТАТ: ожидаемый phantom-кандидат НЕ обнаружен")

    print("Теперь отпустите ВСЕ клавиши...")
    released = wait_for_release(
        board,
        baseline,
        timeout,
        stable_required,
        poll,
    )

    if released:
        print("Возврат к baseline подтверждён.")
    else:
        print("ПРЕДУПРЕЖДЕНИЕ: возврат к baseline не подтверждён.")

    return {
        "status": "captured",
        "rectangle_id": test["rectangle_id"],
        "rows": test["rows"],
        "cols": test["cols"],
        "rectangle_keys": test["rectangle_keys"],
        "pressed_keys": pressed,
        "omitted_key": omitted,
        "physical_pairs": [pair_name(p) for p in physical_pairs],
        "expected_edges": edge_set_to_strings(expected_full),
        "observed_edges": edge_set_to_strings(observed),
        "matching_samples": votes,
        "sample_count": len(samples),
        "electrically_possible_keys": sorted(possible),
        "phantom_candidates": phantom_candidates,
        "expected_ghost_seen": expected_ghost_seen,
        "release_confirmed": released,
    }


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def save_json(path: Path, obj: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2)


def save_csv(path: Path, results: Sequence[dict]) -> None:
    with path.open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(
            [
                "test",
                "pressed_keys",
                "omitted_key",
                "status",
                "expected_ghost_seen",
                "phantom_candidates",
                "matching_samples",
                "release_confirmed",
            ]
        )

        for i, rec in enumerate(results, start=1):
            w.writerow(
                [
                    i,
                    " + ".join(rec.get("pressed_keys", [])),
                    rec.get("omitted_key", ""),
                    rec.get("status", ""),
                    rec.get("expected_ghost_seen", ""),
                    " | ".join(rec.get("phantom_candidates", [])),
                    rec.get("matching_samples", ""),
                    rec.get("release_confirmed", ""),
                ]
            )


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="GS75 3-key ghosting / matrix ambiguity tester"
    )
    parser.add_argument("--port", help="COM-порт, например COM17")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--map",
        dest="map_path",
        type=Path,
        help="JSON, созданный gs75_matrix_mapper.py",
    )
    parser.add_argument(
        "--count",
        type=int,
        default=20,
        help="Количество автоматически выбранных 3-key тестов (default: 20)",
    )
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--stable-scans", type=int, default=3)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--poll", type=float, default=0.05)
    parser.add_argument("--baseline-samples", type=int, default=10)
    parser.add_argument(
        "--list-only",
        action="store_true",
        help="Только показать выбранные тесты, UART не открывать",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("ghosting_results"),
    )
    args = parser.parse_args()

    map_path = args.map_path or autodetect_map_file()
    source_data, key_pairs = load_single_key_pairs(map_path)

    rows, cols, color = bipartition(key_pairs)
    cells = make_cell_map(key_pairs, color)
    rectangles = find_rectangles(rows, cols, cells)
    candidates = build_candidates(rectangles)

    if args.count <= 0:
        raise ValueError("--count должен быть > 0")

    tests = select_diverse_candidates(candidates, min(args.count, len(candidates)))

    print("=" * 78)
    print("GS75 GHOSTING TEST")
    print(f"Карта: {map_path}")
    print(f"Одиночных клавиш: {len(key_pairs)}")
    print("Группа scan/rows: " + ", ".join(f"FPC{x:02d}" for x in rows))
    print("Группа sense/cols: " + ", ".join(f"FPC{x:02d}" for x in cols))
    print(f"Полных 2x2 прямоугольников: {len(rectangles)}")
    print(f"Кандидатов 3-key тестов: {len(candidates)}")
    print(f"Будет выполнено тестов: {len(tests)}")

    print("\nВыбранные тесты:")
    for i, t in enumerate(tests, start=1):
        print(
            f"{i:02d}. {' + '.join(t['pressed_keys'])}"
            f"    -> phantom candidate: {t['omitted_key']}"
        )

    if args.list_only:
        return 0

    port = choose_port(args.port)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    json_path = args.output_dir / f"gs75_ghosting_{stamp}.json"
    csv_path = args.output_dir / f"gs75_ghosting_{stamp}.csv"

    output = {
        "format_version": 1,
        "created_local": datetime.now().isoformat(timespec="seconds"),
        "source_map": str(map_path),
        "serial": {"port": port, "baud": args.baud},
        "matrix": {
            "rows": rows,
            "cols": cols,
            "single_key_count": len(key_pairs),
            "rectangle_count": len(rectangles),
            "candidate_test_count": len(candidates),
        },
        "selected_tests": tests,
        "baseline": {},
        "results": [],
    }

    board: Optional[Board] = None

    try:
        print(f"\nОткрываю {port} @ {args.baud}...")
        board = Board(port, args.baud)

        if not board.ping():
            print(
                "ОШИБКА: PONG не получен. Закройте PuTTY/miniterm и "
                "проверьте COM-порт."
            )
            return 2

        print("UART: PONG получен.")

        baseline, votes = capture_baseline(
            board,
            args.baseline_samples,
            args.poll,
        )

        output["baseline"] = {
            "edges": edge_set_to_strings(baseline),
            "matching_samples": votes,
            "sample_count": args.baseline_samples,
        }

        print(
            f"Baseline: {len(baseline)} edge(s), "
            f"{votes}/{args.baseline_samples} одинаковых."
        )

        if baseline:
            print(
                "ПРЕДУПРЕЖДЕНИЕ: baseline не пустой. "
                "Для вашей ранее измеренной карты ожидался пустой baseline."
            )

        input("\nНажмите Enter, чтобы начать тесты...")

        for i, test in enumerate(tests, start=1):
            print(f"\nТЕСТ {i}/{len(tests)}")
            rec = capture_test(
                board=board,
                test=test,
                key_pairs=key_pairs,
                baseline=baseline,
                timeout=args.timeout,
                stable_required=args.stable_scans,
                sample_count=args.samples,
                poll=args.poll,
            )
            output["results"].append(rec)
            save_json(json_path, output)
            save_csv(csv_path, output["results"])

            if rec["status"] == "not_detected":
                choice = input("[R] повторить / [S] пропустить / [Q] закончить: ").strip().lower()
                if choice.startswith("q"):
                    break
                if choice.startswith("r"):
                    # Repeat same test once immediately.
                    rec2 = capture_test(
                        board=board,
                        test=test,
                        key_pairs=key_pairs,
                        baseline=baseline,
                        timeout=args.timeout,
                        stable_required=args.stable_scans,
                        sample_count=args.samples,
                        poll=args.poll,
                    )
                    rec2["repeat_of_test"] = i
                    output["results"].append(rec2)
                    save_json(json_path, output)
                    save_csv(csv_path, output["results"])

        confirmed = sum(
            1 for r in output["results"]
            if r.get("status") == "captured" and r.get("expected_ghost_seen")
        )
        captured = sum(1 for r in output["results"] if r.get("status") == "captured")

        print("\n" + "=" * 78)
        print("ГОТОВО")
        print(f"Успешно снято тестов: {captured}")
        print(f"Ghost ambiguity подтверждена: {confirmed}")
        print(f"JSON: {json_path.resolve()}")
        print(f"CSV:  {csv_path.resolve()}")
        print("\nДля дальнейшего анализа пришлите JSON-файл.")

        return 0

    except KeyboardInterrupt:
        print("\n\nОстановлено пользователем.")
        save_json(json_path, output)
        save_csv(csv_path, output["results"])
        print(f"Частичный JSON сохранён: {json_path.resolve()}")
        return 130

    except Exception as exc:
        print(f"\nОШИБКА: {exc}")
        save_json(json_path, output)
        save_csv(csv_path, output["results"])
        print(f"Частичный JSON сохранён: {json_path.resolve()}")
        return 1

    finally:
        if board is not None:
            board.close()


if __name__ == "__main__":
    raise SystemExit(main())
