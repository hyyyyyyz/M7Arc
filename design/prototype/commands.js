(function () {
  const separator = () => ({ separator: true });
  const item = (id, label, shortcut, extra = {}) => ({ id, label, ...(shortcut ? { shortcut } : {}), ...extra });
  const hashes = [
    item('crc32', 'CRC-32'), item('crc64', 'CRC-64'), item('xxh64', 'XXH64'),
    item('md5', 'MD5'), item('sha1', 'SHA-1'), item('sha256', 'SHA-256'),
    item('sha384', 'SHA-384'), item('sha512', 'SHA-512'), item('sha3-256', 'SHA3-256'),
    item('blake2sp', 'BLAKE2sp'), item('hash-all', '* — 全部算法')
  ];
  const shellItems = [
    item('open', '打开压缩包'),
    item('open-as', '按格式打开', null, { items: [
      item('open-inside-one', '* — 单层归档'), item('open-inside-parser', '# — 解析容器'),
      item('open-inside-parser-exact', '#:e — 精确解析'),
      item('open-as-7z', '7z'), item('open-as-zip', 'zip'), item('open-as-cab', 'cab'), item('open-as-rar', 'rar')
    ] }),
    separator(), item('extract', '提取文件…'), item('extract-here', '提取到当前位置'),
    item('extract-folder', '提取到“归档名称/”'), item('test', '测试压缩包'),
    separator(), item('add', '添加到压缩包…'), item('compress-email', '压缩并通过电子邮件发送…'),
    item('compress-7z', '添加到“归档名称.7z”'), item('compress-7z-email', '压缩到 .7z 并通过电子邮件发送'),
    item('compress-zip', '添加到“归档名称.zip”'), item('compress-zip-email', '压缩到 .zip 并通过电子邮件发送')
  ];
  const bookmarks = Array.from({ length: 10 }, (_, i) => item('favorite-' + i, '书签 ' + i, '⌥' + i));
  const setBookmarks = Array.from({ length: 10 }, (_, i) => item('favorite-set-' + i, '书签 ' + i, '⌥⇧' + i));
  window.M7ArcCommands = {
    menus: [
      { label: '文件', items: [
        item('sevenzip-menu', '7-Zip', null, { dynamic: 'filesystem', items: shellItems }),
        item('shell-hash-menu', 'CRC SHA', null, { dynamic: 'filesystem', items: [
          ...hashes, separator(), item('hash-save', 'SHA-256 → file.sha256'), item('hash-verify', '校验和：测试')
        ] }),
        separator(), item('open', '打开', '↵'), item('open-inside', '内部打开', '⌃PageDown'),
        item('open-inside-one', '内部打开 *'), item('open-inside-parser', '内部打开 #'),
        item('open-outside', '外部打开', '⇧↵'), item('view-file', '查看', 'F3'), item('edit-file', '编辑', 'F4'),
        separator(), item('rename', '重命名', 'F2'), item('copy', '复制到…', 'F5'),
        item('move', '移动到…', 'F6'), item('delete', '删除', '⌦'),
        separator(), item('split', '分割文件…'), item('combine', '合并文件…'),
        separator(), item('info', '属性', '⌥↵'), item('comment', '注释…', '⌃Z'),
        item('crc-menu', 'CRC', null, { items: hashes }), item('diff', '比较差异', null, { requires: 'diff-tool' }),
        separator(), item('create-folder', '新建文件夹', 'F7'), item('create-file', '新建文件', '⌃N'),
        separator(), item('link', '链接…'), item('streams', '备用数据流', null, { requires: 'streams-provider' }),
        separator(), item('exit', '退出 M7Arc', '⌘Q')
      ] },
      { label: '编辑', items: [
        item('select-all', '全选', '⌘A'), item('deselect-all', '取消全选', '⇧小键盘 −'),
        item('invert-selection', '反向选择', '小键盘 *'), item('select-mask', '选择…', '小键盘 +'),
        item('deselect-mask', '取消选择…', '小键盘 −'), separator(),
        item('select-type', '按类型选择', '⌥小键盘 +'), item('deselect-type', '按类型取消选择', '⌥小键盘 −')
      ] },
      { label: '查看', items: [
        item('view-large', '大图标', '⌃1'), item('view-small', '小图标', '⌃2'),
        item('view-list', '列表', '⌃3'), item('view-details', '详细信息', '⌃4', { checked: true }),
        separator(), item('sort-name', '名称', '⌃F3', { checked: true }), item('sort-type', '类型', '⌃F4'),
        item('sort-date', '日期', '⌃F5'), item('sort-size', '大小', '⌃F6'), item('sort-none', '不排序', '⌃F7'),
        separator(), item('flat-view', '平面视图', null, { checked: false }),
        item('two-panels', '双面板', 'F9', { checked: false }),
        item('time-menu', '时间显示', null, { items: [
          item('time-day', '2026-09-06'), item('time-minute', '2026-09-06 14:32'),
          item('time-second', '2026-09-06 14:32:08', null, { checked: true }),
          item('time-ntfs', '2026-09-06 14:32:08.1234567'), item('time-ns', '2026-09-06 14:32:08.123456789'),
          separator(), item('time-utc', 'UTC', null, { checked: false })
        ] }),
        item('toolbars-menu', '工具栏', null, { items: [
          item('toolbar-archive', '压缩工具栏', null, { checked: true }),
          item('toolbar-standard', '标准工具栏', null, { checked: true }), separator(),
          item('toolbar-large', '大按钮', null, { checked: true }),
          item('toolbar-text', '显示按钮文字', null, { checked: true })
        ] }),
        item('root', '打开根文件夹', '\\'), item('up', '向上一级', '⌫'), item('history', '文件夹历史…', '⌥F12'),
        item('refresh', '刷新', '⌘R'), item('auto-refresh', '自动刷新', null, { checked: true })
      ] },
      { label: '收藏夹', items: [item('favorite-add-menu', '添加当前文件夹为', null, { items: setBookmarks }), separator(), ...bookmarks] },
      { label: '工具', items: [item('options', '选项…', '⌘,'), separator(), item('benchmark', '基准测试'), separator(), item('temp-cleanup', '删除临时文件…')] },
      { label: '帮助', items: [item('help', '帮助内容…', 'F1'), separator(), item('about', '关于 M7Arc…')] }
    ],
    toolbar: [
      item('add', '添加', null, { icon: 'add', group: 'archive', description: '添加到压缩包' }),
      item('extract', '提取', null, { icon: 'extract', group: 'archive', description: '提取文件' }),
      item('test', '测试', null, { icon: 'test', group: 'archive', description: '测试压缩包完整性' }),
      item('copy', '复制', 'F5', { icon: 'copy', group: 'standard', description: '复制到…' }),
      item('move', '移动', 'F6', { icon: 'move', group: 'standard', description: '移动到…' }),
      item('delete', '删除', '⌦', { icon: 'delete', group: 'standard', description: '删除选中项目' }),
      item('info', '信息', '⌥↵', { icon: 'info', group: 'standard', description: '查看文件与归档属性' })
    ]
  };
}());
