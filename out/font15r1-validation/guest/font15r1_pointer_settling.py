"""Bounded, read-only settling observations for the FONT15 QMP helper.

The observer sends one logical relative movement and then only requests fresh
screenshots.  It never retries or repairs pointer input.
"""

from __future__ import annotations

import json
import math
import re
import uuid
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Protocol


MAX_IMAGE_PIXELS = 5_000_000
MAX_CURSOR_CANDIDATES = 100_000
MAX_SETTLE_SCREENSHOTS = 32
MAX_MINIMUM_INTERVAL_SECONDS = 5.0
MAX_SETTLE_SECONDS = 30.0


class QmpSession(Protocol):
    clock: Callable[[], float]
    sleeper: Callable[[float], None]

    def relative(self, dx: int, dy: int, delay: float, combined: bool = False, *, deadline: float | None = None) -> None: ...
    def screenshot(self, path: Path, *, deadline: float | None = None) -> dict[str, Any]: ...
    def verify_ownership(self, *, deadline: float | None = None) -> None: ...
    def _record(self, record: dict[str, Any]) -> None: ...


class PpmReadError(ValueError):
    """A P6 screenshot could not be parsed or exceeds the bounded scan size."""


class CursorMeasurementError(ValueError):
    """The screenshot does not contain one sufficiently clear cursor match."""


class AmbiguousCursorError(CursorMeasurementError):
    """Multiple equally strong pointer matches were found."""


class SettleStatus:
    EXPECTED_POSITION_OBSERVED = "EXPECTED_POSITION_OBSERVED"
    EXPECTED_CLAMPED_POSITION_OBSERVED = "EXPECTED_CLAMPED_POSITION_OBSERVED"
    PARTIAL_DISPLACEMENT_AT_DEADLINE = "PARTIAL_DISPLACEMENT_AT_DEADLINE"
    NO_DISPLACEMENT_AT_DEADLINE = "NO_DISPLACEMENT_AT_DEADLINE"
    PARTIAL_DISPLACEMENT_SCREENSHOT_LIMIT = "PARTIAL_DISPLACEMENT_SCREENSHOT_LIMIT"
    NO_DISPLACEMENT_SCREENSHOT_LIMIT = "NO_DISPLACEMENT_SCREENSHOT_LIMIT"
    CURSOR_MEASUREMENT_UNAVAILABLE = "CURSOR_MEASUREMENT_UNAVAILABLE"
    AMBIGUOUS_CURSOR_LOCATION = "AMBIGUOUS_CURSOR_LOCATION"
    SCREENSHOT_INCOMPLETE_OR_INVALID = "SCREENSHOT_INCOMPLETE_OR_INVALID"
    SCREENSHOT_STALE_OR_REUSED = "SCREENSHOT_STALE_OR_REUSED"
    VERIFIED_ENDPOINT_OWNERSHIP_LOST = "VERIFIED_ENDPOINT_OWNERSHIP_LOST"
    QMP_FAILURE = "QMP_FAILURE"
    OBSERVATION_DEADLINE_EXCEEDED = "OBSERVATION_DEADLINE_EXCEEDED"
    UNEXPECTED_OVERSHOOT = "UNEXPECTED_OVERSHOOT"
    UNEXPECTED_POSITION_MISMATCH = "UNEXPECTED_POSITION_MISMATCH"
    TARGET_REGION_NOT_REACHED = "TARGET_REGION_NOT_REACHED"


@dataclass(frozen=True)
class CursorTemplate:
    width: int
    height: int
    pixels: tuple[tuple[int, int, tuple[int, int, int]], ...]

    @classmethod
    def from_cpp_source(cls, path: Path) -> "CursorTemplate":
        source = path.read_text(encoding="utf-8", errors="replace")
        match = re.search(
            r"s_cursorBitmap\s*\[\s*(\d+)\s*\]\s*\[\s*(\d+)\s*\]\s*=\s*\{(.*?)\};",
            source,
            re.DOTALL,
        )
        if match is None:
            raise ValueError(f"s_cursorBitmap was not found in {path}")
        height, width = int(match.group(1)), int(match.group(2))
        rows = re.findall(r"\{([^{}]+)\}", match.group(3))
        values = [[int(value.strip()) for value in row.split(",") if value.strip()] for row in rows]
        if len(values) != height or any(len(row) != width for row in values):
            raise ValueError("s_cursorBitmap dimensions do not match its initializer")
        if any(value not in (0, 1, 2) for row in values for value in row):
            raise ValueError("s_cursorBitmap contains an unsupported color index")
        palette = {1: (0, 0, 0), 2: (255, 255, 255)}
        pixels = tuple(
            (x, y, palette[value])
            for y, row in enumerate(values)
            for x, value in enumerate(row)
            if value
        )
        if not pixels:
            raise ValueError("s_cursorBitmap has no opaque cursor pixels")
        return cls(width=width, height=height, pixels=pixels)


