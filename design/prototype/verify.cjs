const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');

async function main() {
  const output = path.resolve(__dirname, '../verification');
  fs.mkdirSync(output, { recursive: true });
  const browser = await chromium.launch({
    executablePath: process.env.CHROME_PATH || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless: true
  });
  const checks = [];
  const errors = [];
  try {
    const page = await browser.newPage({ viewport: { width: 1024, height: 900 }, colorScheme: 'light' });
    page.on('pageerror', error => errors.push(String(error)));
    const reset = () => page.goto(pathToFileURL(path.resolve(__dirname, '../index.html')).href);
    const close = () => page.locator('#p-dialog-close').click();
    const toolbar = id => page.locator('.p-tool[data-command="' + id + '"]').click();
    const selectFile = name => page.locator('.p-panel [data-entry]').filter({ hasText: name });
    const menu = async (index, id) => {
      await page.locator('[data-menu="' + index + '"]').click();
      await page.locator('.p-menu-popup:visible [data-command="' + id + '"]:visible').click();
    };
    const check = (name, condition) => { assert.ok(condition, name); checks.push(name); };
    const noOverflow = async label => check(label, await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await reset();
    check('六菜单、七按钮、默认单面板', await page.locator('[data-menu]').count() === 6 && await page.locator('.p-tool').count() === 7 && await page.locator('.p-panel').count() === 1);
    check('七个工具栏图标完整加载', await page.locator('.p-tool svg').count() === 7);
    check('文件列表与导航图标完整加载', await page.locator('#m7arc-fm i[data-lucide]').count() === 0);
    await page.screenshot({ path: path.join(output, 'main-light.png'), fullPage: true });
    await selectFile('project.7z').dblclick();
    check('双击进入归档', (await page.locator('[data-address-form] input').inputValue()).endsWith('project.7z/'));
    await selectFile('nested.zip').dblclick();
    check('嵌套归档保留父路径', (await page.locator('[data-address-form] input').inputValue()).endsWith('project.7z/nested.zip/'));
    await page.locator('.p-address [data-command="up"]').click();
    check('向上返回父归档', (await page.locator('[data-address-form] input').inputValue()).endsWith('project.7z/'));
    await reset();
    await page.locator('[data-command="formats"]').click();
    check('格式目录全部61项', (await page.locator('#p-catalog-count').textContent()).includes('61 / 61'));
    await page.locator('#p-format-mode').selectOption('write');
    check('仅七类归档可创建', (await page.locator('#p-catalog-count').textContent()).includes('7 / 61'));
    await page.locator('#p-format-mode').selectOption('all');
    await page.locator('#p-format-search').fill('rar');
    check('RAR与RAR5均可发现', await page.locator('#p-catalog-table tbody tr').count() === 2);
    await page.locator('[data-format-detail="rar5"]').click();
    check('RAR5无虚假创建入口', await page.locator('[data-create-format]').count() === 0);
    await page.locator('#p-format-search').fill('docx');
    check('扩展别名可检索ZIP handler', (await page.locator('#p-catalog-table').textContent()).includes('zip'));
    await page.locator('[data-cat-tab="codecs"]').click();
    check('25个codec完整呈现', await page.locator('#p-algorithm-table tbody tr').count() === 25);
    await page.locator('[data-cat-tab="hashers"]').click();
    check('十种摘要算法完整呈现', await page.locator('#p-algorithm-table tbody tr').count() === 10);
    await close();
    await toolbar('add');
    check('七类格式与三个组合预设', await page.locator('#dlg-format option').count() === 10);
    assert.deepEqual(await page.locator('#dlg-method option').allTextContents(), ['LZMA2', 'LZMA', 'PPMd', 'BZip2']);
    checks.push('7z普通方法遵循原版筛选');
    await page.locator('#dlg-format').selectOption('zip');
    check('ZIP包含Deflate64与PPMd', (await page.locator('#dlg-method option').allTextContents()).includes('Deflate64') && (await page.locator('#dlg-method option').allTextContents()).includes('PPMd'));
    check('ZIP不启用文件名加密', await page.locator('#dlg-encryptNames').isDisabled());
    await page.locator('#dlg-format').selectOption('wim');
    check('WIM创建仅存储', (await page.locator('#dlg-level').inputValue()) === '0' && await page.locator('#dlg-password').isDisabled());
    await page.locator('#dlg-format').selectOption('tar');
    assert.deepEqual(await page.locator('#dlg-method option').allTextContents(), ['GNU', 'POSIX']);
    checks.push('TAR头格式GNU/POSIX');
    await page.locator('#dlg-format').selectOption('tar.gz');
    check('TAR组合管线与扩展名联动', (await page.locator('#dlg-archive').inputValue()).endsWith('.tar.gz') && (await page.locator('#dlg-format-note').textContent()).includes('先创建 TAR'));
    await close();
    await selectFile('项目源代码').click();
    await toolbar('add');
    await page.locator('#dlg-format').selectOption('gzip');
    await page.locator('[data-dlg="submit"]').click();
    check('目录不能直接写单文件压缩流', (await page.locator('#dlg-result').textContent()).includes('只支持一个文件'));
    await close();
    await selectFile('资料归档.rar').dblclick();
    await selectFile('README.md').click();
    await menu(0, 'rename');
    check('只读归档阻止成员修改', (await page.locator('#p-dialog-title').textContent()).includes('只读'));
    await reset();
    await page.locator('#p-dual-button').click();
    await toolbar('copy');
    check('复制到预填另一面板路径', (await page.locator('#dlg-destination').inputValue()) === '~/Documents/');
    await close();
    await toolbar('test');
    await page.locator('#p-job-pause').click();
    check('作业可暂停', (await page.locator('#p-job-phase').textContent()) === '已暂停');
    await page.locator('#p-job-cancel').click();
    check('作业可取消', (await page.locator('#p-job-phase').textContent()) === '已取消');
    await reset();
    await menu(4, 'options');
    check('设置六页完整', await page.locator('#p-dialog-body [data-tab]').count() === 6);
    check('关联包含61个注册项', await page.locator('[id^="dlg-assoc-"]').count() === 61);
    check('设置关联每页12行', await page.locator('#p-dialog-body tbody tr:visible').count() === 12);
    await page.locator('[data-dlg="association-next"]').click();
    await page.locator('[data-dlg="associate-all"]').click();
    check('跨页全选61项', await page.locator('[id^="dlg-assoc-"]:checked').count() === 61);
    await page.locator('[data-tab="folders"]').click();
    await page.locator('#dlg-workFolder').selectOption('specified');
    check('指定临时目录可编辑', await page.locator('#dlg-workPath').isEnabled());
    await page.locator('[data-dlg="submit"]').click();
    check('设置提交保留结果', await page.locator('#p-dialog-region').isVisible() && (await page.locator('#dlg-result').textContent()).includes('保存'));
    await close();
    await page.locator('[data-menu="0"]').click();
    await page.locator('[data-submenu]').filter({ hasText: /^CRC SHA/ }).click();
    await page.locator('[data-command="hash-all"]:visible').click();
    await page.locator('[data-dlg="submit"]').click();
    check('十算法计算结果保留在窗口', await page.locator('#dlg-hash-results tbody tr').count() === 10 && await page.locator('#p-dialog-region').isVisible());
    await close();
    await page.locator('[data-menu="0"]').click();
    await page.locator('[data-submenu]').filter({ hasText: /^CRC SHA/ }).click();
    await page.locator('[data-command="hash-verify"]:visible').click();
    await page.locator('[data-dlg="submit"]').click();
    const verification = await page.locator('#dlg-hash-results').textContent();
    check('清单校验呈现通过、缺失、不匹配', ['通过','文件缺失','摘要不匹配'].every(x => verification.includes(x)));
    await close();
    await menu(4, 'benchmark');
    await page.locator('[data-dlg="submit"]').click();
    await page.locator('[data-dlg="bench-stop"]').click();
    check('基准测试可停止', (await page.locator('#dlg-bench-status').textContent()).includes('已停止'));
    await page.locator('[data-dlg="bench-restart"]').click();
    await close();
    await toolbar('info');
    await page.waitForTimeout(700);
    check('离开基准测试窗口不影响其他对话框', errors.length === 0);
    await close();
    for (const width of [1024, 736, 360]) {
      for (const colorScheme of ['light', 'dark']) {
        await page.setViewportSize({ width, height: 900 });
        await page.emulateMedia({ colorScheme });
        await reset();
        await noOverflow('主窗口适配 ' + width + ' / ' + colorScheme);
        await page.screenshot({ path: path.join(output, `main-${width}-${colorScheme}.png`), fullPage: true });
        await toolbar('add');
        await noOverflow('添加窗口适配 ' + width + ' / ' + colorScheme);
        await page.screenshot({ path: path.join(output, `add-${width}-${colorScheme}.png`), fullPage: true });
        await close();
        await page.locator('[data-menu="0"]').click();
        await page.locator('[data-submenu]').filter({ hasText: /^7-Zip/ }).click();
        await page.locator('[data-submenu]:visible').filter({ hasText: /按格式打开/ }).click();
        await page.waitForTimeout(100);
        const clipped = await page.evaluate(() => {
          const root = document.querySelector('#m7arc-fm').getBoundingClientRect();
          return [...document.querySelectorAll('.p-menu-popup button')].filter(button => {
            const rect = button.getBoundingClientRect();
            return rect.width > 0 && (rect.left < root.left - 1 || rect.right > root.right + 1 || rect.bottom > root.bottom + 1);
          }).map(button => button.textContent);
        });
        check('级联菜单可达 ' + width + ' / ' + colorScheme, clipped.length === 0);
        await page.locator('[data-menu="0"]').click();
        await page.locator('[data-command="formats"]').click();
        await noOverflow('格式目录适配 ' + width + ' / ' + colorScheme);
      }
    }
    check('浏览器无未捕获异常', errors.length === 0);
    fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify({ checks, errors, browser: await browser.version(), prototype: 'design/index.html' }, null, 2) + '\n');
    console.log(JSON.stringify({ passed: checks.length, errors }, null, 2));
  } finally {
    await browser.close();
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
