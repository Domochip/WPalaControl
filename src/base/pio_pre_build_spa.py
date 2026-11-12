import os
import re

# Screens of the web UI: template id -> fragment files, one "part" per file (see the screens in index_template.html).
# Files ending with a number get that number as part id ('0', '1', '2'), used in API endpoints;
# other files get an empty part id.
PAGES = {
    'tpl-status': ['src/base/data/status0.html', 'src/base/data/status1.html', 'src/data/status2.html'],
    'tpl-wifi':   ['src/base/data/config1.html'],
    'tpl-system': ['src/base/data/config0.html'],
    'tpl-app':    ['src/data/config2.html'],
    'tpl-fw':     ['src/base/data/fw.html'],
}

# Returns the value of a #define macro from a C/C++ header (quotes removed)
def extract_macro_value(file_path, macro_name):
    with open(file_path, 'r') as file:
        content = file.read()
        pattern = rf'#define\s+{macro_name}\s+"?([^"\s]+)"?'
        match = re.search(pattern, content)
        if match:
            return match.group(1)
        else:
            raise ValueError(f"{macro_name} not found in {file_path}")

# Reads a fragment file and returns (part_id, markup, script):
#   - part_id : number at the end of the filename ('status1.html' -> '1'), '' if none
#   - markup  : the HTML without its <script> blocks
#   - script  : the content of its <script> blocks
def read_part(source):
    match = re.search(r'(\d+)\.html$', os.path.basename(source))
    part_id = match.group(1) if match else ''
    with open(source, 'r', encoding='utf-8') as f:
        html = f.read()
    script = '\n'.join(re.findall(r'<script>(.*?)</script>', html, flags=re.DOTALL))
    markup = re.sub(r'<script>.*?</script>', '', html, flags=re.DOTALL).strip()
    return part_id, markup, script

# Builds one page as HTML: a <template> holding each part's markup in <div data-part="ID">,
# followed by a <script> registering each part's script as an init function:
#   registerPart('<templateId>', 'ID', function (root, id) { ...script... });
def build_page(template_id, sources):
    print(f'Building {template_id} from {", ".join(os.path.basename(s) for s in sources)}')
    parts = [read_part(source) for source in sources]
    template = ''.join(f'<div data-part="{part_id}">\n{markup}\n</div>\n' for part_id, markup, _ in parts)
    script = ''.join(f"registerPart('{template_id}', '{part_id}', function (root, id) {{\n{js}\n}});\n"
                     for part_id, _, js in parts if js.strip())
    return f'<template id="{template_id}">\n{template}</template>\n<script>\n{script}</script>\n'

# Reads the template file, replaces {{KEY}} placeholders and <!-- PAGES --> with all pages
# producing a single self-contained index.html
def build_spa(template_file, pages, output_file, placeholders=None):
    print(f'Building SPA -> {os.path.basename(output_file)}')
    if placeholders is None:
        placeholders = {}

    with open(template_file, 'r', encoding='utf-8') as f:
        html = f.read()

    for key, value in placeholders.items():
        html = html.replace(f'{{{{{key}}}}}', value)

    pages_html = '\n'.join(build_page(template_id, sources) for template_id, sources in pages.items())
    html = html.replace('<!-- PAGES -->', pages_html)

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(html)

print('--- pio_pre_build_spa.py start ---')

build_spa(
    'src/base/data/index_template.html',
    PAGES,
    'src/base/data/index.html',
    placeholders={
        'APP_MODEL': extract_macro_value('./src/Main.h', 'CUSTOM_APP_MODEL'),
    }
)

print('--- pio_pre_build_spa.py end ---')
print()