@dataclass(frozen=True)
class PpmImage:
    width: int
    height: int
    pixels: bytes

    @classmethod
    def read(cls, path: Path) -> "PpmImage":
        data = path.read_bytes()
        offset = 0
        whitespace = b" \t\r\n\v\f"

        def token() -> bytes:
            nonlocal offset
            while offset < len(data):
                if data[offset] in whitespace:
                    offset += 1
                elif data[offset] == ord("#"):
                    end = data.find(b"\n", offset)
                    if end < 0:
                        raise PpmReadError("unterminated PPM comment")
                    offset = end + 1
                else:
                    break
            start = offset
            while offset < len(data) and data[offset] not in whitespace + b"#":
                offset += 1
            if start == offset:
                raise PpmReadError("missing PPM header token")
            return data[start:offset]

        if token() != b"P6":
            raise PpmReadError("expected a binary P6 PPM image")
        try:
            width, height, maximum = int(token()), int(token()), int(token())
        except ValueError as exc:
            raise PpmReadError("invalid PPM dimensions or maximum value") from exc
        if width <= 0 or height <= 0 or maximum != 255:
            raise PpmReadError("PPM must have positive dimensions and max value 255")
        if width * height > MAX_IMAGE_PIXELS:
            raise PpmReadError("PPM exceeds the bounded cursor-scan pixel limit")
        if offset >= len(data) or data[offset] not in whitespace:
            raise PpmReadError("PPM header has no raster separator")
        if data[offset : offset + 2] == b"\r\n":
            offset += 2
        else:
            offset += 1
        raster = data[offset:]
        expected_bytes = width * height * 3
        if len(raster) != expected_bytes:
            raise PpmReadError(
                f"PPM raster is {len(raster)} bytes; expected exactly {expected_bytes}"
            )
        return cls(width, height, raster)

    def pixel(self, x: int, y: int) -> tuple[int, int, int]:
        offset = (y * self.width + x) * 3
        return self.pixels[offset], self.pixels[offset + 1], self.pixels[offset + 2]


@dataclass(frozen=True)
class CursorMeasurement:
    x: int
    y: int
    matching_pixels: int
    visible_pixels: int
    runner_up_pixels: int

    def as_dict(self) -> dict[str, int]:
        return asdict(self)


