from pathlib import Path


root = Path(__file__).resolve().parent
shell = (root / 'shell.html').read_text()
style = '<style>\n' + (root / 'style.css').read_text() + '\n</style>\n'
scripts = ''
for filename in ['formats.js', 'commands.js', 'dialogs.js', 'app.js']:
    scripts += '<script>\n' + (root / filename).read_text() + '\n</script>\n'
fragment = style + shell + scripts
(root.parent / 'm7arc-ui.html').write_text(fragment)
icon_library = '<script>\n' + (root.parent / 'vendor/lucide-0.468.0.min.js').read_text() + '\n</script>\n'
standalone = '<!doctype html>\n<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="light dark"><title>M7Arc File Manager</title><style>body{margin:0;padding:20px;background:light-dark(#e9ecf1,#18191d);color-scheme:light dark}body>main{max-width:1200px;margin:auto}@media(max-width:600px){body{padding:0}}</style></head><body><main>\n' + icon_library + fragment + '\n</main></body></html>\n'
(root.parent / 'index.html').write_text(standalone)
print('Built design/m7arc-ui.html and design/index.html')
