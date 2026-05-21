#!/usr/bin/env python3
"""Analyze stack-usage (.su) combined with compiler callgraph (.ci).

Produces top-N call-stack paths by summing per-function stack sizes.
Preferred input sources (in order): .ci files (callgraph), .su files (stack sizes).
"""
import os
import re
import sys
import collections

Import("env")


def find_files(root, ext):
    out = []
    for r, d, files in os.walk(root):
        for f in files:
            if f.endswith(ext):
                out.append(os.path.join(r, f))
    return out


def normalize_name_from_sig(sig):
    # sig: demangled function signature line, e.g. 'bool WPalaControl::mqttPublishData(const String&...)'
    if not sig:
        return ''
    # remove template noise and leading qualifiers
    sig = sig.strip()
    # take before '('
    idx = sig.find('(')
    if idx != -1:
        before = sig[:idx].strip()
    else:
        before = sig
    # drop return type / qualifiers by taking last token
    last_space = before.rfind(' ')
    if last_space != -1:
        name = before[last_space+1:].strip()
    else:
        name = before.strip()
    return name


def normalize_plain(raw):
    if not raw:
        return ''
    s = raw.strip()
    # remove surrounding file:prefix if present (contains ':')
    if ':' in s and s.count(':') > 1:
        # keep whole if looks like C++ mangled/title, otherwise try last segment
        parts = s.split(':')
        # if looks like 'src\file.cpp:_ZN...' keep after last ':' as mangled
        if s.startswith('src') or s.startswith('.pio') or s.find('\\') != -1 or s.find('/') != -1:
            # try to take part after first ':'
            s = s.split(':', 1)[1]
    # remove parentheses and trailing args
    idx = s.find('(')
    if idx != -1:
        s = s[:idx]
    # take last token after space
    last = s.rfind(' ')
    if last != -1:
        s = s[last+1:]
    # strip quotes
    s = s.strip('"').strip("'")
    return s


def parse_su_files(su_files):
    pattern = re.compile(r'(.+?)\s+(\d+)\s+static$')
    weights = {}
    origin = {}
    for s in su_files:
        try:
            with open(s, 'r', encoding='utf-8', errors='ignore') as fh:
                for line in fh:
                    m = pattern.search(line.rstrip('\n'))
                    if not m:
                        continue
                    sig = m.group(1).strip()
                    size = int(m.group(2))
                    # normalize signature like in previous analyzer
                    idx = sig.find('(')
                    if idx != -1:
                        before = sig[:idx].strip()
                    else:
                        before = sig
                    last_space = before.rfind(' ')
                    if last_space != -1:
                        name = before[last_space+1:].strip()
                    else:
                        name = before.strip()
                    if not name:
                        continue
                    if name not in weights or size > weights[name]:
                        weights[name] = size
                        origin[name] = s
        except Exception as e:
            print('ERROR reading .su file %s: %s' % (s, e), file=sys.stderr)
    return weights, origin


def parse_ci_files(ci_files):
    # returns graph: name -> set(callee_names), and maps for provenance
    title_to_norm = {}
    mangled_to_norm = {}
    norm_to_src = collections.defaultdict(set)
    edges = []
    node_re = re.compile(r'node:\s*\{\s*title:\s*"([^"]+)"\s+label:\s*"([^"]+)"')
    edge_re = re.compile(r'edge:\s*\{\s*sourcename:\s*"([^"]+)"\s+targetname:\s*"([^"]+)"')
    for ci in ci_files:
        try:
            with open(ci, 'r', encoding='utf-8', errors='ignore') as fh:
                for line in fh:
                    line = line.rstrip('\n')
                    if 'node:' in line:
                        m = node_re.search(line)
                        if not m:
                            continue
                        title = m.group(1)
                        label = m.group(2).replace('\\n', '\n')
                        label_lines = label.split('\n')
                        sig_line = label_lines[0] if label_lines else ''
                        src_line = label_lines[1] if len(label_lines) > 1 else None
                        norm = normalize_name_from_sig(sig_line)
                        if not norm:
                            # fallback to title's trailing token
                            norm = normalize_plain(title)
                        title_to_norm[title] = norm
                        if src_line:
                            norm_to_src[norm].add(src_line)
                        # if title contains mangled part after ':' map it
                        if ':' in title:
                            parts = title.split(':', 1)
                            mangled = parts[1].strip()
                            mangled_to_norm[mangled] = norm
                    elif 'edge:' in line:
                        m = edge_re.search(line)
                        if not m:
                            continue
                        s = m.group(1)
                        t = m.group(2)
                        edges.append((s, t))
        except Exception as e:
            print('ERROR reading .ci file %s: %s' % (ci, e), file=sys.stderr)

    graph = collections.defaultdict(set)
    for s, t in edges:
        if not s or not t:
            continue
        if t == '__indirect_call':
            continue
        # resolve source
        src_norm = None
        if s in title_to_norm:
            src_norm = title_to_norm[s]
        else:
            # try mangled
            if ':' in s:
                mang = s.split(':', 1)[1]
                src_norm = mangled_to_norm.get(mang)
        if not src_norm:
            src_norm = normalize_plain(s)
        # resolve target
        tgt_norm = None
        if t in title_to_norm:
            tgt_norm = title_to_norm[t]
        else:
            if ':' in t:
                mang = t.split(':', 1)[1]
                tgt_norm = mangled_to_norm.get(mang)
        if not tgt_norm:
            tgt_norm = normalize_plain(t)

        if src_norm and tgt_norm:
            graph[src_norm].add(tgt_norm)

    return graph, norm_to_src