def locate_cursor(
    image: PpmImage,
    template: CursorTemplate,
    *,
    max_candidates: int = MAX_CURSOR_CANDIDATES,
) -> CursorMeasurement:
    """Find a unique exact match across the framebuffer, including clipped edges."""
    candidates: list[tuple[int, int, int, int]] = []
    evaluated = 0
    template_colors = {(x, y): color for x, y, color in template.pixels}
    anchor_pixels = tuple(
        (x, y, template_colors[(x, y)])
        for x, y in ((0, 0), (1, 1), (1, 2))
        if (x, y) in template_colors
    )
    primary_x, primary_y, primary_color = template.pixels[0]
    primary_bytes = bytes(primary_color)
    offset = 0
    while True:
        found = image.pixels.find(primary_bytes, offset)
        if found < 0:
            break
        # Keep the search aligned to RGB pixel boundaries. An unaligned match
        # advances by one byte so it cannot skip the next aligned pixel.
        offset = found + (3 if found % 3 == 0 else 1)
        if found % 3:
            continue
        pixel_index = found // 3
        base_y, base_x = divmod(pixel_index, image.width)
        x, y = base_x - primary_x, base_y - primary_y
        if x < 0 or y < 0 or x >= image.width or y >= image.height:
            continue
        if x + template.width <= image.width and y + template.height <= image.height:
            visible = template.pixels
        else:
            visible = tuple(
                (px, py, color)
                for px, py, color in template.pixels
                if x + px < image.width and y + py < image.height
            )
            if len(visible) < 12:
                continue
        anchors = tuple(
            (ax, ay, color)
            for ax, ay, color in anchor_pixels
            if x + ax < image.width and y + ay < image.height
        )
        if any(image.pixel(x + ax, y + ay) != color for ax, ay, color in anchors):
            continue
        evaluated += 1
        if evaluated > max_candidates:
            raise AmbiguousCursorError("cursor search exceeded the bounded candidate count")
        matching = sum(
            image.pixel(x + px, y + py) == color for px, py, color in visible
        )
        if matching == len(visible):
            candidates.append((matching, len(visible), x, y))

    if not candidates:
        raise CursorMeasurementError("no exact cursor template match was found")
    candidates.sort(key=lambda item: (item[0] / item[1], item[0]), reverse=True)
    best = candidates[0]
    runner_up = candidates[1][0] if len(candidates) > 1 else 0
    if len(candidates) > 1:
        second = candidates[1]
        if abs(best[0] / best[1] - second[0] / second[1]) < 1e-9:
            raise AmbiguousCursorError(
                f"multiple exact cursor matches were found at {(best[2], best[3])} and {(second[2], second[3])}"
            )
    return CursorMeasurement(best[2], best[3], best[0], best[1], runner_up)


@dataclass
class SettleObservation:
    index: int
    started_monotonic: float
    started_utc: str
    completed_monotonic: float | None = None
    completed_utc: str | None = None
    screenshot_path: str | None = None
    screenshot_fresh: bool = False
    screenshot_size_bytes: int | None = None
    image_width: int | None = None
    image_height: int | None = None
    cursor: dict[str, int] | None = None
    error_type: str | None = None
    error: str | None = None


@dataclass
class PointerSettleResult:
    status: str
    initial_position: tuple[int, int]
    requested_delta: tuple[int, int]
    expected_position: tuple[int, int] | None = None
    observed_position: tuple[int, int] | None = None
    clipped: bool = False
    click_authorized: bool = False
    movement_operations: int = 0
    observations: list[SettleObservation] = field(default_factory=list)
    started_monotonic: float = 0.0
    finished_monotonic: float = 0.0
    deadline_monotonic: float = 0.0
    detail: str | None = None

    def as_dict(self) -> dict[str, Any]:
        result = asdict(self)
        result["initial_position"] = list(self.initial_position)
        result["requested_delta"] = list(self.requested_delta)
        result["expected_position"] = list(self.expected_position) if self.expected_position is not None else None
        result["observed_position"] = list(self.observed_position) if self.observed_position is not None else None
        return result


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def _error_name(error: BaseException) -> str:
    return type(error).__name__


def _ownership_error(error: BaseException) -> bool:
    return _error_name(error) in {"EndpointIdentityError", "ConnectionOwnershipError"}


def _qmp_error(error: BaseException) -> bool:
    return _error_name(error).startswith("QMP") or _error_name(error) in {
        "TimeoutError",
        "OSError",
        "socket.timeout",
    }


def _contains(region: tuple[int, int, int, int] | None, point: tuple[int, int]) -> bool:
    if region is None:
        return False
    left, top, right, bottom = region
    return left <= point[0] <= right and top <= point[1] <= bottom


def _at_or_past_deadline(clock: Callable[[], float], deadline: float) -> bool:
    return clock() >= deadline


def _time_left(clock: Callable[[], float], deadline: float) -> float:
    return max(0.0, deadline - clock())


def _classify_incomplete(
    initial: tuple[int, int],
    observed: tuple[int, int] | None,
    *,
    deadline: bool,
    tolerance: int = 0,
) -> str:
    if observed is None or (
        abs(observed[0] - initial[0]) <= tolerance
        and abs(observed[1] - initial[1]) <= tolerance
    ):
        return (
            SettleStatus.NO_DISPLACEMENT_AT_DEADLINE
            if deadline
            else SettleStatus.NO_DISPLACEMENT_SCREENSHOT_LIMIT
        )
    return (
        SettleStatus.PARTIAL_DISPLACEMENT_AT_DEADLINE
        if deadline
        else SettleStatus.PARTIAL_DISPLACEMENT_SCREENSHOT_LIMIT
    )


