from pathlib import Path

try:
    Import("env")
    project_dir = Path(env["PROJECT_DIR"])
except NameError:
    project_dir = Path(__file__).resolve().parents[1]

config_page_source = project_dir / "src" / "web" / "config_page.html"
config_page_target = project_dir / "include" / "generated" / "config_page_html.h"
default_sprite_source_dir = project_dir / "src" / "assets" / "default_pet_sprites"
default_sprite_target = project_dir / "include" / "generated" / "default_pet_sprites.h"


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


def c_identifier(name):
    result = []
    capitalize_next = True
    for char in name:
        if char.isalnum():
            result.append(char.upper() if capitalize_next else char)
            capitalize_next = False
        else:
            capitalize_next = True
    return "".join(result)


def sprite_name_from_file(path):
    return path.stem


def format_bytes(data):
    lines = []
    for index in range(0, len(data), 12):
        chunk = data[index : index + 12]
        lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    return "\n".join(lines)


def generate_default_sprite_header(source_dir, target_path):
    sprite_paths = sorted(Path(source_dir).glob("*.gif"))

    content = [
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace default_pet_sprites {",
        "struct Sprite {",
        "  const char *name;",
        "  const uint8_t *data;",
        "  size_t size;",
        "};",
        "",
    ]

    sprite_entries = []
    for sprite_path in sprite_paths:
        name = sprite_name_from_file(sprite_path)
        identifier = "k" + c_identifier(name) + "Gif"
        data = sprite_path.read_bytes()
        content.extend(
            [
                f"alignas(4) constexpr uint8_t {identifier}[] = {{",
                format_bytes(data),
                "};",
                "",
            ]
        )
        sprite_entries.append((name, identifier, len(data)))

    content.append("constexpr Sprite kSprites[] = {")
    for name, identifier, size in sprite_entries:
        content.append(f'    {{"{name}", {identifier}, {size}}},')
    content.extend(
        [
            "};",
            "",
            f"constexpr size_t kSpriteCount = {len(sprite_entries)};",
            "}  // namespace default_pet_sprites",
            "",
        ]
    )

    target_path = Path(target_path)
    target_path.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(content)
    if target_path.exists() and target_path.read_text(encoding="utf-8") == text:
        return

    target_path.write_text(text, encoding="utf-8")


generate_config_page_header(config_page_source, config_page_target)
generate_default_sprite_header(default_sprite_source_dir, default_sprite_target)
