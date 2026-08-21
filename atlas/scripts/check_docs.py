#!/usr/bin/env python3
"""Validate Atlas content without external Python dependencies."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from urllib.parse import unquote


REPOSITORY = Path(__file__).resolve().parents[2]
ATLAS = REPOSITORY / "atlas"
DOCS = ATLAS / "docs"
MARKDOWN_FILES = [ATLAS / "README.md", *sorted(DOCS.rglob("*.md"))]
PROCEDURE_FILES = [
    *sorted((REPOSITORY / "comp").glob("*/gsw/procedures/*.ycs")),
    *sorted((REPOSITORY / "yamcs/src/main/yamcs/procedures").rglob("*.ycs")),
]
ASSET_EXTENSIONS = {".gif", ".jpeg", ".jpg", ".png", ".svg", ".webp"}
DASH_PUNCTUATION = "‐‑‒–—−"
LINK_PATTERN = re.compile(r"(!?)\[[^]]*\]\(([^)]+)\)")
HTML_IMAGE_PATTERN = re.compile(r"<img\s+[^>]*src=[\"']([^\"']+)[\"']", re.IGNORECASE)
INLINE_CODE_PATTERN = re.compile(r"`[^`]*`")
SENTENCE_BREAK_PATTERN = re.compile(r"[.!?][)\]\"'’”]*\s+(?=[A-Z`[(])")
ABBREVIATIONS = ("e.g.", "i.e.", "Mr.", "Mrs.", "Ms.", "Dr.", "vs.", "etc.")


def report(issues: list[str], path: Path, line_number: int | None, message: str) -> None:
    location = path.relative_to(REPOSITORY).as_posix()
    if line_number is not None:
        location += f":{line_number}"
    issues.append(f"{location}: {message}")


def strip_inline_code(line: str) -> str:
    return INLINE_CODE_PATTERN.sub("", line)


def heading_slug(heading: str) -> str:
    heading = re.sub(r"`([^`]*)`", r"\1", heading).lower().strip()
    heading = re.sub(r"[^\w\s-]", "", heading)
    return re.sub(r"[\s-]+", "-", heading).strip("-")


def document_anchors(path: Path) -> set[str]:
    anchors: set[str] = set()
    fenced = False
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.lstrip()
        if stripped.startswith(("```", "~~~")):
            fenced = not fenced
            continue
        if not fenced and stripped.startswith("#"):
            heading = re.sub(r"^#+\s+", "", stripped).strip()
            if heading:
                anchors.add(heading_slug(heading))
    return anchors


def resolve_link(path: Path, target: str) -> tuple[Path | None, str | None]:
    target = unquote(target.strip())
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1]
    if target.startswith(("http://", "https://", "mailto:")):
        return None, None
    file_part, separator, anchor = target.partition("#")
    destination = path if not file_part else (path.parent / file_part).resolve()
    return destination, anchor if separator else None


def check_markdown(issues: list[str]) -> set[Path]:
    referenced_assets: set[Path] = set()

    for path in MARKDOWN_FILES:
        fenced = False
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            stripped = line.lstrip()
            if stripped.startswith(("```", "~~~")):
                fenced = not fenced
                continue

            if re.search(r"!\[\s*\]\(", line):
                report(issues, path, line_number, "image alternative text must not be empty")

            for image_marker, raw_target in LINK_PATTERN.findall(line):
                target = raw_target.split(maxsplit=1)[0]
                destination, anchor = resolve_link(path, target)
                if destination is None:
                    continue
                if not destination.exists():
                    report(issues, path, line_number, f"local link does not exist: {target}")
                    continue
                if image_marker and destination.suffix.lower() in ASSET_EXTENSIONS:
                    referenced_assets.add(destination)
                    lowered = f"{line} {target}".lower()
                    if "shire" in lowered and "logo" in lowered:
                        report(issues, path, line_number, "unapproved SHIRE logo reference")
                if anchor and destination.suffix.lower() == ".md":
                    if heading_slug(anchor) not in document_anchors(destination):
                        report(issues, path, line_number, f"local heading does not exist: {target}")

            for raw_target in HTML_IMAGE_PATTERN.findall(line):
                destination, _ = resolve_link(path, raw_target)
                if destination is not None:
                    if not destination.exists():
                        report(issues, path, line_number, f"local image does not exist: {raw_target}")
                    else:
                        referenced_assets.add(destination)
            if "<img" in line.lower() and not re.search(r'alt=["\'][^"\']+["\']', line, re.IGNORECASE):
                report(issues, path, line_number, "HTML image alternative text must not be empty")

            if fenced:
                continue

            if re.match(r"^\s*-\s+", line):
                report(issues, path, line_number, "use * for unordered list markers")
            if re.fullmatch(r"\s*-{3,}\s*", line):
                report(issues, path, line_number, "use *** for horizontal rules")

            prose = strip_inline_code(line)
            if ";" in prose:
                report(issues, path, line_number, "avoid semicolons in prose")
            if any(character in prose for character in DASH_PUNCTUATION):
                report(issues, path, line_number, "avoid dash punctuation in prose")

            content = prose.strip()
            if not content or content.startswith("#"):
                continue
            if content.startswith("|"):
                if re.fullmatch(r"\|[ :|*+-]+\|", content):
                    continue
                endings = re.findall(r"[.!?](?=\s|\||$)", content)
                if len(endings) > 1:
                    report(issues, path, line_number, "table row contains more than one sentence")
                continue

            content = re.sub(r"^>\s*", "", content)
            content = re.sub(r"^(?:[*+] |\d+[.)] )", "", content)
            for match in SENTENCE_BREAK_PATTERN.finditer(content):
                prefix = content[: match.start() + 1].rstrip()
                if not prefix.endswith(ABBREVIATIONS):
                    report(issues, path, line_number, "start each prose sentence on its own line")
                    break

    config_text = (ATLAS / "zensical.toml").read_text(encoding="utf-8")
    for target in re.findall(r'"(assets/[^"?]+)"', config_text):
        referenced_assets.add((DOCS / target).resolve())

    return referenced_assets


def check_assets(issues: list[str], referenced_assets: set[Path]) -> None:
    for asset in sorted(DOCS.joinpath("assets").rglob("*")):
        if not asset.is_file() or asset.suffix.lower() not in ASSET_EXTENSIONS:
            continue
        lowered = asset.name.lower()
        if "shire" in lowered and "logo" in lowered:
            report(issues, asset, None, "unapproved SHIRE logo asset")
        if "screenshots" in asset.parts and not re.fullmatch(r"[a-z0-9_]+\.(?:gif|jpeg|jpg|png|webp)", lowered):
            report(issues, asset, None, "screenshot filename must use lowercase words separated by underscores")
        if asset.resolve() not in referenced_assets:
            report(issues, asset, None, "image asset is not referenced by Markdown or Zensical configuration")


def make_targets(makefile: Path) -> set[str]:
    targets: set[str] = set()
    for line in makefile.read_text(encoding="utf-8").splitlines():
        match = re.match(r"^([A-Za-z0-9_.-]+)\s*:(?!=)", line)
        if match and not match.group(1).startswith("."):
            targets.add(match.group(1))
    return targets


def check_make_commands(issues: list[str]) -> None:
    root_targets = make_targets(REPOSITORY / "Makefile")
    subsystem_targets = {
        name: make_targets(REPOSITORY / name / "Makefile")
        for name in ("simulith", "yamcs")
    }

    for path in MARKDOWN_FILES:
        fenced = False
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if line.lstrip().startswith(("```", "~~~")):
                fenced = not fenced
                continue
            if not fenced and "`make " not in line and "`cd " not in line:
                continue
            subsystem_match = re.search(r"cd\s+(simulith|yamcs)\s+&&\s+make\s+([A-Za-z0-9_.-]+)", line)
            if subsystem_match:
                subsystem, target = subsystem_match.groups()
                if target not in subsystem_targets[subsystem]:
                    report(issues, path, line_number, f"unknown {subsystem} Make target: {target}")
                continue
            for target in re.findall(r"(?<![-\w])make\s+([a-z][A-Za-z0-9_.-]*)", line):
                if target not in root_targets:
                    report(issues, path, line_number, f"unknown root Make target: {target}")


def check_procedures(issues: list[str]) -> None:
    for path in PROCEDURE_FILES:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            report(issues, path, None, f"invalid procedure JSON: {error}")
            continue
        if data.get("$schema") != "https://yamcs.org/schema/stack.schema.json":
            report(issues, path, None, "unexpected or missing YAMCS stack schema")
        if not isinstance(data.get("steps"), list) or not data["steps"]:
            report(issues, path, None, "procedure must contain at least one step")


def yaml_named_list(path: Path, key: str) -> list[str]:
    """Read a simple YAML list of name mappings without adding a YAML dependency."""
    lines = path.read_text(encoding="utf-8").splitlines()
    for index, line in enumerate(lines):
        match = re.match(rf"^(\s*){re.escape(key)}:\s*$", line)
        if not match:
            continue
        base_indent = len(match.group(1))
        names: list[str] = []
        for candidate in lines[index + 1 :]:
            if not candidate.strip() or candidate.lstrip().startswith("#"):
                continue
            indent = len(candidate) - len(candidate.lstrip())
            if indent <= base_indent:
                break
            name_match = re.match(r"\s*-\s+name:\s*[\"']?([^\"'#\s]+)", candidate)
            if name_match:
                names.append(name_match.group(1))
        return names
    return []


def check_repository_facts(issues: list[str]) -> None:
    quick_path = DOCS / "reference/quick-reference.md"
    quick_text = quick_path.read_text(encoding="utf-8")
    architecture_path = DOCS / "manual/core-concepts/architecture.md"
    architecture_text = architecture_path.read_text(encoding="utf-8")

    known_components: set[str] = set()
    expected_spacecraft: dict[str, set[str]] = {}
    for path in sorted((REPOSITORY / "cfg/drm/spacecraft").glob("*.yaml")):
        name_match = re.search(r'^spacecraft_name:\s*[\"\']?([^\"\'\s]+)', path.read_text(encoding="utf-8"), re.MULTILINE)
        if not name_match:
            report(issues, path, None, "spacecraft_name is missing")
            continue
        components = set(yaml_named_list(path, "components"))
        expected_spacecraft[name_match.group(1)] = components
        known_components.update(components)

    documented_spacecraft: dict[str, str] = {}
    for line in quick_text.splitlines():
        match = re.match(r"^\| `([^`]+)` \| ([^|]+) \|$", line)
        if match and match.group(1) in expected_spacecraft:
            documented_spacecraft[match.group(1)] = match.group(2).lower()

    for spacecraft, components in expected_spacecraft.items():
        if spacecraft not in documented_spacecraft:
            report(issues, quick_path, None, f"missing spacecraft reference row: {spacecraft}")
            continue
        row = documented_spacecraft[spacecraft]
        documented = {component for component in known_components if re.search(rf"\b{re.escape(component)}\b", row)}
        if documented != components:
            report(
                issues,
                quick_path,
                None,
                f"component list for {spacecraft} does not match cfg: expected {sorted(components)}, found {sorted(documented)}",
            )

    mission_path = REPOSITORY / "cfg/drm/drm.yaml"
    scenarios = yaml_named_list(mission_path, "scenarios")
    for scenario in scenarios:
        if f"`{scenario}`" not in quick_text:
            report(issues, quick_path, None, f"missing DRM scenario: {scenario}")

    compose_text = (REPOSITORY / "cfg/shire-compose.j2").read_text(encoding="utf-8")
    services = set(re.findall(r"^  (shire-[A-Za-z0-9-]+):\s*$", compose_text, re.MULTILINE))
    for service in services:
        if f"`{service}`" not in architecture_text:
            report(issues, architecture_path, None, f"missing full lab service: {service}")

    ports = set(re.findall(r'"(\d+):\d+"', compose_text))
    yamcs_config = REPOSITORY / "yamcs/src/main/yamcs/etc/yamcs.shire.yaml"
    if yamcs_config.exists():
        ports.update(re.findall(r"^\s+port:\s*(\d+)\s*$", yamcs_config.read_text(encoding="utf-8"), re.MULTILINE))
    for port in ports:
        if port not in quick_text:
            report(issues, quick_path, None, f"missing configured public or UDP port: {port}")


def main() -> int:
    issues: list[str] = []
    referenced_assets = check_markdown(issues)
    check_assets(issues, referenced_assets)
    check_make_commands(issues)
    check_procedures(issues)
    check_repository_facts(issues)

    if issues:
        print("Atlas validation failed:")
        for issue in issues:
            print(f"* {issue}")
        return 1

    print(
        f"Atlas validation passed for {len(MARKDOWN_FILES)} Markdown files, "
        f"{len(referenced_assets)} referenced assets, and {len(PROCEDURE_FILES)} procedures."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