def settle_pointer(
    qmp: QmpSession,
    *,
    initial_position: tuple[int, int],
    dx: int,
    dy: int,
    cursor_template: CursorTemplate,
    screenshot_directory: Path,
    deadline: float,
    max_screenshots: int = 8,
    min_interval: float = 0.25,
    tolerance: int = 1,
    delay: float = 0.2,
    target_region: tuple[int, int, int, int] | None = None,
) -> PointerSettleResult:
    """Move once, then observe fresh screenshots until bounded settlement.

    ``target_region`` is an inclusive (left, top, right, bottom) rectangle. The
    returned ``click_authorized`` flag is true only when the measured pointer
    also lies inside that caller-supplied target rectangle.
    """
    if max_screenshots < 1 or max_screenshots > MAX_SETTLE_SCREENSHOTS:
        raise ValueError(f"max_screenshots must be in [1, {MAX_SETTLE_SCREENSHOTS}]")
    if not math.isfinite(min_interval) or not 0 < min_interval <= MAX_MINIMUM_INTERVAL_SECONDS:
        raise ValueError(
            f"min_interval must be greater than 0 and no more than {MAX_MINIMUM_INTERVAL_SECONDS:g} seconds"
        )
    if tolerance < 0 or (abs(dx) + 34) // 35 + (abs(dy) + 34) // 35 > 128:
        raise ValueError("coordinate tolerance must be nonnegative and movement must be bounded")
    if target_region is not None:
        left, top, right, bottom = target_region
        if left < 0 or top < 0 or right < left or bottom < top:
            raise ValueError("target_region must be an inclusive nonnegative rectangle")
    if deadline <= qmp.clock() or deadline - qmp.clock() > MAX_SETTLE_SECONDS:
        raise ValueError(f"deadline must be future and within {MAX_SETTLE_SECONDS:g} seconds")
    if not screenshot_directory.is_dir():
        raise ValueError(f"screenshot directory does not exist: {screenshot_directory}")
    if len(initial_position) != 2 or min(initial_position) < 0:
        raise ValueError("initial_position must contain nonnegative x and y coordinates")

    clock = qmp.clock
    sleeper = qmp.sleeper
    started = clock()
    result = PointerSettleResult(
        status=SettleStatus.QMP_FAILURE,
        initial_position=tuple(map(int, initial_position)),
        requested_delta=(int(dx), int(dy)),
        started_monotonic=started,
        finished_monotonic=started,
        deadline_monotonic=float(deadline),
    )
    run_id = uuid.uuid4().hex
    expected: tuple[int, int] | None = None
    clipped = False
    last_observation_start: float | None = None
    last_position: tuple[int, int] | None = None
    qmp._record(
        {
            "kind": "pointer-settle-start",
            "run_id": run_id,
            "initial_position": list(result.initial_position),
            "requested_delta": [dx, dy],
            "deadline_monotonic": deadline,
            "max_screenshots": max_screenshots,
            "min_interval_seconds": min_interval,
            "coordinate_tolerance": tolerance,
            "target_region": list(target_region) if target_region else None,
            "issued_at_utc": _utc_now(),
        }
    )

    def finish(status: str, detail: str | None = None) -> PointerSettleResult:
        result.status = status
        result.expected_position = expected
        result.observed_position = last_position
        result.clipped = clipped
        result.click_authorized = bool(
            status in {
                SettleStatus.EXPECTED_POSITION_OBSERVED,
                SettleStatus.EXPECTED_CLAMPED_POSITION_OBSERVED,
            }
            and expected is not None
            and last_position is not None
            and _contains(target_region, last_position)
        )
        result.finished_monotonic = clock()
        result.detail = detail
        qmp._record({"kind": "pointer-settle-result", "run_id": run_id, **result.as_dict()})
        return result

    if _at_or_past_deadline(clock, deadline):
        return finish(SettleStatus.OBSERVATION_DEADLINE_EXCEEDED, "deadline expired before movement")
    try:
        qmp.verify_ownership(deadline=deadline)
        qmp._record(
            {
                "kind": "pointer-settle-movement-start",
                "run_id": run_id,
                "dx": dx,
                "dy": dy,
                "logical_movement_operations": 1,
            }
        )
        qmp.relative(dx, dy, delay, deadline=deadline)
        result.movement_operations = 1
        qmp._record(
            {
                "kind": "pointer-settle-movement-acknowledged",
                "run_id": run_id,
                "logical_movement_operations": 1,
                "acknowledged_at_utc": _utc_now(),
            }
        )
    except Exception as exc:
        if _ownership_error(exc):
            return finish(SettleStatus.VERIFIED_ENDPOINT_OWNERSHIP_LOST, str(exc))
        if _at_or_past_deadline(clock, deadline):
            return finish(SettleStatus.OBSERVATION_DEADLINE_EXCEEDED, str(exc))
        return finish(SettleStatus.QMP_FAILURE, f"movement failed: {exc}")

    for index in range(1, max_screenshots + 1):
        if _at_or_past_deadline(clock, deadline):
            return finish(
                _classify_incomplete(
                    result.initial_position, last_position, deadline=True, tolerance=tolerance
                ),
                "observation deadline reached after the last valid frame",
            )
        if last_observation_start is not None:
            wait = min(
                max(0.0, last_observation_start + min_interval - clock()),
                _time_left(clock, deadline),
            )
            if wait:
                sleeper(wait)
            if _at_or_past_deadline(clock, deadline):
                return finish(
                    _classify_incomplete(
                        result.initial_position, last_position, deadline=True, tolerance=tolerance
                    ),
                    "deadline reached during the bounded inter-capture interval",
                )

        observation = SettleObservation(
            index=index, started_monotonic=clock(), started_utc=_utc_now()
        )
        result.observations.append(observation)
        last_observation_start = observation.started_monotonic
        path = screenshot_directory / f"pointer-settle-{run_id}-{index:02d}.ppm"
        observation.screenshot_path = str(path)
        try:
            if path.exists() or path.is_symlink():
                observation.error_type = "ScreenshotFreshnessError"
                observation.error = "fresh screenshot destination already exists"
                return finish(SettleStatus.SCREENSHOT_STALE_OR_REUSED, observation.error)
            qmp.verify_ownership(deadline=deadline)
            capture = qmp.screenshot(path, deadline=deadline)
            observation.completed_monotonic = clock()
            observation.completed_utc = _utc_now()
            if (
                not isinstance(capture, dict)
                or capture.get("fresh_capture") is not True
                or Path(str(capture.get("path", ""))).resolve() != path.resolve()
                or not isinstance(capture.get("request_monotonic"), (int, float))
                or not isinstance(capture.get("artifact_verified_monotonic"), (int, float))
                or capture["request_monotonic"] < observation.started_monotonic
                or capture["artifact_verified_monotonic"] < capture["request_monotonic"]
            ):
                observation.error_type = "ScreenshotFreshnessError"
                observation.error = "capture did not prove a new artifact completed after this request"
                return finish(SettleStatus.SCREENSHOT_STALE_OR_REUSED, observation.error)
            if capture["artifact_verified_monotonic"] > deadline or clock() > deadline:
                observation.error_type = "QMPTimeout"
                observation.error = "screenshot completed after the observation deadline"
                return finish(SettleStatus.OBSERVATION_DEADLINE_EXCEEDED, observation.error)
            observation.screenshot_fresh = True
            observation.screenshot_size_bytes = int(capture["size_bytes"])
            image = PpmImage.read(path)
            observation.image_width = image.width
            observation.image_height = image.height
            measurement = locate_cursor(image, cursor_template)
            observation.cursor = measurement.as_dict()
            last_position = (measurement.x, measurement.y)
            result.observed_position = last_position
            # Recheck ownership after the frame so a stale or replaced endpoint
            # cannot authorize a follow-up click.
            qmp.verify_ownership(deadline=deadline)
        except Exception as exc:
            observation.completed_monotonic = clock()
            observation.completed_utc = _utc_now()
            observation.error_type = _error_name(exc)
            observation.error = str(exc)
            if _ownership_error(exc):
                return finish(SettleStatus.VERIFIED_ENDPOINT_OWNERSHIP_LOST, str(exc))
            if isinstance(exc, AmbiguousCursorError):
                return finish(SettleStatus.AMBIGUOUS_CURSOR_LOCATION, str(exc))
            if isinstance(exc, CursorMeasurementError):
                return finish(SettleStatus.CURSOR_MEASUREMENT_UNAVAILABLE, str(exc))
            if _at_or_past_deadline(clock, deadline):
                return finish(SettleStatus.OBSERVATION_DEADLINE_EXCEEDED, str(exc))
            if isinstance(exc, (PpmReadError, OSError)) or _error_name(exc) in {
                "ScreenshotArtifactError",
                "ScreenshotFreshnessError",
            }:
                return finish(SettleStatus.SCREENSHOT_INCOMPLETE_OR_INVALID, str(exc))
            if _qmp_error(exc):
                return finish(SettleStatus.QMP_FAILURE, str(exc))
            return finish(SettleStatus.QMP_FAILURE, f"observation failed: {exc}")

        if expected is None:
            if not (0 <= result.initial_position[0] < image.width and 0 <= result.initial_position[1] < image.height):
                return finish(
                    SettleStatus.CURSOR_MEASUREMENT_UNAVAILABLE,
                    "verified initial position is outside the captured framebuffer",
                )
            raw = (result.initial_position[0] + dx, result.initial_position[1] + dy)
            expected = (
                min(image.width - 1, max(0, raw[0])),
                min(image.height - 1, max(0, raw[1])),
            )
            clipped = expected != raw
            result.expected_position = expected
            result.clipped = clipped

        if clock() > deadline:
            return finish(SettleStatus.OBSERVATION_DEADLINE_EXCEEDED, "cursor measurement completed after the deadline")
        if (
            abs(last_position[0] - expected[0]) <= tolerance
            and abs(last_position[1] - expected[1]) <= tolerance
        ):
            status = (
                SettleStatus.EXPECTED_CLAMPED_POSITION_OBSERVED
                if clipped
                else SettleStatus.EXPECTED_POSITION_OBSERVED
            )
            if target_region is not None and not _contains(target_region, last_position):
                return finish(
                    SettleStatus.TARGET_REGION_NOT_REACHED,
                    "settled coordinate is outside the caller's target rectangle",
                )
            return finish(status)

        overshot = (dx < 0 and last_position[0] < expected[0] - tolerance) or (
            dx > 0 and last_position[0] > expected[0] + tolerance
        ) or (dy < 0 and last_position[1] < expected[1] - tolerance) or (
            dy > 0 and last_position[1] > expected[1] + tolerance
        )
        if overshot:
            return finish(
                SettleStatus.UNEXPECTED_OVERSHOOT,
                "measured cursor passed the expected clamped destination",
            )
        if (
            last_position[0] < min(result.initial_position[0], expected[0]) - tolerance
            or last_position[0] > max(result.initial_position[0], expected[0]) + tolerance
            or last_position[1] < min(result.initial_position[1], expected[1]) - tolerance
            or last_position[1] > max(result.initial_position[1], expected[1]) + tolerance
        ):
            return finish(
                SettleStatus.UNEXPECTED_POSITION_MISMATCH,
                "measured cursor is outside the clamped movement corridor",
            )
        if index == max_screenshots:
            if last_position != result.initial_position and (
                (dx == 0 or (last_position[0] - result.initial_position[0]) * dx >= 0)
                and (dy == 0 or (last_position[1] - result.initial_position[1]) * dy >= 0)
            ):
                return finish(
                    SettleStatus.PARTIAL_DISPLACEMENT_SCREENSHOT_LIMIT,
                    "screenshot limit reached with incomplete displacement",
                )
            if last_position == result.initial_position:
                return finish(
                    SettleStatus.NO_DISPLACEMENT_SCREENSHOT_LIMIT,
                    "screenshot limit reached without visible displacement",
                )
            return finish(
                SettleStatus.UNEXPECTED_POSITION_MISMATCH,
                "measured cursor is outside the expected movement path",
            )
        if _at_or_past_deadline(clock, deadline):
            return finish(
                _classify_incomplete(
                    result.initial_position, last_position, deadline=True, tolerance=tolerance
                )
            )

    return finish(
        _classify_incomplete(
            result.initial_position, last_position, deadline=True, tolerance=tolerance
        )
    )


def result_json(result: PointerSettleResult) -> str:
    return json.dumps(result.as_dict(), separators=(",", ":"), sort_keys=True)
