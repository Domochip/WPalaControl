import os
import re

def extract_macro_value(file_path, macro_name):
    with open(file_path, 'r') as file:
        content = file.read()
        pattern = rf'#define\s+{macro_name}\s+"?([^"\s]+)"?'
        match = re.search(pattern, content)
        if match:
            return match.group(1)
        else:
            raise ValueError(f"{macro_name} not found in {file_path}")

# HTML fragments use 'qsp' as a scoped CSS selector prefix, substituted at build time:
# - Numbered index N : qsp -> '#contentN ', qsp[8] -> 'N' (used in API endpoints like '/gsN')
#                      content is wrapped in <div id="contentN"> to isolate element IDs
# - Empty index ''   : qsp -> '#content ', no wrapper (fragment loads directly into #content)
def preprocess_fragment(html, fragment_index):
    wrapper_id = f'content{fragment_index}'
    qsp_replacement = f"'#{wrapper_id} '"

    # Replace qsp[8] before qsp to avoid double-substitution
    def replace_qsp(match):
        content = match.group(1)
        content = content.replace('qsp[8]', f"'{fragment_index}'")
        content = content.replace('qsp', qsp_replacement)
        return f'<script>{content}</script>'

    html = re.sub(r'<script>(.*?)</script>', replace_qsp, html, flags=re.DOTALL)
    if fragment_index == '':
        return html
    return f'<div id="{wrapper_id}">\n{html}\n</div>\n'

def merge_fragments(sources):
    print(f'Merging {", ".join(os.path.basename(s) for s in sources)}')
    merged = ''
    for source in sources:
        fragment_index = int(re.search(r'(\d+)\.html$', os.path.basename(source)).group(1))
        with open(source, 'r', encoding='utf-8') as f:
            merged += preprocess_fragment(f.read(), fragment_index)
    return merged

def read_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()

# Reads index_template.html and embeds each page as a <template> element,
# producing a single self-contained index.html with no runtime HTML fetches.
def build_spa(template_file, pages, output_file, placeholders=None):
    print(f'Building SPA -> {os.path.basename(output_file)}')
    if placeholders is None:
        placeholders = {}

    with open(template_file, 'r', encoding='utf-8') as f:
        html = f.read()

    for key, value in placeholders.items():
        html = html.replace(f'{{{{{key}}}}}', value)

    templates_html = ''
    for template_id, content in pages:
        templates_html += f'    <template id="{template_id}">\n{content}\n    </template>\n'

    html = html.replace('<!-- TEMPLATES -->', templates_html)

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(html)

print('--- pio_pre_build_spa.py start ---')

build_spa(
    'src/base/data/index_template.html',
    [
        ('tpl-status', merge_fragments(['src/base/data/status0.html', 'src/base/data/status1.html', 'src/data/status2.html'])),
        ('tpl-config', merge_fragments(['src/base/data/config0.html', 'src/base/data/config1.html', 'src/data/config2.html'])),
        ('tpl-fw',     preprocess_fragment(read_file('src/base/data/fw.html'), '')),
    ],
    'src/base/data/index.html',
    placeholders={
        'APP_MODEL': extract_macro_value('./src/Main.h', 'CUSTOM_APP_MODEL'),
    }
)

print('--- pio_pre_build_spa.py end ---')
print()
