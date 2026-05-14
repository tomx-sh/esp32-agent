from pathlib import Path

Import("env")

project_dir = Path(env["PROJECT_DIR"])
source = project_dir / "src" / "web" / "config_page.html"
target = project_dir / "include" / "generated" / "config_page_html.h"


def generate_config_page_header(source_path, target_path):
    html = Path(source_path).read_text(encoding="utf-8")
    delimiter = "CONFIG_PAGE_HTML"

    if f"){delimiter}\"" in html:
        raise ValueError(f"HTML contains raw string delimiter {delimiter}")

    content = (
        "#pragma once\n\n"
        f"constexpr const char kConfigPageHtml[] = R\"{delimiter}("
        f"{html}"
        f"){delimiter}\";\n"
    )

    target_path = Path(target_path)
    target_path.parent.mkdir(parents=True, exist_ok=True)

    if target_path.exists() and target_path.read_text(encoding="utf-8") == content:
        return

    target_path.write_text(content, encoding="utf-8")


generate_config_page_header(source, target)
