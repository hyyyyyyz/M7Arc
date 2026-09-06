(function () {
  'use strict';
  const esc = v => String(v ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const opt = (v, label, selected) => `<option value="${esc(v)}"${String(v) === String(selected) ? ' selected' : ''}>${esc(label ?? v)}</option>`;
  const input = (id, value = '', type = 'text', extra = '') => `<input class="p-input" id="dlg-${id}" name="${id}" type="${type}" value="${esc(value)}" ${extra}>`;
  const select = (id, values, selected) => `<select class="p-select" id="dlg-${id}" name="${id}">${values.map(v => Array.isArray(v) ? opt(v[0], v[1], selected) : opt(v,v,selected)).join('')}</select>`;
  const field = (label, html) => `<div class="p-field"><label${html.match(/id="([^"]+)"/) ? ` for="${html.match(/id="([^"]+)"/)[1]}"` : ''}>${label}</label>${html}</div>`;
  const check = (id, label, checked = false, extra = '') => `<label class="p-check"><input id="dlg-${id}" name="${id}" type="checkbox" ${checked ? 'checked' : ''} ${extra}> ${label}</label>`;
  const group = (label, html) => `<fieldset class="p-group"><legend>${label}</legend>${html}</fieldset>`;
  const note = (str, id = '') => `<p class="p-note"${id ? ` id="dlg-${id}"` : ''}>${str}</p>`;
  const table = (headers, rows, id = '') => `<table class="p-table"${id ? ` id="dlg-${id}"` : ''}><thead><tr>${headers.map(x=>`<th>${x}</th>`).join('')}</tr></thead><tbody>${rows.map(r=>`<tr>${r.map(v=>`<td>${v}</td>`).join('')}</tr>`).join('')}</tbody></table>`;
  const footer = (primary = '模拟执行', extra = '') => `<div id="dlg-result" class="p-result" role="status" hidden></div><div class="p-actions">${extra}<button type="button" data-dlg="cancel">取消</button>${primary ? `<button type="button" class="p-primary" data-dlg="submit">${primary}</button>` : ''}</div>`;
  const selection = ctx => (ctx.selection || []).map(s=>s.name).join('、') || '示例文件';
  const config = ctx => ctx.formats || window.M7ArcFormats;
  const choices = (ctx) => {
    const cfg = config(ctx);
    return `<optgroup label="直接创建 · ${cfg.createFormats.length} 种格式">${cfg.createFormats.map(f=>opt(f.id,f.label,ctx.formatId || '7z')).join('')}</optgroup><optgroup label="组合归档 · 两步管线">${cfg.presets.map(f=>opt(f.id,f.label,ctx.formatId)).join('')}</optgroup>`;
  };
  function add(ctx) {
    return `<div class="p-form">${field('压缩包名称 / Archive',input('archive','Archive.7z'))}<div class="p-grid p-grid-2"><div>${field('归档格式',`<select class="p-select" id="dlg-format" name="format">${choices(ctx)}</select>`)}${field('压缩等级',select('level',[],5))}${field('压缩方法',select('method',[]))}${field('字典大小',select('dictionary',['自动','64 KB','256 KB','1 MB','4 MB','16 MB','32 MB','64 MB','128 MB','256 MB','512 MB','1 GB'],'自动'))}${field('词大小 / 模型阶数',select('word',['自动','8','12','16','24','32','48','64','96','128','192','256','273'],'自动'))}${field('固实数据块大小',select('solid',['自动','非固实','16 MB','64 MB','256 MB','1 GB','4 GB','固实'],'自动'))}${field('CPU 线程数',select('threads',['自动','1','2','4','8','12','16','32','64'],'自动'))}${field('压缩内存上限',select('memory',['自动','25% RAM','50% RAM','75% RAM','90% RAM'],'自动'))}${field('压缩所需内存',`<output id="dlg-memory-compress">等待引擎估算</output>`)}${field('解压所需内存',`<output id="dlg-memory-extract">等待引擎估算</output>`)}${field('分卷大小，字节',input('volume','','text','placeholder="不分卷；或 100M、700M、2G" list="dlg-volume-presets"'))}<datalist id="dlg-volume-presets"><option value="100M"><option value="700M"><option value="4092M"><option value="25G"></datalist>${field('参数',input('parameters','','text','placeholder="例如：mtc=on mta=on"'))}</div><div>${field('更新模式',select('update',[['add','添加并替换文件'],['update','更新并添加文件'],['fresh','只更新已有文件'],['sync','同步文件']],'add'))}${field('路径模式',select('pathMode',[['relative','相对路径'],['full','完整路径'],['absolute','绝对路径']],'relative'))}${group('选项',check('sfx','创建 SFX 自解压程序（Windows .exe）')+check('shared','压缩共享文件')+check('delete','压缩成功后删除源文件'))}${group('加密',field('输入密码',input('password','','password'))+field('再次输入密码',input('password2','','password'))+check('showPassword','显示密码')+field('加密方法',select('encryption',[]))+check('encryptNames','加密文件名'))}${note('', 'format-note')}${note(`已选：${esc(selection(ctx))}`)}${note('内存估计将来自官方引擎；本原型不报告本机性能。')}</div></div><details class="p-group" id="dlg-more"><summary>更多选项 · 链接、元数据、时间精度与高级方法</summary><div class="p-grid p-grid-2"><div>${group('文件元数据',check('symlinks','存储符号链接',true)+check('hardlinks','存储硬链接')+check('streams','存储 NTFS 备用数据流',false,'disabled')+check('security','存储 NTFS 文件安全信息',false,'disabled'))}${check('advancedMethods','显示引擎高级压缩方法')}${field('预处理滤镜',select('filter',[]))}</div><div>${field('时间精度',select('timePrecision',['格式默认','秒','毫秒','微秒','100 纳秒','纳秒'],'格式默认'))}${field('修改时间',select('mtime',['格式默认','存储','不存储'],'格式默认'))}${field('创建时间',select('ctime',['格式默认','存储','不存储'],'格式默认'))}${field('访问时间',select('atime',['格式默认','存储','不存储'],'格式默认'))}${field('归档时间取最新文件',select('archiveTime',['格式默认','是','否'],'格式默认'))}${check('preserveAtime','不更改源文件访问时间',true)}</div></div>${note('NTFS 数据流与安全描述符保留为跨平台元数据能力；macOS 文件系统适配后按输入及格式能力启用。')}</details></div>${footer('模拟添加')}`;
  }
  function extract(ctx) {
    return `<div class="p-form">${field('解压到',input('destination','~/Downloads/Extracted/'))}<div class="p-grid p-grid-2"><div>${check('subfolder','创建归档同名子文件夹',true)}${field('子文件夹名称',input('subfolderName','Archive'))}${field('路径模式',select('pathMode',[['full','完整路径 · 保留归档目录'],['current','当前路径 · 相对当前归档目录'],['flat','不含路径 · 全部平铺']],'full'))}${note('', 'path-note')}${check('eliminateRoot','消除重复的根文件夹',true)}${field('覆盖模式',select('overwrite',[['ask','覆盖前询问'],['replace','直接覆盖'],['skip','跳过已有文件'],['renameNew','自动重命名新文件'],['renameExisting','自动重命名已有文件']],'ask'))}</div><div>${group('密码',field('归档密码',input('password','','password'))+check('showPassword','显示密码'))}${group('恢复元数据',check('restoreTimes','恢复时间',true)+check('restoreMode','恢复 POSIX 权限',true)+check('restoreSecurity','恢复 NTFS 文件安全信息',false,'disabled'))}${note(`范围：${esc(selection(ctx))}`)}</div></div><details class="p-group"><summary>高级路径模式</summary>${check('absolute','按归档中的绝对路径解压')}${note('启用后使用记录的绝对路径；只对含绝对路径的归档有意义。')}</details></div>${footer('模拟解压')}`;
  }
  function options(ctx) {
    const cfg = config(ctx);
    const labels = [['system','系统关联'],['menu','菜单'],['folders','文件夹'],['edit','查看 / 编辑 / Diff'],['settings','设置'],['language','语言']];
    const associationRows = cfg.formats.map(f=>[check('assoc-'+f.id,esc(f.name),['7z','zip','rar','gzip'].includes(f.id)),esc(f.extensions.join(', ')||'按签名识别'),f.kind==='hash'?'读取 / 生成摘要':f.write?'读取 / 创建':'读取']);
    const associationTable = `<table class="p-table" id="dlg-association-table"><thead><tr><th>格式</th><th>扩展名</th><th>能力</th></tr></thead><tbody>${associationRows.map((row,i)=>`<tr${i>=12?' hidden':''}>${row.map(value=>`<td>${value}</td>`).join('')}</tr>`).join('')}</tbody></table>`;
    const panels = {
      system:note('选择关联格式；正式版通过 macOS Launch Services 注册，遵循系统默认应用设置。')+`<div class="p-actions"><button type="button" data-dlg="associate-all">全选</button><button type="button" data-dlg="associate-none">清空</button></div>`+associationTable+`<div class="p-actions"><button type="button" data-dlg="association-prev" disabled>上一页</button><output id="dlg-association-page">1 / ${Math.ceil(associationRows.length/12)} 页 · ${associationRows.length} 种格式</output><button type="button" data-dlg="association-next">下一页</button></div>`,
      menu:check('finderMenu','在 Finder 服务菜单中显示 M7Arc',true)+check('cascadeMenu','使用 M7Arc 子菜单',true)+check('menuIcons','显示菜单图标',true)+check('menuElimRoot','解压时消除重复根目录',true)+group('上下文菜单命令',['打开归档','提取文件…','提取到当前目录','提取到同名文件夹','测试归档','添加到压缩包…','添加为 7z','添加为 ZIP','CRC / SHA'].map((x,i)=>check('menu-'+i,x,true)).join(''))+note('Windows 32 位 Shell 扩展入口与 Zone.Identifier 流没有同名 macOS 设置；Finder 集成与 macOS 下载来源信息由平台模块处理。'),
      folders:field('归档临时工作文件夹',select('workFolder',[['system','系统临时文件夹'],['current','当前文件夹'],['specified','指定文件夹']],'system'))+field('指定位置',input('workPath','~/Library/Caches/M7Arc/'))+check('removableOnly','仅对可移动磁盘使用',false),
      edit:field('查看器',input('viewer','系统默认查看器'))+field('编辑器',input('editor','TextEdit'))+field('Diff 程序',input('diff','FileMerge'))+note('从归档编辑文件时，关闭外部编辑器后提示保存回归档；嵌套归档按层回写。'),
      settings:['显示“..”条目','显示真实文件图标','整行选择','显示网格线','单击打开项目','替代选择模式','显示系统菜单'].map((x,i)=>check('setting-'+i,x,[0,1,2].includes(i))).join('')+check('largePages','使用大内存页（平台不支持）',false,'disabled')+field('解压允许的最大内存',input('extractMemory',4,'number','min="1" max="256"'))+note('单位 GB；超过限制时进入内存对话框，可降低线程或修改本次上限。'),
      language:field('界面语言',select('language',['跟随系统','简体中文','繁體中文','English','日本語','한국어','Deutsch','Français','Español'],'简体中文'))+note('正式版从随附的语言资源枚举完整语言列表，翻译采用稳定字符串 ID。')
    };
    return `<div class="p-tabs" role="tablist">${labels.map(([id,label],i)=>`<button type="button" class="p-tab${i===0?' active':''}" data-tab="${id}" role="tab" aria-selected="${i===0}">${label}</button>`).join('')}</div><div class="p-form">${labels.map(([id])=>`<section data-panel="${id}"${id==='system'?'':' hidden'}>${panels[id]}</section>`).join('')}</div>${footer('保存原型设置')}`;
  }
  function benchmark() {
    const blank = ['压缩 · 当前','压缩 · 平均','解压 · 当前','解压 · 平均'].map(n=>[n,'—','—','—','—','—']);
    return `<div class="p-form">${note('性能测试演示 · 表格中的演示结果不代表本机测量。')}<div class="p-grid p-grid-2">${field('字典大小',select('dictionary',['1 MB','2 MB','4 MB','8 MB','16 MB','32 MB','64 MB','128 MB'],'32 MB'))}${field('CPU 线程数',select('threads',['1','2','4','8','12','16','32'],'8'))}${field('运行轮数',select('passes',['1','3','5','10'],'3'))}${field('内存需求','<output id="dlg-bench-memory">由引擎计算</output>')}</div>${table(['项目','数据量','速度','CPU 使用率','评分 / 使用率','评分'],blank,'bench-table')}${field('总体评分','<output id="dlg-bench-score">—</output>')}${field('运行状态','<output id="dlg-bench-status">等待开始</output>')}${field('日志','<pre class="p-note" id="dlg-bench-log">LZMA Benchmark · 演示会话</pre>')}</div>${footer('开始模拟测试','<button type="button" data-dlg="bench-stop" disabled>停止</button><button type="button" data-dlg="bench-restart">重新开始</button>')}`;
  }
  function hash(ctx) {
    if(ctx.extra?.action==='hash-verify')return `<div class="p-form">${field('校验清单',input('manifest',(ctx.selection||[])[0]?.name?.match(/\.(sha256|sha1|md5|crc32|crc64)$/i)?ctx.selection[0].name:'checksums.sha256'))}${field('待校验文件根目录',input('destination',ctx.location||'~/Downloads/'))}${field('演示场景',select('verifyScenario',[['mixed','展示通过、缺失与不匹配'],['pass','全部通过'],['missing','文件缺失'],['mismatch','摘要不匹配']],'mixed'))}${note('校验清单记录文件名和期望摘要；正式版通过 Hash handler 与已注册算法读取文件并比对。')}<div id="dlg-hash-results"></div></div>${footer('模拟验证清单')}`;
    return `<div class="p-form">${note(`所选项目：${esc(selection(ctx))}`)}<div class="p-actions"><button type="button" data-dlg="hash-all">选择全部</button><button type="button" data-dlg="hash-none">清空</button></div>${group('摘要算法',`<div class="p-grid p-grid-2">${config(ctx).hashers.map(h=>check('hash-'+h.id,esc(h.name),ctx.extra?.hashAlgorithm ? ['*','hash-all'].includes(ctx.extra.hashAlgorithm)||ctx.extra.hashAlgorithm.toUpperCase()===h.id.toUpperCase() : ['CRC32','SHA256'].includes(h.id))).join('')}</div>`)}${field('保存摘要文件',select('hashFile',[['none','不保存'],['SHA256','SHA256 · .sha256'],['SHA1','SHA1 · .sha1']],ctx.extra?.action==='hash-save'?'SHA256':'none'))}${note('结果按单文件摘要、文件数据总和、数据与文件名总和分组。')}<div id="dlg-hash-results"></div></div>${footer('模拟计算')}`;
  }
  function split(ctx) {
    return `<div class="p-form">${note(`源文件：${esc(selection(ctx))}`)}${field('分割到',input('destination','~/Downloads/Parts/'))}${field('每卷大小，字节',input('volume','100M','text','list="dlg-split-presets"'))}<datalist id="dlg-split-presets"><option value="100M"><option value="700M"><option value="4092M"><option value="25G"></datalist>${note('支持 B / K / M / G / T，整数按二进制倍数解释。支持空格分隔多种分卷大小，后续分卷沿用最后一个大小。')}${field('输出示例','<output id="dlg-split-preview"></output>')}</div>${footer('模拟分割')}`;
  }
  function combine(ctx) {
    const name = (ctx.selection||[])[0]?.name || 'Archive.7z.001';
    const base = name.replace(/\.\d+$/,'');
    const isFirstPart = /\.0*1$/.test(name);
    return `<div class="p-form">${note('选择第一个分卷，按连续编号顺序合并。压缩分卷通常可直接打开首卷并提取。')}${field('合并到',input('destination','~/Downloads/Combined/'))}${field('输出文件名',input('name',base))}${table(['序号','示例分卷','状态'],[1,2,3].map(n=>[n,esc(base+'.'+String(n).padStart(3,'0')),'示例']))}${note(isFirstPart?'已选择首卷；上述分卷列表为演示数据。':'请选择 .001 首卷；可在文件面板中选择示例分卷。','combine-note')}</div>${footer('模拟合并')}`;
  }
  function info(ctx) {
    const tabs = [['item','文件'],['folder','文件夹'],['archive','当前归档'],['outer','外层归档']];
    const item = (ctx.selection||[])[0] || {name:'README.md',size:'4 KB'};
    const panels = {
      item:table(['属性','值'],[['名称',esc(item.name)],['大小',esc(item.size||'4 KB')],['压缩后大小','2 KB（示例）'],['修改时间','2026-09-05 14:32:18（示例）'],['属性','-rw-r--r--'],['加密','否'],['方法','LZMA2:24'],['CRC','—'],['固实块','0']]),
      folder:table(['属性','值'],[['路径',esc(ctx.location||'/')],['文件数','18（示例）'],['文件夹数','3（示例）'],['大小','2.4 MB（示例）']]),
      archive:table(['属性','值'],[['类型',esc(ctx.formatId||'7z')],['物理大小','846 KB（示例）'],['头部大小','642 B（示例）'],['方法','LZMA2:24'],['固实','是'],['块数','2'],['偏移','0'],['尾部大小','0'],['警告','无（示例）']]),
      outer:table(['层级','类型','路径'],[['当前','TAR','Demo.7z / package.tar'],['父层','7z','Demo.7z']])+note('嵌套归档逐层枚举 IFolderArcProps，包括签名偏移、警告与未成功打开层的属性。')
    };
    return `<div class="p-tabs" role="tablist">${tabs.map(([id,l],i)=>`<button type="button" class="p-tab${i===0?' active':''}" data-tab="${id}" role="tab" aria-selected="${i===0}">${l}</button>`).join('')}</div>${tabs.map(([id])=>`<section data-panel="${id}"${id==='item'?'':' hidden'}>${panels[id]}</section>`).join('')}${footer('复制属性摘要')}`;
  }
  function editSession(ctx) {
    return `<div class="p-form">${note('归档内的文件已在外部编辑器中修改。')}${table(['项目','位置'],[['文件',esc(selection(ctx))],['当前归档',esc(ctx.location||'Demo.7z / docs/')],['回写过程','临时文件 → 当前归档 → 外层归档（如存在）']])}${note('保存将把修改写回归档；放弃保留原归档；取消保留编辑会话。')}</div>${footer('模拟保存回归档','<button type="button" data-dlg="edit-discard">放弃修改</button>')}`;
  }
  function overwrite(ctx) {
    return `<div class="p-form">${note(`目标位置已存在 ${esc(selection(ctx))}。`)}${table(['属性','已有文件','传入文件'],[['大小','4 KB（示例）','6 KB（示例）'],['修改时间','2026-09-01 09:00','2026-09-05 14:32']])}</div><div id="dlg-result" class="p-result" role="status" hidden></div><div class="p-actions">${[['yes','替换'],['yesAll','全部替换'],['rename','自动重命名'],['no','跳过'],['noAll','全部跳过']].map(([id,l])=>`<button type="button" data-overwrite="${id}">${l}</button>`).join('')}<button type="button" data-dlg="cancel">取消</button></div>`;
  }
  function password(ctx) {
    return `<div class="p-form">${note(`归档：${esc(ctx.location || selection(ctx))}`)}${field('输入密码',input('password','','password'))}${check('showPassword','显示密码')}${note('密码仅用于当前原型会话。')}</div>${footer('模拟解锁')}`;
  }
  function sfx() {
    return `<div class="p-form">${field('目标平台',select('sfxTarget',[['windows','Windows · .exe'],['macos','macOS · 尚未实现']],'windows'))}${field('模块',select('sfxModule',['7z.sfx · 完整界面','7zCon.sfx · 控制台'],'7z.sfx · 完整界面'))}${field('输出文件',input('name','Archive.exe'))}${note('官方 SFX 模块是 Windows 可执行程序。macOS 自解压应用需要单独实现启动器、签名与解压界面；不能把 .exe 改名为 .app。')}</div>${footer('模拟生成 Windows SFX')}`;
  }
  function simple(type,ctx) {
    if(type==='delete')return `<div class="p-form">${note('从当前示例位置删除所选项目。')}${table(['名称','位置'],(ctx.selection||[]).map(s=>[esc(s.name),esc(ctx.location)]))}</div>${footer('模拟删除')}`;
    if(type==='link')return `<div class="p-form">${field('链接位置',input('name',((ctx.selection||[])[0]?.name||'file')+'.link'))}${field('链接目标',input('target',(ctx.location||'~/Downloads/')+((ctx.selection||[])[0]?.name||'file')))}${field('链接类型',select('linkType',[['hard','硬链接'],['symlinkFile','文件符号链接'],['symlinkDirectory','目录符号链接']],'symlinkFile'))}${note('硬链接与符号链接使用 macOS 原生文件系统接口。Windows Directory Junction 与 WSL reparse point 在 macOS 不作为创建类型。')}</div>${footer('模拟创建链接')}`;
    if(type==='comment')return `<div class="p-form">${note(esc(selection(ctx)))}${field('注释',`<textarea class="p-input" id="dlg-comment" name="comment" rows="4">${esc((ctx.selection||[])[0]?.comment||'')}</textarea>`)}</div>${footer('模拟保存注释')}`;
    const descriptions = {copy:'复制到',move:'移动到',rename:'新名称',createFolder:'文件夹名称',createFile:'文件名称'};
    const destination = ['copy','move'].includes(type);
    return `<div class="p-form">${note(`所选：${esc(selection(ctx))}`)}${field(descriptions[type]||'名称',input(destination?'destination':'name',destination?(ctx.extra?.otherLocation||'~/Downloads/'):type==='createFolder'?'新建文件夹':type==='createFile'?'新建文件.txt':(ctx.selection||[])[0]?.name||'文件'))}${destination?check('oppositePanel','使用另一面板的目录',Boolean(ctx.extra?.otherLocation),ctx.extra?.otherLocation?'':'disabled'):''}</div>${footer('模拟'+({copy:'复制',move:'移动',rename:'重命名',createFolder:'创建文件夹',createFile:'创建文件'}[type]||'执行'))}`;
  }
  function render(type, ctx = {}) {
    const renders = {add,extract,options,benchmark,hash,split,combine,info,editSession,overwrite,password,sfx};
    return (renders[type] || ((ctx)=>simple(type,ctx)))(ctx);
  }
  function bind(host, type, ctx = {}) {
    const q = id => host.querySelector('#dlg-'+id);
    const val = id => q(id)?.value || '';
    const checked = id => Boolean(q(id)?.checked);
    const on = (selector,event,fn) => host.querySelectorAll(selector).forEach(el=>el.addEventListener(event,fn));
    const report = text => {const out=q('result'); if(out){out.hidden=false;out.textContent=text;}};
    const payload = () => Object.fromEntries(Array.from(host.querySelectorAll('input[name],select[name],textarea[name]')).filter(el=>!el.disabled).map(el=>[el.name,el.type==='checkbox'?el.checked:el.value]));
    const action = (name,data={}) => {if(ctx.onAction)ctx.onAction(name,{...data,simulated:true});};
    on('[data-dlg="cancel"]','click',()=>ctx.close?.());
    on('[data-tab]','click',e=>{const key=e.currentTarget.dataset.tab;host.querySelectorAll('[data-tab]').forEach(b=>{b.classList.toggle('active',b.dataset.tab===key);b.setAttribute('aria-selected',String(b.dataset.tab===key));});host.querySelectorAll('[data-panel]').forEach(p=>p.hidden=p.dataset.panel!==key);});
    if(q('showPassword')) q('showPassword').addEventListener('change',()=>{['password','password2'].forEach(id=>{if(q(id))q(id).type=checked('showPassword')?'text':'password';});});
    const fill = (id,vals,keep) => {const el=q(id);if(!el)return;const v=keep??el.value;el.innerHTML=vals.map(x=>Array.isArray(x)?opt(x[0],x[1],v):opt(x,x,v)).join('');};
    const enable = (id,yes) => {if(q(id)){q(id).disabled=!yes;if(!yes&&q(id).type==='checkbox')q(id).checked=false;}};
    let addFormat;
    if(type==='add') {
      let previousExtension='7z';
      const cfg=config(ctx);
      const levelNames={0:'0 · 仅存储',1:'1 · 极速',3:'3 · 快速',5:'5 · 标准',7:'7 · 最大',9:'9 · 极限'};
      const refreshMethod=()=>{
        const f=addFormat;
        const method=val('method');
        const store=val('level')==='0'&&!['tar'].includes(f.id);
        fill('method',store?['Copy']:checked('advancedMethods')?f.methods:f.guiMethods,method);
        enable('method',!store&&q('method').options.length>0);
        const m=val('method');
        const detail=cfg.methodDetails[m]||{dictionaries:[],wordSizes:[]};
        const wordSizes=m==='PPMd'&&f.id==='zip'?detail.zipWordSizes:detail.wordSizes;
        const dictionaries=m==='PPMd'&&f.id==='zip'?detail.zipDictionaries:detail.dictionaries;
        enable('dictionary',!store&&dictionaries.length>1);
        enable('word',!store&&wordSizes.length>0);
        fill('dictionary',dictionaries.length?dictionaries:['不适用']);
        fill('word',wordSizes.length?['自动',...wordSizes]:['不适用']);
        q('dictionary').previousElementSibling.textContent=detail.dictionaryLabel||'字典大小';
        q('word').previousElementSibling.textContent=detail.wordSizeLabel||'词大小 / 模型阶数';
        const filterList=cfg.filters[f.id]||[];
        fill('filter',filterList.length?filterList:['不适用']);
        enable('filter',!store&&filterList.length>0);
        enable('solid',f.solid&&!store);
        enable('threads',f.multithread&&!store);
        enable('memory',!['tar','wim'].includes(f.id)&&!store);
        enable('sfx',f.id==='7z'&&(store||['LZMA','LZMA2','PPMd'].includes(m)));
        enable('volume',!checked('sfx'));
        q('memory-compress').textContent=store?'仅缓冲区 · 由引擎报告':'等待引擎估算';
        q('memory-extract').textContent=store?'仅缓冲区 · 由引擎报告':'等待引擎估算';
      };
      const refresh=()=>{
        const id=val('format');const preset=cfg.presets.find(p=>p.id===id);
        addFormat=cfg.createFormats.find(f=>f.id===(preset?preset.pipeline.at(-1):id));
        const f=addFormat;
        fill('level',f.levels.map(n=>[n,levelNames[n]||String(n)]),f.defaultLevel);
        const extension=preset?preset.extension:f.extension;
        const name=val('archive');
        if(name.endsWith('.'+previousExtension))q('archive').value=name.slice(0,-previousExtension.length)+extension;else if(name.endsWith('.exe')&&f.id!=='7z')q('archive').value=name.slice(0,-3)+extension;
        previousExtension=extension;
        ['password','password2','showPassword','encryption'].forEach(id=>enable(id,f.encryption.length>0));
        fill('encryption',f.encryption.length?f.encryption:['不支持加密']);
        enable('encryptNames',f.headerEncryption);
        enable('symlinks',['tar','wim'].includes(f.id)||Boolean(preset));
        enable('hardlinks',['tar','wim'].includes(f.id)||Boolean(preset));
        enable('advancedMethods',f.advancedMethods.length>0);
        q('format-note').textContent=(preset?'组合处理：先创建 TAR，再用 '+f.label+' 压缩。':f.singleStream?'单流格式：直接创建只接受一个文件；多个文件请选择 TAR 组合。':f.note||'')+' '+(f.encryption.length?'支持 '+f.encryption.join(' / ')+(f.headerEncryption?' 和文件名加密。':'。'):'此格式没有密码加密。');
        refreshMethod();
      };
      q('format').addEventListener('change',refresh);
      ['level','advancedMethods'].forEach(id=>q(id).addEventListener('change',refreshMethod));
      q('method').addEventListener('change',refreshMethod);
      q('sfx').addEventListener('change',()=>{enable('volume',!checked('sfx'));const name=val('archive');if(checked('sfx'))q('archive').value=name.replace(/\.[^.]+$/,'.exe');else if(name.endsWith('.exe'))q('archive').value=name.slice(0,-4)+'.'+previousExtension;});
      refresh();
    }
    if(type==='extract') {
      const refresh=()=>{enable('pathMode',!checked('absolute'));enable('subfolderName',checked('subfolder'));q('path-note').textContent=checked('absolute')?'使用归档中记录的绝对路径。':({full:'a/b/readme.txt → 目标/a/b/readme.txt',current:'当前目录 a/ 中的 b/readme.txt → 目标/b/readme.txt',flat:'a/b/readme.txt → 目标/readme.txt'}[val('pathMode')]);};
      ['absolute','pathMode','subfolder'].forEach(id=>q(id).addEventListener('change',refresh));refresh();
    }
    if(['copy','move'].includes(type)&&q('oppositePanel'))q('oppositePanel').addEventListener('change',()=>{if(checked('oppositePanel')&&ctx.extra?.otherLocation)q('destination').value=ctx.extra.otherLocation;});
    if(type==='options') {
      host.querySelectorAll('[name]').forEach(el=>{const value=ctx.extra?.settings?.[el.name];if(value!==undefined){if(el.type==='checkbox')el.checked=Boolean(value);else el.value=value;}});
      on('[data-dlg="associate-all"]','click',()=>host.querySelectorAll('[id^="dlg-assoc-"]').forEach(x=>x.checked=true));
      on('[data-dlg="associate-none"]','click',()=>host.querySelectorAll('[id^="dlg-assoc-"]').forEach(x=>x.checked=false));
      let associationPage=0;
      const associationRows=Array.from(q('association-table').querySelectorAll('tbody tr'));
      const associationPages=Math.ceil(associationRows.length/12);
      const showAssociationPage=()=>{
        associationRows.forEach((row,i)=>row.hidden=Math.floor(i/12)!==associationPage);
        q('association-page').textContent=(associationPage+1)+' / '+associationPages+' 页 · '+associationRows.length+' 种格式';
        host.querySelector('[data-dlg="association-prev"]').disabled=associationPage===0;
        host.querySelector('[data-dlg="association-next"]').disabled=associationPage===associationPages-1;
      };
      on('[data-dlg="association-prev"]','click',()=>{associationPage=Math.max(0,associationPage-1);showAssociationPage();});
      on('[data-dlg="association-next"]','click',()=>{associationPage=Math.min(associationPages-1,associationPage+1);showAssociationPage();});
      showAssociationPage();
      q('workFolder').addEventListener('change',()=>enable('workPath',val('workFolder')==='specified'));enable('workPath',val('workFolder')==='specified');
    }
    if(type==='hash') {
      on('[data-dlg="hash-all"]','click',()=>host.querySelectorAll('[id^="dlg-hash-"]').forEach(x=>x.checked=true));
      on('[data-dlg="hash-none"]','click',()=>host.querySelectorAll('[id^="dlg-hash-"]').forEach(x=>x.checked=false));
    }
    const parseVolumes=str=>str.replace(/\s+-.*$/, '').replace(/(\d)\s+([BKMGT])/gi,'$1$2').trim().split(/\s+/).map(v=>{const match=v.match(/^(\d+)([BKMGT]?)(?:i?B)?$/i);return match?Number(match[1])*Math.pow(1024,'BKMGT'.indexOf((match[2]||'B').toUpperCase())):NaN;});
    if(type==='split') {const update=()=>{q('split-preview').textContent=((ctx.selection||[])[0]?.name||'Archive.7z')+'.001, .002, .003…';};q('volume').addEventListener('input',update);update();}
    if(type==='sfx') q('sfxTarget').addEventListener('change',()=>{host.querySelector('[data-dlg="submit"]').disabled=val('sfxTarget')==='macos';});
    let benchTimer=null;
    if(type==='benchmark') {
      let pass=0;let running=false;const statusNode=q('bench-status');
      const stop=(status)=>{if(benchTimer)clearInterval(benchTimer);benchTimer=null;running=false;q('bench-status').textContent=status;host.querySelector('[data-dlg="bench-stop"]').disabled=true;host.querySelector('[data-dlg="submit"]').disabled=false;};
      const start=()=>{
        if(benchTimer)clearInterval(benchTimer);pass=0;running=true;
        const max=Number(val('passes')),threads=Number(val('threads')),dict=val('dictionary');
        host.querySelector('[data-dlg="bench-stop"]').disabled=false;host.querySelector('[data-dlg="submit"]').disabled=true;
        q('bench-status').textContent='模拟运行中 · 0 / '+max+' 轮';q('bench-log').textContent='LZMA · '+dict+' · '+threads+' 线程（模拟输入）';
        benchTimer=setInterval(()=>{
          if(!statusNode.isConnected||!host.contains(statusNode)||statusNode.closest('[hidden]')){clearInterval(benchTimer);return;}
          pass++;q('bench-status').textContent='模拟运行中 · '+pass+' / '+max+' 轮';
          const rows=['压缩 · 当前','压缩 · 平均','解压 · 当前','解压 · 平均'].map((n,i)=>[n,dict,'示例 · '+(i<2?'32,480':'295,120')+' KB/s','示例 · '+(threads*94)+'%','示例 · 5,720 MIPS','示例 · '+(threads*5720)+' MIPS']);
          q('bench-table').outerHTML=table(['项目','数据量','速度','CPU 使用率','评分 / 使用率','评分'],rows,'bench-table');
          q('bench-score').textContent='示例 · '+(threads*5720)+' MIPS';q('bench-log').textContent+='\n完成模拟第 '+pass+' 轮';
          if(pass>=max)stop('模拟测试完成 · '+max+' 轮');
        },600);
      };
      on('[data-dlg="bench-stop"]','click',()=>stop('模拟测试已停止 · '+pass+' 轮'));
      on('[data-dlg="bench-restart"]','click',start);
      on('[data-dlg="submit"]','click',start);
      ['dictionary','threads','passes'].forEach(id=>q(id).addEventListener('change',()=>{if(running)stop('参数已改变，重新开始以应用。');}));
      return;
    }
    on('[data-dlg="edit-discard"]','click',()=>{action('edit-discard',{names:selection(ctx)});ctx.close?.();});
    on('[data-overwrite]','click',e=>{action('overwrite',{decision:e.currentTarget.dataset.overwrite});ctx.close?.();});
    on('[data-dlg="submit"]','click',()=>{
      const data=payload();
      if(type==='add') {
        const cfg=config(ctx);const preset=cfg.presets.find(p=>p.id===val('format'));
        if(!val('archive').trim())return report('请输入归档名称。');
        if(addFormat.singleStream&&!preset&&((ctx.selection||[]).length>1||(ctx.selection||[]).some(s=>s.type==='folder'||s.kind==='folder'||s.isDir)))return report('该格式只支持一个文件。请选择 TAR + '+addFormat.label+' 组合归档。');
        if(!q('password').disabled&&val('password')!==val('password2'))return report('两次密码不一致。');
        if(val('volume')&&!q('volume').disabled&&parseVolumes(val('volume')).some(n=>!Number.isFinite(n)||n<=0))return report('请输入有效分卷大小，例如 100M 或 100M 700M。');
        data.pipeline=preset?preset.pipeline:[addFormat.id];data.name=val('archive');data.formatId=val('format');delete data.password;delete data.password2;data.hasPassword=Boolean(val('password'));
        action('add',data);report('模拟添加已提交：'+val('archive')+' · '+data.pipeline.join(' → ')+' · 更新模式：'+q('update').selectedOptions[0].textContent+'。');
      } else if(type==='extract') {
        if(!val('destination').trim())return report('请输入解压目标位置。');
        delete data.password;data.hasPassword=Boolean(val('password'));data.pathMode=checked('absolute')?'absolute':val('pathMode');
        action(type,data);report('模拟解压已提交：'+val('destination')+' · '+(checked('absolute')?'绝对路径':q('pathMode').selectedOptions[0].textContent)+'。');
      } else if(type==='hash') {
        if(ctx.extra?.action==='hash-verify') {
          if(!val('manifest').trim())return report('请选择校验清单。');
          const scenario=val('verifyScenario');
          const samples=[['README.md','通过'],['src/main.cpp','文件缺失'],['assets/icon.png','摘要不匹配']];
          const rows=samples.map((r,i)=>[esc(r[0]),'清单中的期望摘要',scenario==='mixed'?r[1]+'（示例）':({pass:'通过',missing:i===1?'文件缺失':'通过',mismatch:i===2?'摘要不匹配':'通过'}[scenario])+'（示例）']);
          q('hash-results').innerHTML=table(['文件','依据','验证结果'],rows);
          action('hash-verify',{manifest:val('manifest'),destination:val('destination'),scenario});
          return report('已展示清单验证结果 · 3 个示例文件。');
        }
        const hashers=config(ctx).hashers.filter(h=>checked('hash-'+h.id));
        if(!hashers.length)return report('请选择至少一种摘要算法。');
        q('hash-results').innerHTML=table(['算法','位数','单文件 / 文件数据摘要'],hashers.map(h=>[esc(h.name),h.bits,'演示 · 等待实际引擎摘要']))+note('多文件结果还包含数据与文件名总和。');
        action(type,{algorithms:hashers.map(h=>h.id),save:val('hashFile')});report('已展示 '+hashers.length+' 种摘要算法'+(val('hashFile')!=='none'?'；模拟输出 '+val('hashFile')+' 摘要文件。':'。'));
      } else if(type==='split') {
        const sizes=parseVolumes(val('volume'));if(sizes.some(n=>!Number.isFinite(n)||n<=0))return report('请输入有效分卷大小，例如 100M。');
        if((ctx.selection||[]).length!==1||(ctx.selection||[])[0]?.isDir||(ctx.selection||[])[0]?.type==='folder'||(ctx.selection||[])[0]?.kind==='folder')return report('分割需要选择一个普通文件。');
        action(type,{...data,volumeBytes:sizes});report('模拟分割：每卷 '+sizes.join(' / ')+' 字节，输出 '+val('destination')+'。');
      } else if(type==='combine') {
        if(!/\.0*1$/.test((ctx.selection||[])[0]?.name||''))return report('请选择首个 .001 分卷。');
        action(type,data);report('模拟合并：输出 '+val('destination')+val('name')+'。');
      } else if(type==='password') {
        if(!val('password'))return report('请输入密码以演示解锁。');action(type,{hasPassword:true});report('模拟解锁完成。');
      } else if(type==='info') {
        const text=host.querySelector('[data-panel]:not([hidden])').innerText;
        if(navigator.clipboard&&window.isSecureContext)navigator.clipboard.writeText(text).then(()=>report('属性摘要已复制。'),()=>report(text));else report(text);
      } else if(type==='options') {action(type,data);report('原型设置已保存到本次会话。');}
      else {if(q('name')&&!val('name').trim())return report('请输入名称。');action(type==='editSession'?'edit-save':type,data);report(type==='editSession'?'修改已在演示会话中保存回归档。':'模拟操作已提交。');}
    });
  }
  window.M7ArcDialogs={render,bind};
}());
