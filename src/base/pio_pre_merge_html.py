import os
import re

# Each HTML fragment uses 'qsp' as a CSS selector prefix to scope its DOM queries
# (e.g. qsp + '#someId' resolves to '#contentN #someId').
# qsp[8] extracts the app index digit from that string (character at index 8 of '#contentN ')
# and is used to build API endpoint URLs like '/gsN', '/gcN', '/scN'.
# Both substitutions are done at build time so the merged file needs no runtime patching.
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
    # Wrap in a scoped div so element IDs don't collide across fragments
    return f'<div id="{wrapper_id}">\n{html}\n</div>\n'

def merge_html_files(sources, output):
    print(f'Merging {", ".join(os.path.basename(s) for s in sources)} -> {os.path.basename(output)}')
    output_html = ''
    for source in sources:
        fragment_index = int(re.search(r'(\d+)\.html$', os.path.basename(source)).group(1))
        with open(source, 'r', encoding='utf-8') as f:
            output_html += preprocess_fragment(f.read(), fragment_index)

    with open(output, 'w', encoding='utf-8') as f:
        f.write(output_html)

print('--- pio_pre_merge_html.py start ---')

merge_html_files(
    ['src/base/data/status0.html', 'src/base/data/status1.html', 'src/data/status2.html'],
    'src/base/data/status.html'
)
merge_html_files(
    ['src/base/data/config0.html', 'src/base/data/config1.html', 'src/data/config2.html'],
    'src/base/data/config.html'
)

print('--- pio_pre_merge_html.py end ---')
print()