def compute_top_paths(graph, weights, topn=3):
    sys.setrecursionlimit(10000)
    memo = {}

    def dfs(node, visited):
        if node in memo:
            return memo[node]
        if node in visited:
            return (0, [node])
        visited.add(node)
        w = weights.get(node, 0)
        best_sum = w
        best_path = [node]
        for c in graph.get(node, []):
            cs, cp = dfs(c, visited)
            total = w + cs
            if total > best_sum:
                best_sum = total
                best_path = [node] + cp
        visited.remove(node)
        memo[node] = (best_sum, best_path)
        return memo[node]

    all_nodes = set(weights.keys()) | set(graph.keys())
    results = []
    for n in all_nodes:
        s, p = dfs(n, set())
        results.append((s, p))
    results.sort(key=lambda x: x[0], reverse=True)
    return results[:topn]


def execute(source, target, env):
    print()
    print('---- pio_post_find_biggest_callstack_paths.py start ----')
    
    build_dir = os.path.join('.pio', 'build', env["PIOENV"])
    if not os.path.isdir(build_dir):
        print('ERROR: build dir not found: %s' % build_dir, file=sys.stderr)
        sys.exit(2)

    su_files = find_files(build_dir, '.su')
    # also check global libdeps
    libdeps = os.path.join('.pio', 'libdeps', env["PIOENV"])
    if os.path.isdir(libdeps):
        for r, d, files in os.walk(libdeps):
            for f in files:
                if f.endswith('.su'):
                    su_files.append(os.path.join(r, f))

    ci_files = find_files(build_dir, '.ci')
    if os.path.isdir(libdeps):
        for r, d, files in os.walk(libdeps):
            for f in files:
                if f.endswith('.ci'):
                    ci_files.append(os.path.join(r, f))

    if not su_files:
        print('WARNING: no .su files found under %s' % build_dir, file=sys.stderr)

    weights, origin = parse_su_files(su_files)

    if ci_files:
        graph, norm_to_src = parse_ci_files(ci_files)
    else:
        graph = {}
        norm_to_src = {}
        print('WARNING: no .ci files found; cannot use callgraph info', file=sys.stderr)

    top = compute_top_paths(graph, weights, topn=15)

    out_path = os.path.join(build_dir, 'biggest_callstacks.txt')
    try:
        with open(out_path, 'w', encoding='utf-8') as out_f:
            out_f.write('TOP_CALLSTACKS (.ci + .su)\n')
            for i, (s, p) in enumerate(top, start=1):
                out_f.write('RANK %d: TOTAL_STACK=%d\n' % (i, s))
                for fn in p:
                    out_f.write('  %s %d\n' % (fn, weights.get(fn, 0)))
                    if fn in norm_to_src and norm_to_src[fn]:
                        for src in sorted(norm_to_src[fn]):
                            out_f.write('    -> %s\n' % src)
                out_f.write('\n')
        print('WROTE: %s' % out_path)
    except Exception as e:
        print('ERROR: cannot write output file %s: %s' % (out_path, e), file=sys.stderr)

    print('SUMMARY: funcs_with_stack=%d, callgraph_nodes=%d, su_files=%d, ci_files=%d' % (len(weights), len(graph), len(su_files), len(ci_files)))
    print('---- pio_post_find_biggest_callstack_paths.py end ----')
    print()

env.AddPostAction("$PROGPATH", execute)
