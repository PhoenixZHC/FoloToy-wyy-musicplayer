#!/usr/bin/env python3
"""Dependency-free repository checks shared by local development and CI."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parent.parent
FULL_SHA_RE = re.compile(r"^[0-9a-f]{40}$")
MARKDOWN_LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
CJK_RE = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")
SECRET_PATTERNS = {
    "GitHub token": re.compile(r"(?:ghp_|github_pat_)[A-Za-z0-9_]{20,}"),
    "AWS access key": re.compile(r"AKIA[0-9A-Z]{16}"),
    "private key": re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
}
ROOT_MARKDOWN_ALLOWLIST = {
    "AGENTS.md",
    "AGENTS.zh_CN.md",
    "CLAUDE.md",
    "CLAUDE.zh_CN.md",
    "README.md",
    "README.zh_CN.md",
}
DYNAMIC_FONT_SIZES = (10, 14, 18, 24)
MIN_DYNAMIC_FONT_CHARACTERS = 3000


def git_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return [ROOT / line for line in result.stdout.splitlines() if line]


def text_files() -> list[Path]:
    files: list[Path] = []
    for path in git_files():
        if not path.is_file() or path.stat().st_size > 2 * 1024 * 1024:
            continue
        try:
            path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        files.append(path)
    return files


def check_required_files(errors: list[str]) -> None:
    required = (
        "AGENTS.md",
        "AGENTS.zh_CN.md",
        "CLAUDE.md",
        "CLAUDE.zh_CN.md",
        "docs/CHANGELOG.md",
        ".github/CONTRIBUTING.md",
        ".github/CODE_OF_CONDUCT.md",
        ".github/SECURITY.md",
        ".github/SUPPORT.md",
        "dependencies.lock",
        "sdkconfig.defaults",
        "partitions.csv",
        ".github/PULL_REQUEST_TEMPLATE.md",
    )
    for name in required:
        if not (ROOT / name).is_file():
            errors.append(f"missing required file: {name}")

    ignored = subprocess.run(
        ["git", "check-ignore", "-q", "dependencies.lock"], cwd=ROOT
    )
    if ignored.returncode == 0:
        errors.append("dependencies.lock must be tracked, not ignored")

    for path in sorted(ROOT.glob("*.md")):
        if path.name not in ROOT_MARKDOWN_ALLOWLIST:
            errors.append(
                f"{path.name}: root Markdown must move to docs/ or .github/"
            )


def check_markdown_links(files: list[Path], errors: list[str]) -> None:
    for path in files:
        if path.suffix.lower() != ".md":
            continue
        text = path.read_text(encoding="utf-8")
        for raw_target in MARKDOWN_LINK_RE.findall(text):
            target = raw_target.strip().split(maxsplit=1)[0].strip("<>")
            if not target or target.startswith(("#", "http://", "https://", "mailto:")):
                continue
            local = unquote(target.split("#", 1)[0])
            resolved = (ROOT / local.lstrip("/")) if local.startswith("/") else (path.parent / local)
            if local and not resolved.resolve().exists():
                errors.append(f"{path.relative_to(ROOT)}: missing link target {target}")


def check_document_languages(files: list[Path], errors: list[str]) -> None:
    """Require an English default and a linked Simplified Chinese peer."""
    markdown = {path.resolve() for path in files if path.suffix.lower() == ".md"}

    for path in sorted(markdown):
        name = path.name
        text = path.read_text(encoding="utf-8")
        opening = "\n".join(text.splitlines()[:8])

        if name.endswith(".zh_CN.md"):
            default_name = f"{name[:-len('.zh_CN.md')]}.md"
            default_path = path.with_name(default_name).resolve()
            if default_path not in markdown:
                errors.append(
                    f"{path.relative_to(ROOT)}: missing English default {default_name}"
                )
            elif default_name not in opening:
                errors.append(
                    f"{path.relative_to(ROOT)}: missing top language link to {default_name}"
                )
            continue

        chinese_name = f"{path.stem}.zh_CN.md"
        chinese_path = path.with_name(chinese_name).resolve()
        if chinese_path not in markdown:
            errors.append(
                f"{path.relative_to(ROOT)}: missing Simplified Chinese peer {chinese_name}"
            )
        elif chinese_name not in opening:
            errors.append(
                f"{path.relative_to(ROOT)}: missing top language link to {chinese_name}"
            )

        english_prose = text.replace("简体中文", "")
        match = CJK_RE.search(english_prose)
        if match:
            line = english_prose.count("\n", 0, match.start()) + 1
            errors.append(
                f"{path.relative_to(ROOT)}:{line}: default Markdown must use English prose"
            )


def check_action_pins(errors: list[str]) -> None:
    workflow_dir = ROOT / ".github" / "workflows"
    for path in sorted(workflow_dir.glob("*.y*ml")):
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            match = re.match(r"\s*-?\s*uses:\s*['\"]?([^'\"\s]+)", line)
            if not match:
                continue
            action = match.group(1)
            if action.startswith("./"):
                continue
            if action.startswith("docker://"):
                if "@sha256:" not in action:
                    errors.append(f"{path.relative_to(ROOT)}:{line_number}: unpinned Docker action {action}")
                continue
            if "@" not in action or not FULL_SHA_RE.fullmatch(action.rsplit("@", 1)[1]):
                errors.append(f"{path.relative_to(ROOT)}:{line_number}: action must use a full commit SHA: {action}")


def check_issue_forms(errors: list[str]) -> None:
    issue_dir = ROOT / ".github" / "ISSUE_TEMPLATE"
    for name in ("feature_request.yml", "usage_question.yml"):
        path = issue_dir / name
        if not path.is_file():
            errors.append(f"missing issue form: {path.relative_to(ROOT)}")
            continue
        text = path.read_text(encoding="utf-8")
        for field in ("name:", "description:", "body:"):
            if not re.search(rf"(?m)^{re.escape(field)}", text):
                errors.append(f"{path.relative_to(ROOT)}: missing top-level {field[:-1]}")


def check_sensitive_content(files: list[Path], errors: list[str]) -> None:
    for path in files:
        text = path.read_text(encoding="utf-8")
        for label, pattern in SECRET_PATTERNS.items():
            if pattern.search(text):
                errors.append(f"{path.relative_to(ROOT)}: possible {label}")

        for match in re.finditer(r"https://ai-passport\.folotoy\.cn/trae/\?s=([^&\s)]+)&k=([^\s)]+)", text):
            if "<" not in match.group(1) and "AAAAAA" not in match.group(1):
                errors.append(f"{path.relative_to(ROOT)}: possible unsanitized device QR link")


def check_conflict_markers(files: list[Path], errors: list[str]) -> None:
    marker = re.compile(r"(?m)^(<<<<<<< |=======\s*$|>>>>>>> )")
    for path in files:
        if marker.search(path.read_text(encoding="utf-8")):
            errors.append(f"{path.relative_to(ROOT)}: unresolved merge conflict marker")


def check_lvgl_font_config(errors: list[str]) -> None:
    custom_font_dir = ROOT / "main" / "fonts"
    sdkconfig_defaults = ROOT / "sdkconfig.defaults"
    has_compressed_font = any(
        ".bitmap_format = 1" in path.read_text(encoding="utf-8")
        for path in custom_font_dir.glob("*.c")
    )
    if not has_compressed_font:
        return

    config = sdkconfig_defaults.read_text(encoding="utf-8")
    if "CONFIG_LV_USE_FONT_COMPRESSED=y" not in config:
        errors.append(
            "sdkconfig.defaults: compressed LVGL fonts require CONFIG_LV_USE_FONT_COMPRESSED=y"
        )


def read_sparse_font_characters(path: Path) -> set[str]:
    source = path.read_text(encoding="utf-8")
    unicode_lists = {
        name: [int(value, 16) for value in re.findall(r"0x([0-9a-f]+)", body, re.I)]
        for name, body in re.findall(
            r"static const uint16_t (unicode_list_\d+)\[\] = \{(.*?)\};",
            source,
            re.DOTALL,
        )
    }
    cmap_matches = re.findall(
        r"\{\s*\.range_start = (\d+), \.range_length = (\d+),.*?"
        r"\.unicode_list = (NULL|unicode_list_\d+),.*?\.list_length = (\d+), "
        r"\.type = (LV_FONT_FMT_TXT_CMAP_[A-Z0-9_]+)\s*\}",
        source,
        re.DOTALL,
    )
    if not cmap_matches:
        raise ValueError(f"cannot read font character maps from {path}")

    characters: set[str] = set()
    for range_start_text, range_length_text, list_name, list_length_text, cmap_type in cmap_matches:
        range_start = int(range_start_text)
        range_length = int(range_length_text)
        if list_name == "NULL":
            codepoints = range(range_start, range_start + range_length)
        else:
            offsets = unicode_lists.get(list_name)
            expected_count = int(list_length_text)
            if offsets is None or len(offsets) != expected_count:
                raise ValueError(f"{path}: invalid {list_name} for {cmap_type}")
            codepoints = (range_start + offset for offset in offsets)
        characters.update(chr(codepoint) for codepoint in codepoints if codepoint > 0x7F)
    return characters


def check_device_font_contract(errors: list[str]) -> None:
    project_root = ROOT.parent.parent
    charset_path = project_root / "server" / "src" / "generated" / "deviceCharset.ts"
    charset_source = charset_path.read_text(encoding="utf-8")
    allowlist_match = re.search(
        r'export const DEVICE_SUPPORTED_TEXT = "([^"]*)";',
        charset_source,
    )
    if allowlist_match is None:
        errors.append("server device character allowlist is missing")
        return

    service_characters = set(allowlist_match.group(1))
    reference_characters: set[str] | None = None
    for size in DYNAMIC_FONT_SIZES:
        font_path = ROOT / "main" / "fonts" / f"netease_ui_font_{size}.c"
        try:
            font_characters = read_sparse_font_characters(font_path)
        except (OSError, ValueError) as error:
            errors.append(str(error))
            continue
        if len(font_characters) < MIN_DYNAMIC_FONT_CHARACTERS:
            errors.append(
                f"{font_path.relative_to(ROOT)}: only {len(font_characters)} dynamic characters; "
                f"need at least {MIN_DYNAMIC_FONT_CHARACTERS}"
            )
        if reference_characters is None:
            reference_characters = font_characters
        elif font_characters != reference_characters:
            errors.append(f"{font_path.relative_to(ROOT)}: dynamic font character set differs")

    if reference_characters is not None and service_characters != reference_characters:
        missing = "".join(sorted(service_characters - reference_characters))
        extra = "".join(sorted(reference_characters - service_characters))
        errors.append(
            "server/device font character sets differ: "
            f"missing_in_font={missing!r} missing_in_service={extra!r}"
        )


def main() -> int:
    errors: list[str] = []
    files = text_files()
    check_required_files(errors)
    check_markdown_links(files, errors)
    check_document_languages(files, errors)
    check_action_pins(errors)
    check_issue_forms(errors)
    check_sensitive_content(files, errors)
    check_conflict_markers(files, errors)
    check_lvgl_font_config(errors)
    check_device_font_contract(errors)

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(f"Repository checks: PASS ({len(files)} text files scanned)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
