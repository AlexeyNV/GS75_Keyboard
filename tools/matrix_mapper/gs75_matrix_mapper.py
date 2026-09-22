#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
GS75 keyboard matrix mapper/analyzer.

Works with current STM32 firmware protocol:
  PING -> PONG
  DUMP -> MATRIX_BEGIN ... EDGE,... EDGE_COUNT,n ... MATRIX_END

Dependency:
  py -m pip install pyserial

Example:
  py gs75_matrix_mapper.py --port COM17 --plan gs75_key_plan.json
"""

from __future__ import annotations

import argparse
import csv
import json
import time
from collections import Counter
from datetime import datetime
from pathlib import Path
from typing import Dict, FrozenSet, Iterable, List, Optional, Sequence, Set, Tuple

import serial
from serial.tools import list_ports

Edge = Tuple[int, int]
EdgeSet = FrozenSet[Edge]


def edge_name(edge: Edge) -> str:
    return f"FPC{edge[0]:02d}->FPC{edge[1]:02d}"


def undirected_name(a: int, b: int) -> str:
    x, y = sorted((a, b))
    return f"FPC{x:02d}<->FPC{y:02d}"


def edge_set_to_strings(edges: Iterable[Edge]) -> List[str]:
    return [edge_name(e) for e in sorted(edges)]


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
        raw = input("Выберите номер порта: ").strip()
        try:
            idx = int(raw)
            if 1 <= idx <= len(ports):
                return ports[idx - 1].device
        except ValueError:
            pass
        print("Введите номер из списка.")


def load_plan(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    if "keys" not in data or not isinstance(data["keys"], list):
        raise ValueError("В plan-файле должен быть список 'keys'.")
    data.setdefault("combos", [])
    return data


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
                f"ПРЕДУПРЕЖДЕНИЕ: EDGE_COUNT={reported_count}, "
                f"но разобрано уникальных EDGE={len(edges)}."
            )

        return frozenset(edges)


def most_common_state(states: Sequence[EdgeSet]) -> Tuple[EdgeSet, int]:
    state, count = Counter(states).most_common(1)[0]
    return state, count


def format_state(edges: EdgeSet, baseline: EdgeSet = frozenset()) -> str:
    active = frozenset(set(edges) - set(baseline))
    if not active:
        return "нет активных связей"
    return ", ".join(edge_set_to_strings(active))


def wait_for_state_change(
    board: Board,
    baseline: EdgeSet,
    timeout: float,
    stable_required: int,
    poll_interval: float,
) -> Optional[EdgeSet]:
    deadline = time.monotonic() + timeout
    candidate: Optional[EdgeSet] = None
    stable = 0

    while time.monotonic() < deadline:
        state = board.dump()

        if state != baseline:
            if state == candidate:
                stable += 1
            else:
                candidate = state
                stable = 1

            if stable >= stable_required:
                return state
        else:
            candidate = None
            stable = 0

        time.sleep(poll_interval)

    return None


def wait_for_release(
    board: Board,
    baseline: EdgeSet,
    timeout: float,
    stable_required: int,
    poll_interval: float,
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

        time.sleep(poll_interval)

    return False


def capture_action(
    board: Board,
    label: str,
    baseline: EdgeSet,
    timeout: float,
    stable_required: int,
    capture_samples: int,
    poll_interval: float,
) -> Optional[dict]:
    while True:
        print()
        print("=" * 72)
        print(f"Нажмите и УДЕРЖИВАЙТЕ:  {label}")
        print("Скрипт сам обнаружит нажатие. После фиксации отпустите клавишу(и).")
        print("Ctrl+C — завершить программу с сохранением уже собранных данных.")

        detected = wait_for_state_change(
            board,
            baseline,
            timeout,
            stable_required,
            poll_interval,
        )

        if detected is None:
            print(f"Нажатие '{label}' не обнаружено за {timeout:.0f} с.")
            choice = input("[R] повторить / [S] пропустить: ").strip().lower()
            if choice.startswith("s"):
                return None
            continue

        samples: List[EdgeSet] = [detected]
        for _ in range(max(0, capture_samples - 1)):
            time.sleep(poll_interval)
            samples.append(board.dump())

        chosen, votes = most_common_state(samples)
        print(f"Зафиксировано ({votes}/{len(samples)} одинаковых снимков):")
        print("  " + format_state(chosen, baseline))

        print("Теперь ОТПУСТИТЕ клавишу(и)...")
        released = wait_for_release(
            board,
            baseline,
            timeout,
            stable_required,
            poll_interval,
        )
        if not released:
            print("ПРЕДУПРЕЖДЕНИЕ: возврат к baseline не подтверждён.")

        return {
            "observed_edges": edge_set_to_strings(chosen),
            "active_edges": edge_set_to_strings(
                frozenset(set(chosen) - set(baseline))
            ),
            "sample_count": len(samples),
            "matching_samples": votes,
            "release_confirmed": released,
        }


def parse_edge_string(text: str) -> Edge:
    left, right = text.split("->")
    return (int(left.replace("FPC", "")), int(right.replace("FPC", "")))


def directed_edges_from_record(record: dict) -> EdgeSet:
    return frozenset(parse_edge_string(s) for s in record.get("active_edges", []))


def physical_pair_from_single(record: dict) -> Optional[Tuple[int, int]]:
    edges = directed_edges_from_record(record)
    nodes = sorted({n for e in edges for n in e})
    if len(nodes) != 2:
        return None
    a, b = nodes
    if (a, b) in edges and (b, a) in edges:
        return (a, b)
    return None


def transitive_closure_from_pairs(pairs: Iterable[Tuple[int, int]]) -> EdgeSet:
    adjacency: Dict[int, Set[int]] = {}
    for a, b in pairs:
        adjacency.setdefault(a, set()).add(b)
        adjacency.setdefault(b, set()).add(a)

    visited: Set[int] = set()
    result: Set[Edge] = set()

    for start in adjacency:
        if start in visited:
            continue

        stack = [start]
        component: Set[int] = set()

        while stack:
            node = stack.pop()
            if node in component:
                continue
            component.add(node)
            visited.add(node)
            stack.extend(adjacency.get(node, ()))

        for src in component:
            for dst in component:
                if src != dst:
                    result.add((src, dst))

    return frozenset(result)


def analyze_combo(
    combo_keys: Sequence[str],
    singles: Dict[str, dict],
    actual_record: dict,
) -> dict:
    pairs: List[Tuple[int, int]] = []
    unavailable: List[str] = []

    for key in combo_keys:
        rec = singles.get(key)
        if not rec:
            unavailable.append(key)
            continue
        pair = physical_pair_from_single(rec)
        if pair is None:
            unavailable.append(key)
        else:
            pairs.append(pair)

    actual = directed_edges_from_record(actual_record)

    if unavailable:
        return {
            "status": "cannot_predict",
            "unavailable_single_keys": unavailable,
            "actual_edges": edge_set_to_strings(actual),
        }

    expected = transitive_closure_from_pairs(pairs)
    extra = frozenset(set(actual) - set(expected))
    missing = frozenset(set(expected) - set(actual))

    return {
        "status": "match" if not extra and not missing else "mismatch",
        "physical_pairs": [undirected_name(a, b) for a, b in pairs],
        "expected_edges": edge_set_to_strings(expected),
        "actual_edges": edge_set_to_strings(actual),
        "extra_edges": edge_set_to_strings(extra),
        "missing_edges": edge_set_to_strings(missing),
    }


def save_csvs(base_path: Path, data: dict) -> Tuple[Path, Path]:
    singles_csv = base_path.with_name(base_path.stem + "_singles.csv")
    combos_csv = base_path.with_name(base_path.stem + "_combos.csv")

    with singles_csv.open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(
            ["key", "status", "physical_pair", "active_edge_count", "active_edges"]
        )
        for key in data["plan"]["keys"]:
            rec = data["singles"].get(key)
            if rec is None:
                w.writerow([key, "skipped", "", "", ""])
                continue
            pair = physical_pair_from_single(rec)
            pair_text = undirected_name(*pair) if pair else ""
            edges = rec.get("active_edges", [])
            w.writerow([key, "captured", pair_text, len(edges), " | ".join(edges)])

    with combos_csv.open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(
            [
                "combo",
                "status",
                "actual_edge_count",
                "expected_edge_count",
                "extra_edges",
                "missing_edges",
                "actual_edges",
            ]
        )
        for combo_name, rec in data["combos"].items():
            analysis = rec.get("analysis", {})
            actual = analysis.get("actual_edges", rec.get("active_edges", []))
            expected = analysis.get("expected_edges", [])
            w.writerow(
                [
                    combo_name,
                    analysis.get("status", "captured"),
                    len(actual),
                    len(expected),
                    " | ".join(analysis.get("extra_edges", [])),
                    " | ".join(analysis.get("missing_edges", [])),
                    " | ".join(actual),
                ]
            )

    return singles_csv, combos_csv


def save_result(output_path: Path, data: dict) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Interactive GS75 keyboard matrix mapper/analyzer"
    )
    parser.add_argument("--port", help="COM-порт, например COM17")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--plan",
        type=Path,
        default=Path(__file__).with_name("gs75_key_plan.json"),
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("matrix_results"),
    )
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--stable-scans", type=int, default=3)
    parser.add_argument("--capture-samples", type=int, default=5)
    parser.add_argument("--poll", type=float, default=0.05)
    parser.add_argument("--baseline-samples", type=int, default=10)
    parser.add_argument("--skip-combos", action="store_true")
    args = parser.parse_args()

    plan = load_plan(args.plan)
    port = choose_port(args.port)

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_path = args.output_dir / f"gs75_matrix_{stamp}.json"

    result = {
        "format_version": 1,
        "created_local": datetime.now().isoformat(timespec="seconds"),
        "serial": {"port": port, "baud": args.baud},
        "plan": plan,
        "baseline": {},
        "singles": {},
        "combos": {},
    }

    board: Optional[Board] = None

    try:
        print(f"\nОткрываю {port} @ {args.baud}...")
        board = Board(port, args.baud)

        if not board.ping():
            print(
                "ОШИБКА: PONG не получен. Закройте PuTTY/miniterm и проверьте "
                "порт/прошивку."
            )
            return 2

        print("UART: PONG получен.")

        print("\nBaseline: ничего не нажимайте.")
        baseline_samples: List[EdgeSet] = []
        for i in range(args.baseline_samples):
            state = board.dump()
            baseline_samples.append(state)
            print(
                f"  baseline {i+1:02d}/{args.baseline_samples}: "
                f"{len(state)} directed edge(s)"
            )
            time.sleep(args.poll)

        baseline, baseline_votes = most_common_state(baseline_samples)
        result["baseline"] = {
            "edges": edge_set_to_strings(baseline),
            "matching_samples": baseline_votes,
            "sample_count": len(baseline_samples),
        }

        print(
            f"Baseline выбран: {len(baseline)} edge(s), "
            f"{baseline_votes}/{len(baseline_samples)} одинаковых снимков."
        )

        input("\nНажмите Enter, чтобы начать карту ОДИНОЧНЫХ клавиш...")

        for idx, key in enumerate(plan["keys"], start=1):
            print(f"\n[{idx}/{len(plan['keys'])}] {key}")
            rec = capture_action(
                board,
                key,
                baseline,
                args.timeout,
                args.stable_scans,
                args.capture_samples,
                args.poll,
            )
            if rec is not None:
                result["singles"][key] = rec
            save_result(output_path, result)

        if not args.skip_combos and plan.get("combos"):
            input("\nОдиночные клавиши завершены. Enter — начать КОМБИНАЦИИ...")

            for idx, combo in enumerate(plan["combos"], start=1):
                if not isinstance(combo, list) or len(combo) < 2:
                    continue

                label = " + ".join(combo)
                print(f"\n[combo {idx}/{len(plan['combos'])}] {label}")

                rec = capture_action(
                    board,
                    label,
                    baseline,
                    args.timeout,
                    args.stable_scans,
                    args.capture_samples,
                    args.poll,
                )

                if rec is not None:
                    rec["keys"] = combo
                    rec["analysis"] = analyze_combo(combo, result["singles"], rec)
                    result["combos"][label] = rec

                    analysis = rec["analysis"]
                    if analysis["status"] == "match":
                        print("Анализ: MATCH — связность совпадает с ожидаемой.")
                    elif analysis["status"] == "mismatch":
                        print("Анализ: MISMATCH")
                        if analysis.get("extra_edges"):
                            print("  EXTRA:   " + ", ".join(analysis["extra_edges"]))
                        if analysis.get("missing_edges"):
                            print("  MISSING: " + ", ".join(analysis["missing_edges"]))
                    else:
                        print(
                            "Анализ: нельзя построить прогноз — нет однозначной "
                            "карты одиночной клавиши."
                        )

                save_result(output_path, result)

        save_result(output_path, result)
        singles_csv, combos_csv = save_csvs(output_path, result)

        print("\n" + "=" * 72)
        print("ГОТОВО")
        print(f"JSON: {output_path.resolve()}")
        print(f"CSV одиночных: {singles_csv.resolve()}")
        print(f"CSV комбинаций: {combos_csv.resolve()}")
        print("\nДля анализа лучше прислать JSON.")
        return 0

    except KeyboardInterrupt:
        print("\n\nОстановлено пользователем.")
        save_result(output_path, result)
        print(f"Частичный результат сохранён: {output_path.resolve()}")
        return 130

    except Exception as exc:
        print(f"\nОШИБКА: {exc}")
        save_result(output_path, result)
        print(f"Частичный результат сохранён: {output_path.resolve()}")
        return 1

    finally:
        if board is not None:
            board.close()


if __name__ == "__main__":
    raise SystemExit(main())
