(function () {
  const root = document.getElementById('m7arc-fm');
  const get = id => root.querySelector('#' + id);
  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const formats = window.M7ArcFormats;
  const commands = window.M7ArcCommands;
  const MiB = 1024 * 1024;
  let nextId = 100;
  const file = (name, size, formatId = '', kind = 'file') => ({id:nextId++,name,size,formatId,kind,modified:'2026-09-06 14:32:08',attributes:kind === 'folder'?'D':'A',packed:formatId?Math.round(size*.63):size});
  const samples = [
    file('项目源代码',0,'','folder'),file('照片与素材',0,'','folder'),
    file('project.7z',18.4*MiB,'7z'),file('照片备份.zip',96.8*MiB,'zip'),
    file('资料归档.rar',32.7*MiB,'rar5'),file('linux-source.tar.xz',26.1*MiB,'xz'),
    file('服务器日志.tar.gz',8.2*MiB,'gzip'),file('手册.tar.bz2',5.6*MiB,'bzip2'),
    file('系统备份.wim',268*MiB,'wim'),file('Install.dmg',126*MiB,'dmg'),
    file('Release.iso',640*MiB,'iso'),file('Data.apfs',84*MiB,'apfs'),
    file('VirtualDisk.vhdx',512*MiB,'vhdx'),file('stream.zst',6.1*MiB,'zstd'),
    file('split.7z.001',10*MiB,'split'),file('说明.txt',2368),file('checksums.sha256',384,'hash')
  ];
  const localFolders = {'~/Downloads/':samples,'~/Documents/':[file('已解压文件',0,'','folder'),file('归档说明.md',4380)],'/Volumes/':[file('Macintosh HD',0,'','folder'),file('Work SSD',0,'','folder')]};
  const archiveContents = [file('文档',0,'','folder'),file('src',0,'','folder'),file('assets',0,'','folder'),file('nested.zip',3.2*MiB,'zip'),file('README.md',6804),file('LICENSE',26530),file('CMakeLists.txt',3912),file('说明.txt',2148)];
  const state = {dual:false,active:0,density:'standard',large:true,labels:true,archiveToolbar:true,standardToolbar:true,autoRefresh:true,time:'second',utc:false,menu:null,dialog:null,selectionAnchor:0,clipboard:null,job:null,settings:{},bookmarks:['~/Downloads/','~/Documents/','/Volumes/'],panels:[]};
  function panel(path) {return {path,mode:'local',formatId:'',stack:[],entries:localFolders[path]||[],selected:new Set(),view:'details',sort:'name',descending:false,flat:false,history:[path],columns:['size','packed','type','modified','attributes']};}
  state.panels = [panel('~/Downloads/'),panel('~/Documents/')];
  state.panels[0].selected.add(samples.find(f=>f.name==='project.7z').id);
  let jobTimer = null;
  let priorFocus = null;
  const current = () => state.panels[state.active];
  const chosen = () => current().entries.filter(f=>current().selected.has(f.id));
  const fmt = id => formats.formats.find(f=>f.id===id);
  function size(v) {if (!v) return '';if(v>=MiB)return (v/MiB).toLocaleString('en-US',{maximumFractionDigits:1})+' MiB';if(v>=1024)return (v/1024).toLocaleString('en-US',{maximumFractionDigits:1})+' KiB';return String(v)+' B';}
  function say(text) {get('p-message').textContent=text;}
  function fileType(f) {return f.kind==='folder'?'文件夹':f.formatId?(fmt(f.formatId)?.name||f.formatId)+' 归档':f.name.split('.').pop().toUpperCase()+' 文件';}
  const glyph = (name, css='p-chrome-icon') => '<i data-lucide="'+name+'" class="'+css+'" aria-hidden="true"></i>';
  function refreshIcons(){if(globalThis.lucide)lucide.createIcons({attrs:{width:16,height:16,'stroke-width':1.7}});}
  function icon(f) {
    const name=f.kind==='folder'?'folder':f.formatId==='iso'?'disc':['dmg','apfs','vhdx','wim'].includes(f.formatId)?'hard-drive':f.formatId?'file-archive':/\.(cpp|h|cmake)$/i.test(f.name)?'file-code':'file-text';
    return glyph(name,'p-file-icon'+(f.kind==='folder'?' p-folder':''));
  }
  function timeText(t) {const n = {day:10,minute:16,second:19,ntfs:27,ns:29}[state.time]||19;return (t+'.123456789').slice(0,n)+(state.utc?' UTC':'');}
  function writeCap(p=current()) {return p.mode==='local'||!!fmt(p.formatId)?.write;}
  function menuCheck(id,fallback) {
    if(id==='two-panels')return state.dual;
    if(id==='flat-view')return current().flat;
    if(id.startsWith('view-'))return current().view===id.slice(5);
    if(id.startsWith('sort-'))return current().sort===id.slice(5);
    if(id==='toolbar-archive')return state.archiveToolbar;
    if(id==='toolbar-standard')return state.standardToolbar;
    if(id==='toolbar-large')return state.large;
    if(id==='toolbar-text')return state.labels;
    if(id==='auto-refresh')return state.autoRefresh;
    if(id==='time-utc')return state.utc;
    if(id.startsWith('time-'))return state.time===id.slice(5);
    return fallback;
  }
  function menuItems(items) {
    return items.map(item=> {
      if(item.separator)return '<div class="p-menu-divider"></div>';
      const checked=menuCheck(item.id,item.checked);
      let label=item.label;
      if(/^favorite-\d+$/.test(item.id)){const b=state.bookmarks[Number(item.id.split('-')[1])];label=b||label+'（未设置）';}
      const labelHTML='<span class="p-checkmark">'+(checked?'✓':'')+'</span><span>'+esc(label)+'</span><span class="p-shortcut">'+esc(item.shortcut||(item.items?'›':''))+'</span>';
      if(item.items)return '<div><button type="button" data-submenu aria-expanded="false">'+labelHTML+'</button><div class="p-subitems" hidden>'+menuItems(item.items)+'</div></div>';
      return '<button type="button" data-command="'+esc(item.id)+'">'+labelHTML+'</button>';
    }).join('');
  }
  function menus() {
    get('p-menubar').innerHTML=commands.menus.map((m,i)=>'<div class="p-menu"><button type="button" data-menu="'+i+'" aria-haspopup="true" aria-expanded="'+(state.menu===i)+'">'+esc(m.label)+'</button><div class="p-menu-popup" '+(state.menu===i?'':'hidden')+'>'+menuItems(m.items)+'</div></div>').join('');
    fitMenus();
  }
  function fitMenus(){requestAnimationFrame(()=>{
    const win=root.querySelector('.p-window');win.style.minHeight='';
    const bounds=root.getBoundingClientRect();
    root.querySelectorAll('.p-subitems:not([hidden])').forEach(menu=>{menu.style.left='';menu.style.right='';if(innerWidth>600&&menu.getBoundingClientRect().right>bounds.right){menu.style.left='auto';menu.style.right='100%';}});
    const bottom=Math.max(win.getBoundingClientRect().bottom,...[...root.querySelectorAll('.p-menu-popup:not([hidden]),.p-subitems:not([hidden])')].map(x=>x.getBoundingClientRect().bottom));
    if(bottom>win.getBoundingClientRect().bottom)win.style.minHeight=(bottom-bounds.top+8)+'px';
  });}
  function toolbar() {
    const glyphs={add:'plus',extract:'arrow-down-to-line',test:'shield-check',copy:'copy',move:'folder-input',delete:'trash-2',info:'info'};
    get('p-toolbar-main').innerHTML=commands.toolbar.filter(c=>c.group==='archive'?state.archiveToolbar:state.standardToolbar).map(c=>'<button type="button" class="p-tool" data-command="'+c.id+'" aria-label="'+esc(c.description)+'"><span class="p-tool-symbol">'+glyph(glyphs[c.icon],'p-tool-icon')+'</span><span class="p-tool-label">'+esc(c.label)+'</span></button>'+((c.id==='test'&&state.standardToolbar)?'<span class="p-tool-separator"></span>':'')).join('');
    refreshIcons();
    get('p-format-summary').textContent=formats.formats.length+' 项注册 · '+formats.createFormats.length+' 类可创建';
    get('p-dual-button').setAttribute('aria-pressed',String(state.dual));
    root.dataset.density=state.density;root.dataset.large=String(state.large);root.dataset.labels=String(state.labels);
  }
  const columns={size:['大小','size-col'],packed:['压缩后','size-col'],type:['类型','type-col'],modified:['修改时间','date-col'],attributes:['属性','extra-col'],method:['方法','extra-col'],encrypted:['加密','extra-col'],crc:['CRC','extra-col'],block:['块','extra-col'],host:['主机系统','extra-col']};
  function entries(p) {
    let list=[...p.entries];
    if(p.flat){list=list.flatMap(f=>f.kind==='folder'?[{...f,name:f.name+'/内容.txt',kind:'file',size:2910}]:[f]);}
    if(p.sort!=='none')list.sort((a,b)=>{
      if(a.kind!==b.kind)return a.kind==='folder'?-1:1;
      let d=p.sort==='size'?a.size-b.size:p.sort==='type'?fileType(a).localeCompare(fileType(b)):p.sort==='date'?a.modified.localeCompare(b.modified):a.name.localeCompare(b.name,'zh-Hans',{numeric:true});return p.descending?-d:d;
    });return list;
  }
  function cell(f,key,p) {
    if(key==='size')return size(f.size);
    if(key==='packed')return p.mode==='archive'?size(f.kind==='folder'?0:Math.round(f.size*.43)):'';
    if(key==='type')return fileType(f);
    if(key==='modified')return timeText(f.modified);
    if(key==='method')return p.mode==='archive'?(p.formatId==='zip'?'Deflate':'LZMA2'):'';
    if(key==='encrypted')return p.mode==='archive'?'−':'';
    if(key==='crc')return f.kind==='folder'?'':'示例属性';
    if(key==='block')return p.mode==='archive'&&f.kind!=='folder'?'0':'';
    if(key==='host')return 'Unix';
    return f[key]||'';
  }
  function renderPanel(p,index) {
    const list=entries(p);const total=p.entries.reduce((a,f)=>a+f.size,0);const selected=p.entries.filter(f=>p.selected.has(f.id));
    let body='';
    if(p.view==='large'||p.view==='small'||p.view==='list'){
      body='<div class="p-icons '+(p.view!=='large'?'p-small-icons':'')+'">'+list.map(f=>'<button type="button" class="p-file-tile '+(p.selected.has(f.id)?'p-selected':'')+'" data-entry="'+f.id+'">'+icon(f)+'<span>'+esc(f.name)+'</span></button>').join('')+'</div>';
    }else{
      body='<table class="p-files" aria-label="'+esc(p.path)+'文件列表"><thead><tr><th class="p-name-col"><button type="button" data-sort="name">名称 '+(p.sort==='name'?(p.descending?'↓':'↑'):'')+'</button></th>'+p.columns.map(k=>'<th class="p-'+columns[k][1]+'"><button type="button" data-sort="'+({size:'size',type:'type',modified:'date'}[k]||'columns')+'">'+columns[k][0]+'</button></th>').join('')+'</tr></thead><tbody><tr><td><button type="button" data-command="up" class="p-filename">'+glyph('corner-left-up','p-file-icon')+'<span>..</span></button></td>'+p.columns.map(k=>'<td class="p-'+columns[k][1]+'"></td>').join('')+'</tr>'+list.map(f=>'<tr class="'+(p.selected.has(f.id)?'p-selected':'')+'" data-row="'+f.id+'" aria-selected="'+p.selected.has(f.id)+'"><td><button type="button" data-entry="'+f.id+'" class="p-filename">'+icon(f)+'<span>'+esc(f.name)+'</span></button></td>'+p.columns.map(k=>'<td class="p-'+columns[k][1]+' '+(['size','packed'].includes(k)?'p-number':'')+'">'+esc(cell(f,k,p))+'</td>').join('')+'</tr>').join('')+'</tbody></table>';
    }
    return '<section class="p-panel '+(state.active===index?'p-active':'')+'" data-panel="'+index+'" aria-label="'+(index?'右':'左')+'文件面板"><div class="p-address"><button type="button" data-command="up" aria-label="向上一级">'+glyph('chevron-up')+'</button><form data-address-form>'+glyph('folder-open','p-location-icon')+'<input aria-label="'+(index?'右':'左')+'面板地址" value="'+esc(p.path)+'"><button type="button" class="p-history" data-command="history" aria-label="文件夹历史">'+glyph('chevron-down')+'</button></form><button type="button" data-command="columns" aria-label="选择详细信息列">'+glyph('sliders-horizontal')+'</button></div><div class="p-list-host">'+body+'</div><div class="p-panel-status"><span>'+selected.length+' / '+p.entries.length+' 项</span><span>'+size(selected.reduce((a,f)=>a+f.size,0))+'</span><span>'+size(total)+'</span><span>'+(p.mode==='local'?'本地目录':esc(fmt(p.formatId)?.name||p.formatId)+(writeCap(p)?' · 可更新':' · 只读'))+'</span></div></section>';
  }
  function panels() {get('p-panels').classList.toggle('p-dual',state.dual);get('p-panels').innerHTML=state.panels.slice(0,state.dual?2:1).map(renderPanel).join('');refreshIcons();}
  function redraw(){menus();toolbar();panels();}
  function snapshot(p){return {path:p.path,mode:p.mode,formatId:p.formatId,entries:p.entries};}
  function navigate(path) {
    const p=current();p.stack=[];p.path=path;p.mode='local';p.formatId='';
    p.entries=localFolders[path]||(localFolders[path]=[file('README.md',4520),file('资源',0,'','folder')]);p.selected.clear();p.history.unshift(path);p.history=[...new Set(p.history)].slice(0,16);panels();say(path);
  }
  function openEntry(f,openMode='auto') {
    if(!f)return;
    const p=current();
    if(f.kind==='folder'||f.formatId){
      p.stack.push(snapshot(p));p.path+=f.name+'/';p.selected.clear();
      if(f.formatId){
        p.mode='archive';p.formatId=f.formatId;
        const type=fmt(f.formatId);
        if(openMode==='parser'||openMode==='parser-exact')p.entries=[file('[0] '+f.name,f.size,f.formatId),file('[1] tail',128)];
        else if(type?.kind==='stream')p.entries=[file(f.name.replace(/\.(gz|bz2|xz|zst|zstd|lzma|b64|lzh)$/i,''),f.size*2,/\.tar\.(gz|bz2|xz)$/i.test(f.name)?'tar':'')];
        else if(f.formatId==='hash')p.entries=[file('README.md',6804),file('说明.txt',2148)];
        else p.entries=archiveContents.map(x=>({...x,id:nextId++}));
      }
      else p.entries=[file('说明.md',4632),file('guide.pdf',1.7*MiB),file('example.cpp',9201),file('nested.zip',3.8*MiB,'zip')];
      p.history.unshift(p.path);panels();say((f.formatId?'已打开 '+fileType(f):'已进入目录')+({one:' · 单层打开 *',parser:' · 解析模式 #','parser-exact':' · 精确解析 #:e'}[openMode]||'')+' · 示例内容');
    }else showDialog('preview',{entry:f});
  }
  function up(){const p=current();if(p.stack.length){Object.assign(p,p.stack.pop());p.selected.clear();panels();say('返回 '+p.path);}else navigate('/Volumes/');}
  function select(id,event){const p=current(),list=entries(p),idx=list.findIndex(f=>f.id===id);if(event.shiftKey){const start=Math.min(idx,state.selectionAnchor),end=Math.max(idx,state.selectionAnchor);list.slice(start,end+1).forEach(f=>p.selected.add(f.id));}else if(event.metaKey||event.ctrlKey){p.selected.has(id)?p.selected.delete(id):p.selected.add(id);state.selectionAnchor=idx;}else{p.selected=new Set([id]);state.selectionAnchor=idx;}
    const area=get('p-panels').querySelector('.p-panel[data-panel="'+state.active+'"]');
    area.querySelectorAll('[data-row],.p-file-tile').forEach(row=>{const selected=p.selected.has(Number(row.dataset.row||row.dataset.entry));row.classList.toggle('p-selected',selected);row.setAttribute('aria-selected',String(selected));});
    const selected=p.entries.filter(f=>p.selected.has(f.id));const status=area.querySelector('.p-panel-status');status.children[0].textContent=selected.length+' / '+p.entries.length+' 项';status.children[1].textContent=size(selected.reduce((sum,f)=>sum+f.size,0));say('已选择 '+p.selected.size+' 项');}
  function closeMenus(){state.menu=null;menus();}
  function closeDialog(){state.dialog=null;get('p-dialog-region').hidden=true;get('p-panels').hidden=false;if(priorFocus?.isConnected)priorFocus.focus();}
  function customDialog(title,html,bind){priorFocus=document.activeElement;state.dialog=title;get('p-dialog-title').textContent=title;get('p-dialog-body').innerHTML=html;get('p-dialog-region').hidden=false;get('p-panels').hidden=true;bind?.(get('p-dialog-body'));get('p-dialog-close').focus();}
  const footer='<div class="p-actions"><span class="p-note">交互原型 · 示例数据</span><button type="button" data-dialog-close>关闭</button></div>';
  function showDialog(type,extra={}){
    if(type==='formats')return catalog();
    if(type==='columns')return customDialog('选择详细信息列','<div class="p-grid-2">'+Object.entries(columns).map(([k,v])=>'<label class="p-check"><input type="checkbox" data-column="'+k+'" '+(current().columns.includes(k)?'checked':'')+'>'+v[0]+'</label>').join('')+'</div>'+footer,host=>host.addEventListener('change',e=>{if(e.target.dataset.column){current().columns=[...host.querySelectorAll('[data-column]:checked')].map(x=>x.dataset.column);panels();}}));
    if(type==='history')return customDialog('文件夹历史','<table class="p-table"><tbody>'+current().history.map(p=>'<tr><td><button type="button" data-go="'+esc(p)+'">'+esc(p)+'</button></td></tr>').join('')+'</tbody></table>'+footer,host=>host.addEventListener('click',e=>{if(e.target.dataset.go){closeDialog();navigate(e.target.dataset.go);}}));
    if(type==='select-mask'||type==='deselect-mask')return customDialog(type==='select-mask'?'选择文件':'取消选择','<form id="p-mask-form"><label class="p-field">通配符<input class="p-input" id="p-mask" value="*.7z"></label><p class="p-note">* 匹配任意字符，? 匹配单个字符。选择作用于当前面板。</p><div class="p-actions"><button type="button" data-dialog-close>取消</button><button class="p-primary" type="submit">应用选择</button></div></form>',host=>host.querySelector('form').addEventListener('submit',e=>{e.preventDefault();const pattern=host.querySelector('input').value;const rx=new RegExp('^'+pattern.replace(/[.+^${}()|[\]\\]/g,'\\$&').replace(/\*/g,'.*').replace(/\?/g,'.')+'$','i');current().entries.filter(f=>rx.test(f.name)).forEach(f=>type==='select-mask'?current().selected.add(f.id):current().selected.delete(f.id));closeDialog();panels();}));
    if(type==='preview'||type==='view-file'||type==='diff'){
      const f=extra.entry||chosen()[0];return customDialog(type==='diff'?'比较差异':'查看 — '+(f?.name||'README.md'),'<div class="p-note">'+esc(f?.name||'README.md')+' · 所选归档成员的预览示例</div><div class="p-result"><pre style="white-space:pre-wrap;margin:0">'+esc(type==='diff'?'  # M7Arc\n− Version: 0.1\n+ Version: 1.0\n  7-Zip file manager for macOS':'# M7Arc\n\n归档内容可以直接浏览、查看和编辑。\n此文件为交互原型中的示例内容。\n\n7-Zip 26.03 · C++ · Qt Widgets')+'</pre></div>'+footer);
    }
    if(type==='streams')return customDialog('备用数据流与 Mac 扩展属性','<div class="p-tabs"><button class="p-tab" aria-selected="true">流与扩展属性</button></div><table class="p-table"><thead><tr><th>名称</th><th>数据类型</th><th>大小</th></tr></thead><tbody><tr><td>com.apple.ResourceFork</td><td>Mac 扩展属性</td><td>8.2 KiB</td></tr><tr><td>com.apple.metadata:_kMDItemUserTags</td><td>Finder 标签</td><td>64 B</td></tr><tr><td>Zone.Identifier</td><td>归档中的 NTFS 流</td><td>26 B</td></tr></tbody></table><p class="p-note">属性展示示例。归档流和 Mac 扩展属性分别保存与恢复。</p>'+footer);
    if(type==='temp-cleanup')return customDialog('临时文件','<table class="p-table"><thead><tr><th>选择</th><th>文件夹</th><th>用途</th><th>大小</th></tr></thead><tbody><tr><td><input type="checkbox" data-temp></td><td>M7Arc-preview-01</td><td>已结束预览</td><td>1.8 MiB</td></tr><tr><td><input type="checkbox" disabled></td><td>M7Arc-edit-02</td><td>待回写编辑文件</td><td>6.4 KiB</td></tr></tbody></table><div class="p-actions"><button type="button" id="p-clean-temp">删除所选示例项</button><button type="button" data-dialog-close>关闭</button></div>',host=>host.querySelector('#p-clean-temp').onclick=()=>{host.querySelectorAll('[data-temp]:checked').forEach(x=>x.closest('tr').remove());say('已清理所选示例临时项');});
    if(type==='help'||type==='about')return customDialog(type==='about'?'关于 M7Arc':'帮助内容',type==='about'?'<div class="p-result"><strong>M7Arc · 7-Zip File Manager for macOS</strong><p>引擎基线：7-Zip 26.03</p><p>'+formats.formats.length+' 个格式注册项 · '+formats.createFormats.length+' 类归档写入 · '+formats.hashers.length+' 个摘要算法</p><p>7-Zip Copyright © 1999–2026 Igor Pavlov</p><p class="p-note">这是可交互设计原型，文件操作使用内置示例数据。</p></div>'+footer:'<ul class="p-help-list"><li>主窗口：文件 / 编辑 / 查看 / 收藏夹 / 工具 / 帮助。</li><li>双击归档进入，向上返回。F9 切换单双面板。</li><li>添加：七类真实可写格式、全部方法、更新、加密、分卷与内存设置。</li><li>格式与方法：搜索所有 handler、扩展别名、读写边界与算法。</li><li>文件菜单：内部打开、包内编辑、分割合并、CRC、链接和备用流。</li><li>工具菜单：选项、基准测试、临时文件管理。</li><li>点击列标题排序，地址栏右侧选择动态属性列。</li><li>交互结果仅影响示例数据，原型不会读取或改写本机文件。</li></ul>'+footer);
    const aliases={'create-folder':'createFolder','create-file':'createFile','edit-file':'editSession','hash-save':'hash','hash-verify':'hash'};
    const actual=aliases[type]||type;
    const titles={add:'添加到压缩包',extract:'提取文件',options:'选项',benchmark:'基准测试',hash:'CRC / SHA — 校验和',split:'分割文件',combine:'合并文件',copy:'复制到',move:'移动到',rename:'重命名',createFolder:'新建文件夹',createFile:'新建文件',info:'属性',editSession:'编辑归档中的文件',overwrite:'确认文件替换',password:'输入密码',sfx:'创建自解压文件',delete:'删除',link:'创建链接',comment:'注释'};
    const selection=chosen().map(f=>({...f,type:f.kind,isDir:f.kind==='folder'}));const context={formats,selection,location:current().path,mode:current().mode,formatId:extra.formatId||selection[0]?.formatId||current().formatId||'7z',extra:{...extra,action:type,settings:state.settings,otherLocation:state.dual?state.panels[1-state.active].path:undefined},onAction:dialogAction,close:closeDialog};
    customDialog(titles[actual]||type,window.M7ArcDialogs.render(actual,context),host=>window.M7ArcDialogs.bind(host,actual,context));
  }
  function dialogAction(action,payload={}) {
    const chosenEntries=chosen();const p=current();
    if(['options','save-options'].includes(action)){state.settings={...payload};say('设置已保存到本次原型会话');return;}
    if(['hash','hash-verify','split','combine'].includes(action)){say('已更新模拟结果');return;}
    closeDialog();
    if(['rename','createFolder','createFile','create-folder','create-file'].includes(action)){
      const name=payload.name||payload.newName||payload.value||'新建项目';
      if(action==='rename'&&chosenEntries[0])chosenEntries[0].name=name;else p.entries.push(file(name,0,'',action.toLowerCase().includes('folder')?'folder':'file'));
      panels();say('示例文件列表已更新');return;
    }
    if(action==='delete'){p.entries=p.entries.filter(f=>!p.selected.has(f.id));p.selected.clear();panels();say('已从示例列表删除所选项');return;}
    if(action==='edit-discard'||action==='discard'){say('已放弃示例编辑内容');return;}
    startJob(action,payload);
  }
  function startJob(action,payload={}) {
    if(jobTimer)clearInterval(jobTimer);
    const p=current();const src=chosen().map(f=>({...f}));const actionName={add:'创建归档',extract:'提取文件',test:'测试归档',copy:'复制文件',move:'移动文件','edit-save':'更新归档',save:'更新归档',split:'分割文件',combine:'合并文件'}[action]||action;
    const targetPath=payload.destination||state.panels[1-state.active].path;
    state.job={action,title:'模拟：'+actionName,value:0,paused:false,detail:payload.name||payload.archiveName||src[0]?.name||p.path};
    const region=get('p-progress-region');region.hidden=false;
    region.innerHTML='<div class="p-progress-head"><strong id="p-job-title">'+esc(state.job.title)+'</strong><span id="p-job-percent">0%</span></div><progress id="p-job-progress" max="100" value="0" aria-label="模拟任务"></progress><div class="p-progress-stats"><span id="p-job-detail">'+esc(state.job.detail)+'</span><span id="p-job-phase">扫描输入</span><span>示例任务</span><div class="p-progress-controls"><button type="button" id="p-job-pause">暂停</button><button type="button" id="p-job-cancel">取消</button><button type="button" id="p-job-hide">收起</button></div></div>';
    get('p-job-pause').onclick=()=>{state.job.paused=!state.job.paused;get('p-job-pause').textContent=state.job.paused?'继续':'暂停';get('p-job-phase').textContent=state.job.paused?'已暂停':'正在处理';};
    get('p-job-cancel').onclick=()=>{clearInterval(jobTimer);get('p-job-phase').textContent='已取消';get('p-job-pause').disabled=true;get('p-job-cancel').disabled=true;say('模拟作业已取消，示例源文件保留');};
    get('p-job-hide').onclick=()=>{region.hidden=true;};
    jobTimer=setInterval(()=>{
      if(state.job.paused)return;state.job.value=Math.min(100,state.job.value+10);
      get('p-job-progress').value=state.job.value;get('p-job-percent').textContent=state.job.value+'%';get('p-job-phase').textContent=state.job.value<20?'扫描输入':state.job.value<90?'正在处理':'正在完成写入';
      if(state.job.value===100){clearInterval(jobTimer);get('p-job-phase').textContent='模拟完成';get('p-job-pause').disabled=true;get('p-job-cancel').disabled=true;
        if(action==='add'){const id=payload.formatId||payload.format||'7z';const preset=formats.presets.find(x=>x.id===id);const out=file(payload.archiveName||payload.name||'新建归档.'+(formats.createFormats.find(x=>x.id===id)?.extension||id),4.6*MiB,preset?preset.pipeline.at(-1):id);(p.mode==='local'?p.entries:samples).push(out);}
        if(action==='copy'||action==='move'){const target=state.panels.find(x=>x.path===targetPath);const destination=target?target.entries:(localFolders[targetPath]||(localFolders[targetPath]=[]));destination.push(...src.map(f=>({...f,id:nextId++})));if(action==='move'){const ids=new Set(src.map(f=>f.id));p.entries=p.entries.filter(f=>!ids.has(f.id));if(p.mode==='local')localFolders[p.path]=p.entries;p.selected.clear();}}
        panels();say(state.job.title+'已完成 · 仅内置示例数据');
      }
    },320);
  }
  function catalog(){
    const data={tab:'formats',query:'',mode:'all',category:'all',page:0};
    const categories=[...new Set(formats.formats.map(f=>f.category))];
    customDialog('格式与方法 — 7-Zip 26.03','<div class="p-tabs" role="tablist"><button type="button" class="p-tab" data-cat-tab="formats" aria-selected="true">归档与容器</button><button type="button" class="p-tab" data-cat-tab="codecs" aria-selected="false">压缩方法与过滤器</button><button type="button" class="p-tab" data-cat-tab="hashers" aria-selected="false">校验和算法</button></div><div id="p-format-catalog"><div class="p-catalog-controls"><input class="p-input" id="p-format-search" aria-label="搜索格式或扩展名" placeholder="搜索名称或扩展名，例如 rar、dmg、docx"><select class="p-select" id="p-format-mode" aria-label="筛选能力"><option value="all">全部能力</option><option value="write">可创建归档</option><option value="read">只读格式</option></select><select class="p-select" id="p-format-category" aria-label="格式分类"><option value="all">全部类别</option>'+categories.map(c=>'<option>'+esc(c)+'</option>').join('')+'</select></div><div class="p-catalog-summary"><span id="p-catalog-count"></span><span>七类归档写入；Hash 为摘要清单</span></div><div id="p-catalog-table"></div><div class="p-catalog-pages"><button type="button" id="p-catalog-prev">上一页</button><span id="p-catalog-page"></span><button type="button" id="p-catalog-next">下一页</button></div></div><div id="p-algorithm-table" hidden></div><div id="p-format-details" hidden class="p-result"></div>'+footer,host=>{
      const render=()=>{
        const list=formats.formats.filter(f=>(f.name+' '+f.extensions.join(' ')).toLowerCase().includes(data.query.toLowerCase())&&(data.mode==='all'||data.mode==='write'&&f.write&&f.id!=='hash'||data.mode==='read'&&!f.write)&&(data.category==='all'||f.category===data.category));
        const pages=Math.max(1,Math.ceil(list.length/16));data.page=Math.min(data.page,pages-1);
        host.querySelector('#p-catalog-count').textContent='显示 '+list.length+' / '+formats.formats.length+' 个注册项';
        host.querySelector('#p-catalog-table').innerHTML='<table class="p-table"><thead><tr><th class="p-catalog-name">格式</th><th class="p-catalog-ext">扩展名 / 别名</th><th class="p-catalog-cap">能力</th><th class="p-catalog-category">类别</th></tr></thead><tbody>'+list.slice(data.page*16,data.page*16+16).map(f=>'<tr><td><button type="button" data-format-detail="'+f.id+'">'+esc(f.name)+'</button></td><td>'+esc(f.extensions.map(x=>'.'+x).join(' '))+'</td><td class="'+(f.write?'p-mode-write':'p-mode-read')+'">'+(f.id==='hash'?'摘要读写':f.write?'读取 / 创建':'读取 / 提取')+'</td><td class="p-catalog-category">'+esc(f.category)+'</td></tr>').join('')+'</tbody></table>';
        host.querySelector('#p-catalog-page').textContent=(data.page+1)+' / '+pages;
        host.querySelector('#p-catalog-prev').disabled=data.page===0;host.querySelector('#p-catalog-next').disabled=data.page>=pages-1;
      };
      host.querySelector('#p-format-search').oninput=e=>{data.query=e.target.value;data.page=0;render();};
      host.querySelector('#p-format-mode').onchange=e=>{data.mode=e.target.value;data.page=0;render();};
      host.querySelector('#p-format-category').onchange=e=>{data.category=e.target.value;data.page=0;render();};
      host.querySelector('#p-catalog-prev').onclick=()=>{data.page--;render();};host.querySelector('#p-catalog-next').onclick=()=>{data.page++;render();};
      host.addEventListener('click',e=>{
        const button=e.target.closest('button');if(!button)return;
        if(button.dataset.catTab){data.tab=button.dataset.catTab;host.querySelectorAll('[data-cat-tab]').forEach(b=>b.setAttribute('aria-selected',String(b===button)));host.querySelector('#p-format-catalog').hidden=data.tab!=='formats';host.querySelector('#p-algorithm-table').hidden=data.tab==='formats';host.querySelector('#p-format-details').hidden=true;
          const arr=data.tab==='codecs'?formats.codecs:formats.hashers;
          host.querySelector('#p-algorithm-table').innerHTML='<table class="p-table"><thead><tr><th>名称</th><th>能力 / 用途</th></tr></thead><tbody>'+arr.map(x=>'<tr><td>'+esc(typeof x==='string'?x:x.name||x.id)+'</td><td>'+esc(typeof x==='string'?'上游注册方法':x.note||x.description||x.capability||x.kind||[x.encode?'编码':'',x.decode?'解码':'',x.filter?'过滤器':''].filter(Boolean).join(' / ')||'摘要计算')+'</td></tr>').join('')+'</tbody></table>';
        }
        if(button.dataset.formatDetail){const f=fmt(button.dataset.formatDetail);const details=host.querySelector('#p-format-details');details.hidden=false;details.innerHTML='<dl><dt>格式</dt><dd>'+esc(f.name)+'</dd><dt>能力</dt><dd>'+esc(f.note||'遵循上游 handler 能力')+'</dd><dt>扩展名</dt><dd>'+esc(f.extensions.join(', '))+'</dd></dl><div class="p-actions"><button type="button" data-open-format="'+f.id+'">打开此类示例</button>'+(f.write&&f.id!=='hash'?'<button type="button" class="p-primary" data-create-format="'+f.id+'">创建 '+esc(f.name)+'…</button>':'')+'</div>';}
        if(button.dataset.openFormat){const id=button.dataset.openFormat;closeDialog();openEntry(file('example.'+(fmt(id).extensions[0]||id),8.4*MiB,id));}
        if(button.dataset.createFormat){showDialog('add',{formatId:button.dataset.createFormat});}
      });render();
    });
  }
  function command(id){
    closeMenus();const p=current();
    if(id==='two-panels'){state.dual=!state.dual;if(!state.dual)state.active=0;toolbar();panels();return;}
    if(id==='up')return up();if(id==='root')return navigate('/Volumes/');
    if(['open','open-inside','open-inside-one','open-inside-parser','open-inside-parser-exact'].includes(id)||id.startsWith('open-as-')){
      const selected=chosen()[0];const mode={'open-inside-one':'one','open-inside-parser':'parser','open-inside-parser-exact':'parser-exact'}[id]||'auto';
      return openEntry(id.startsWith('open-as-')&&selected?{...selected,formatId:id.slice(8)}:selected,mode);
    }
    if(id==='open-outside')return showDialog('preview');
    if(id.startsWith('view-')&&['large','small','list','details'].includes(id.slice(5))){p.view=id.slice(5);panels();return;}
    if(id.startsWith('sort-')){const s=id.slice(5);p.descending=p.sort===s?!p.descending:false;p.sort=s;panels();return;}
    if(id==='flat-view'){p.flat=!p.flat;panels();return;}
    if(id.startsWith('time-')){if(id==='time-utc')state.utc=!state.utc;else state.time=id.slice(5);panels();return;}
    const toolbarOptions={'toolbar-archive':'archiveToolbar','toolbar-standard':'standardToolbar','toolbar-large':'large','toolbar-text':'labels'};
    if(toolbarOptions[id]){state[toolbarOptions[id]]=!state[toolbarOptions[id]];toolbar();return;}
    if(id==='auto-refresh'){state.autoRefresh=!state.autoRefresh;say(state.autoRefresh?'自动刷新已开启':'自动刷新已关闭');return;}
    if(id==='refresh'){panels();say('已刷新当前示例目录');return;}
    if(id==='select-all'){p.entries.forEach(f=>p.selected.add(f.id));panels();return;}
    if(id==='deselect-all'){p.selected.clear();panels();return;}
    if(id==='invert-selection'){p.entries.forEach(f=>p.selected.has(f.id)?p.selected.delete(f.id):p.selected.add(f.id));panels();return;}
    if(id==='select-type'||id==='deselect-type'){const extension=(chosen()[0]?.name||'project.7z').split('.').pop();p.entries.filter(f=>f.name.endsWith('.'+extension)).forEach(f=>id==='select-type'?p.selected.add(f.id):p.selected.delete(f.id));panels();return;}
    if(id.startsWith('favorite-set-')){state.bookmarks[Number(id.split('-').pop())]=p.path;say('已保存收藏夹：'+p.path);return;}
    if(/^favorite-\d+$/.test(id)){const path=state.bookmarks[Number(id.split('-')[1])];if(path)navigate(path);else say('此书签尚未设置，请使用“添加当前文件夹为”');return;}
    if(id==='test')return startJob('test');
    if(['extract-here','extract-folder'].includes(id))return startJob('extract',{destination:id==='extract-here'?p.path:p.path+'归档名称/'});
    if(id.startsWith('compress-'))return showDialog('add',{formatId:id.includes('zip')?'zip':'7z',share:id.includes('email')});
    if(['crc32','crc64','xxh64','md5','sha1','sha256','sha384','sha512','sha3-256','blake2sp','hash-all'].includes(id))return showDialog('hash',{hashAlgorithm:id});
    if(id==='exit'){closeDialog();say('交互原型保留在当前页面，可继续浏览');return;}
    if(['rename','delete','create-folder','create-file','edit-file','comment','move'].includes(id)&&!writeCap())return customDialog('当前归档为只读','<p>'+esc(fmt(p.formatId)?.name||p.formatId)+' 支持读取与提取。修改后的文件可另存到本地目录。</p>'+footer);
    return showDialog(id);
  }
  root.addEventListener('click',e=>{
    const area=e.target.closest('.p-panel[data-panel]');if(area){const idx=Number(area.dataset.panel);if(state.active!==idx){state.active=idx;root.querySelectorAll('.p-panel[data-panel]').forEach(a=>a.classList.toggle('p-active',Number(a.dataset.panel)===idx));}}
    const entry=e.target.closest('[data-entry],[data-row]');if(entry){select(Number(entry.dataset.entry||entry.dataset.row),e);return;}
    const button=e.target.closest('button');if(!button)return;
    if(button.dataset.menu!==undefined){const n=Number(button.dataset.menu);state.menu=state.menu===n?null:n;menus();return;}
    if(button.hasAttribute('data-submenu')){const expanded=button.getAttribute('aria-expanded')==='true';[...button.parentElement.parentElement.children].forEach(sibling=>{const other=sibling.querySelector(':scope > button[data-submenu]');if(other&&other!==button){other.setAttribute('aria-expanded','false');other.nextElementSibling.hidden=true;}});button.setAttribute('aria-expanded',String(!expanded));button.nextElementSibling.hidden=expanded;fitMenus();return;}
    if(button.dataset.command)return command(button.dataset.command);
    if(button.hasAttribute('data-dialog-close'))return closeDialog();
    if(button.dataset.sort)return command(button.dataset.sort==='columns'?'columns':'sort-'+button.dataset.sort);
  });
  root.addEventListener('dblclick',e=>{const row=e.target.closest('[data-entry],[data-row]');if(row){openEntry(current().entries.find(f=>f.id===Number(row.dataset.entry||row.dataset.row)));}});
  root.addEventListener('submit',e=>{if(e.target.hasAttribute('data-address-form')){e.preventDefault();navigate(e.target.querySelector('input').value);}});
  root.addEventListener('contextmenu',e=>{const row=e.target.closest('[data-row]');if(!row)return;e.preventDefault();const area=row.closest('[data-panel]');state.active=Number(area.dataset.panel);current().selected=new Set([Number(row.dataset.row)]);state.menu=0;menus();panels();});
  root.addEventListener('keydown',e=>{
    if(e.key==='Escape'){closeMenus();if(state.dialog)closeDialog();return;}
    const typing=['INPUT','SELECT','TEXTAREA'].includes(e.target.tagName);if(typing)return;
    const key=e.key.toLowerCase();const cmd=e.metaKey||e.ctrlKey;
    if(cmd&&key==='l'){e.preventDefault();get('p-panels').querySelector('[data-panel="'+state.active+'"] input')?.focus();return;}
    if(cmd&&key==='a'){e.preventDefault();command('select-all');return;}
    if(cmd&&key===','){e.preventDefault();command('options');return;}
    if(cmd&&key==='r'){e.preventDefault();command('refresh');return;}
    if(cmd&&['1','2','3','4'].includes(key)){e.preventDefault();command('view-'+{1:'large',2:'small',3:'list',4:'details'}[key]);return;}
    if(cmd&&['c','x'].includes(key)){state.clipboard={entries:chosen().map(f=>({...f})),move:key==='x',source:current()};say('已'+(key==='x'?'剪切':'复制')+'所选示例文件');e.preventDefault();return;}
    if(cmd&&key==='v'&&state.clipboard){current().entries.push(...state.clipboard.entries.map(f=>({...f,id:nextId++})));if(state.clipboard.move){const ids=new Set(state.clipboard.entries.map(f=>f.id));state.clipboard.source.entries=state.clipboard.source.entries.filter(f=>!ids.has(f.id));state.clipboard=null;}panels();e.preventDefault();return;}
    const keys={F1:'help',F2:'rename',F3:'view-file',F4:'edit-file',F5:'copy',F6:'move',F7:'create-folder',F9:'two-panels',Enter:'open',Backspace:'up',Delete:'delete',' ':'view-file'};
    if(keys[e.key]){e.preventDefault();command(keys[e.key]);}
  });
  get('p-dialog-close').onclick=closeDialog;
  redraw();
  if(globalThis.Tweak){const tweak=new Tweak({container:root,onChange:()=>{if(!state.dual)state.active=0;redraw();}});tweak.addToggle(state,'dual',{label:'双面板'});tweak.addSelect(state,'density',{label:'列表密度',options:[{label:'标准',value:'standard'},{label:'紧凑',value:'compact'}]});tweak.addToggle(state,'large',{label:'大工具栏图标'});}
}());
