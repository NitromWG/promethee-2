import sys
shell = open('shell.html', encoding='utf-8').read()
core = open('core.js', encoding='utf-8').read()
ui = open('ui.js', encoding='utf-8').read()
assert '/*__CORE__*/' in shell and '/*__UI__*/' in shell
assert '</script' not in core.lower() and '</script' not in ui.lower()
out = shell.replace('/*__CORE__*/', core).replace('/*__UI__*/', ui)
open(sys.argv[1] if len(sys.argv) > 1 else 'index.html', 'w', encoding='utf-8').write(out)
print('ok', len(out))
