"""Check workflow scaffold shape and local documentation links; no network or writes."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
required = {
    ".github/ISSUE_TEMPLATE/work_item.md": ["## Goal and boundaries", "## Acceptance criteria"],
    ".github/pull_request_template.md": ["## Architecture and decisions", "## Acceptance evidence"],
    "docs/DECISIONS.md": ["#", "https://github.com/"],
}
errors = []
for relative, markers in required.items():
    path = root / relative
    if not path.is_file():
        errors.append(f"missing {relative}")
        continue
    content = path.read_text()
    errors.extend(f"{relative}: missing {marker}" for marker in markers if marker not in content)
for relative in ["README.md", "development-workflow.md", "agent-practice.md", "docs/DECISIONS.md", "docs/workflow-continuation.md"]:
    path = root / relative
    if not path.exists():
        continue
    content = path.read_text()
    if content.count("```") % 2:
        errors.append(f"{relative}: unmatched code fence")
    for target in re.findall(r"\]\(([^)]+)\)", content):
        if "://" in target or target.startswith("#"):
            continue
        file_part = target.split("#", 1)[0]
        if file_part and not (path.parent / file_part).exists():
            errors.append(f"{relative}: missing local target {target}")
    lines = content.splitlines()
    for i, line in enumerate(lines):
        if line.startswith("|") and (i == 0 or not lines[i - 1].startswith("|")):
            if i + 1 >= len(lines) or not re.match(r"^\|[ :|\-]+\|$", lines[i + 1]):
                errors.append(f"{relative}:{i + 1}: detached table row")
if errors:
    raise SystemExit("\n".join(errors))
print("Workflow scaffold and local-link checks passed; semantic review is separate.")
