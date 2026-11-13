
var CFG=null, LOGS=[], TAB='overview', MODAL=null, BUSY=false, VER='', RANGE='day';
var TREND_DAYS=14, TREND_OFF=0, TREND_BUSY=false;   // 趋势窗口：7/14/30 天，可向前翻页
var THEME=(function(){ try{ return localStorage.getItem('relay.theme')==='dark'?'dark':'light'; }catch(e){ return 'light'; } })();

function applyTheme(){
  var el=document.documentElement;
  if(!el) return;
  if(THEME==='dark') el.classList.add('theme-dark'); else el.classList.remove('theme-dark');
}
function setTheme(t){
  THEME=(t==='dark')?'dark':'light';
  try{ localStorage.setItem('relay.theme',THEME); }catch(e){}
  applyTheme();
  if(CFG) render();
}
function themeLabel(){ return THEME==='dark'?'暗色':'亮色'; }
function accentHex(){ return THEME==='dark'?'#58a6ff':'#0969da'; }
function chartInk(){
  return THEME==='dark'
    ? {grid:'#30363d',text:'#8b949e',dot:'#161b22'}
    : {grid:'#eaeef2',text:'#656d76',dot:'#fff'};
}
applyTheme();
var RANGE_LABEL={day:'今日',week:'本周',all:'总计'};
var HIDDEN={};
var CHART_COLORS=['#0969da','#1a7f37','#cf222e','#8250df','#bf3989','#9a6700','#1b7c83','#6e7781',
                  '#d4a72c','#0550ae','#116329','#a40e26'];

function esc(s){return String(s==null?'':s).replace(/[&<>"']/g,function(c){
  return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];});}
function fmt(n){n=Number(n||0);return n.toLocaleString('en-US');}
function trimZero(s){s=String(s);return s.indexOf('.')>=0?s.replace(/0+$/,'').replace(/\.$/,''):s;}
function fmtBig(n){
  n=Number(n||0);
  var neg=n<0; n=Math.abs(n); var out;
  if(n<1000) out=String(Math.round(n));
  else if(n<1e6) out=trimZero((n/1e3).toFixed(1))+'K';
  else if(n<1e9) out=trimZero((n/1e6).toFixed(2))+'M';
  else if(n<1e12) out=trimZero((n/1e9).toFixed(2))+'B';
  else out=trimZero((n/1e12).toFixed(2))+'T';
  return (neg?'-':'')+out;
}
function numCell(n){
  n=Number(n||0);
  return '<span title="'+fmt(n)+'">'+fmtBig(n)+'</span>';
}
function pct(x){return Number(x||0).toFixed(1)+'%';}
function clone(o){return JSON.parse(JSON.stringify(o));}
// tokens <-> M（面板按 M 输入，配置仍按 tokens 保存）
function mValue(tokens){
  var m=Number(tokens||0)/1e6;
  return trimZero(m.toFixed(6));
}
function mToTokens(text){
  var v=parseFloat(text);
  if(!isFinite(v)||v<0) v=0;
  return Math.round(v*1e6);
}

function rangeData(r){
  var br=(CFG&&CFG.usage&&CFG.usage.byRange)||{};
  return br[r]||{tokens:0,requests:0,users:[],models:[]};
}
function rangeTabs(){
  var items=[['day','今日'],['week','本周'],['all','总计']];
  return '<div class="seg">'+items.map(function(x){
    return '<button class="segbtn'+(RANGE===x[0]?' on':'')+'" data-range="'+x[0]+'">'+x[1]+'</button>';
  }).join('')+'</div>';
}

function niceMax(v){
  if(v<=0) return 1;
  var pow=Math.pow(10,Math.floor(Math.log(v)/Math.LN10));
  var steps=[1,2,2.5,5,10];
  for(var i=0;i<steps.length;i++){
    if(v<=steps[i]*pow) return steps[i]*pow;
  }
  return 10*pow;
}

function svgLineChart(labels,series,opts){
  opts=opts||{};
  var W=760,H=252,PL=58,PR=18,PT=26,PB=30;
  var iw=W-PL-PR, ih=H-PT-PB;
  var n=labels.length||1;
  var max=0;
  series.forEach(function(s){
    (s.values||[]).forEach(function(v){ if(v>max) max=v; });
  });
  var top=niceMax(max);
  function X(i){ return PL+(n<=1?iw/2:iw*i/(n-1)); }
  function Y(v){ return PT+ih-ih*(v/top); }
  var ink=chartInk();
  var svg='<svg viewBox="0 0 '+W+' '+H+'" class="chart">';
  for(var g=0;g<=4;g++){
    var gy=PT+ih*g/4;
    svg+='<line x1="'+PL+'" y1="'+gy.toFixed(1)+'" x2="'+(W-PR)+'" y2="'+gy.toFixed(1)+
      '" stroke="#eaeef2" stroke-width="1"/>';
    svg+='<text x="'+(PL-8)+'" y="'+(gy+4).toFixed(1)+'" text-anchor="end" font-size="11" fill="'+ink.text+'">'+
      fmtBig(top*(4-g)/4)+'</text>';
  }
  for(var i=0;i<n;i++){
    if(n>8&&i%2!==0&&i!==n-1) continue;
    svg+='<text x="'+X(i).toFixed(1)+'" y="'+(H-8)+'" text-anchor="middle" font-size="11" fill="'+ink.text+'">'+
      esc(String(labels[i]||'').substring(5))+'</text>';
  }
  series.forEach(function(s,si){
    var d='';
    var dots='';
    var texts='';
    var multi=series.length>1;
    var off=(si%3)*10;
    for(var k=0;k<n;k++){
      var v=(s.values&&s.values[k])||0;
      var x=X(k).toFixed(1), y=Y(v).toFixed(1);
      d+=(k?'L':'M')+x+' '+y;
      dots+='<circle cx="'+x+'" cy="'+y+'" r="3.2" fill="'+ink.dot+'" stroke="'+s.color+
        '" stroke-width="2"><title>'+esc(s.name+' · '+(labels[k]||'')+' · '+fmt(v)+' tokens')+
        '</title></circle>';
      if(v>0){
        var ly=Y(v)-7-off;
        if(ly<12) ly=Y(v)+14+off;
        texts+='<text x="'+x+'" y="'+ly.toFixed(1)+'" text-anchor="middle" font-size="'+
          (multi?9:10)+'" fill="'+s.color+'">'+fmtBig(v)+'</text>';
      }
    }
    if(opts.area&&series.length===1){
      svg+='<path d="'+d+'L'+X(n-1).toFixed(1)+' '+(PT+ih)+'L'+X(0).toFixed(1)+' '+(PT+ih)+
        'Z" fill="'+s.color+'" opacity="0.08"/>';
    }
    svg+='<path d="'+d+'" fill="none" stroke="'+s.color+'" stroke-width="2" '+
      'stroke-linejoin="round" stroke-linecap="round"/>';
    svg+=dots+texts;
  });
  svg+='</svg>';
  return svg;
}

function chartLegend(series){
  return '<div class="legend">'+series.map(function(s){
    var off=HIDDEN[s.name]?' off':'';
    return '<span class="lg'+off+'" data-series="'+esc(s.name)+'"><i style="background:'+s.color+
      '"></i>'+esc(s.name)+'</span>';
  }).join('')+'</div>';
}

function toast(msg,bad){
  var t=document.getElementById('toast');
  t.textContent=msg; t.className='toast on'+(bad?' bad':'');
  clearTimeout(t._h); t._h=setTimeout(function(){t.className='toast'+(bad?' bad':'');},2600);
}

function api(path,method,body){
  var opt={method:method||'GET',headers:{},credentials:'same-origin'};
  if(body!==undefined){opt.headers['Content-Type']='application/json';opt.body=JSON.stringify(body);}
  return fetch(path,opt).then(function(r){
    return r.text().then(function(txt){
      var j=null; try{j=JSON.parse(txt);}catch(e){}
      return {status:r.status,data:j};
    });
  });
}


/* ---------------- data ---------------- */

function loadAll(){
  return api('/api/config?days='+TREND_DAYS+'&offset='+TREND_OFF).then(function(r){
    if(r.status===404||r.status===401||!r.data||!r.data.settings){ location.replace('/'); return; }
    CFG=r.data;
    return api('/api/logs').then(function(l){ LOGS=(l.data&&l.data.logs)||[]; });
  });
}

/* ---------------- shell ---------------- */

var TABS=[['overview','概览'],['groups','API 分组'],['users','用户'],['stats','统计'],['security','安全'],['settings','设置']];
var SEC=null, SEC_LOADING=false;

function ensureSecurity(){
  if(SEC||SEC_LOADING) return;
  SEC_LOADING=true;
  api('/api/security').then(function(r){
    SEC_LOADING=false;
    if(r.status===200&&r.data){ SEC=r.data; if(TAB==='security') render(); }
  });
}

function loadUsage(){
  return api('/api/config?days='+TREND_DAYS+'&offset='+TREND_OFF).then(function(r){
    if(r.status===200&&r.data&&r.data.usage) CFG.usage=r.data.usage;
  });
}

function setTrend(days,off){
  if(TREND_BUSY) return;
  TREND_BUSY=true;
  TREND_DAYS=days;
  TREND_OFF=Math.max(0,off||0);
  render();
  loadUsage().then(function(){ TREND_BUSY=false; render(); },
                   function(){ TREND_BUSY=false; render(); });
}

function trendCtl(){
  var seg=[7,14,30].map(function(d){
    return '<button class="segbtn'+(TREND_DAYS===d?' on':'')+'" data-trend="'+d+'">'+d+' 天</button>';
  }).join('');
  return '<div class="seg">'+seg+'</div>'+
    '<button class="btn sm" data-trend-shift="1"'+(TREND_BUSY?' disabled':'')+'>‹ 更早</button>'+
    '<button class="btn sm" data-trend-shift="-1"'+(TREND_OFF===0||TREND_BUSY?' disabled':'')+
    '>更新 ›</button>';
}

function trendLabel(daily){
  var d=(daily&&daily.days)||[];
  if(!d.length) return '近 '+TREND_DAYS+' 天';
  if(TREND_OFF===0) return '近 '+d.length+' 天';
  return d[0]+' ~ '+d[d.length-1];
}

function secReload(btn){
  if(btn){ btn.disabled=true; btn.innerHTML='刷新中…'; }
  SEC=null;
  SEC_LOADING=true;
  api('/api/security').then(function(r){
    SEC_LOADING=false;
    if(r.status===200&&r.data) SEC=r.data;
    render();
  });
}

function markSvg(){
  return "<svg viewBox='0 0 32 32' width='13' height='13' fill='none' stroke='currentColor' stroke-width='3.2' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'><path d='M7 12h15m0 0-4-4m4 4-4 4'/><path d='M25 20H10m0 0 4 4m-4-4 4-4'/></svg>";
}

function render(){
  if(!CFG) return;
  var tabs=TABS.map(function(t){
    return '<div class="tab'+(TAB===t[0]?' on':'')+'" data-tab="'+t[0]+'">'+t[1]+'</div>';
  }).join('');
  var body='';
  if(TAB==='overview') body=viewOverview();
  else if(TAB==='groups') body=viewGroups();
  else if(TAB==='users') body=viewUsers();
  else if(TAB==='stats') body=viewStats();
  else if(TAB==='security'){ ensureSecurity(); body=SEC?viewSecurity():'<div class="card"><div class="empty">载入中…</div></div>'; }
  else body=viewSettings();

  document.getElementById('app').innerHTML=
   '<div class="topbar">'+
     '<div class="brand"><span class="mark">'+markSvg()+'</span><span>'+esc(CFG.settings.siteName||'AI Relay')+'</span>'+
     '<span class="ver">v'+esc(VER)+'</span></div>'+
     '<div class="sp"></div>'+
     '<div class="stat"><span class="dot"></span>运行中 · 端口 '+CFG.settings.port+'</div>'+
     '<button class="themebtn" id="themeBtn" data-theme-toggle="1" '+
       'title="切换明暗主题">'+(THEME==='dark'?'亮色':'暗色')+'</button>'+
     '<button id="logout">退出</button>'+
   '</div>'+
   '<div class="tabs">'+tabs+'</div>'+
   '<div class="wrap">'+body+'</div>';

  document.getElementById('logout').onclick=function(){
    api('/api/logout','POST',{}).then(function(){ location.replace('/'); });
  };
  var ts=document.querySelectorAll('.tab');
  for(var i=0;i<ts.length;i++){
    ts[i].onclick=function(){ TAB=this.getAttribute('data-tab'); render(); };
  }
  bindView();
  if(MODAL) renderModal();
}

/* ---------------- overview ---------------- */

function viewOverview(){
  var u=CFG.usage||{byRange:{},allTotal:0};
  var day=rangeData('day');
  var all=rangeData('all');
  var groups=(CFG.groups||[]).length;
  var users=(CFG.users||[]).length;

  var rd=rangeData(RANGE);
  var top=rd.users.filter(function(x){return x.tokens>0;})
                  .sort(function(a,b){return b.tokens-a.tokens;});
  var rows='';
  if(!top.length) rows='<tr><td colspan="3"><div class="empty">'+RANGE_LABEL[RANGE]+'暂无用量</div></td></tr>';
  for(var k=0;k<top.length;k++){
    var x=top[k];
    rows+='<tr><td>'+esc(x.name)+'</td>'+
      '<td class="num">'+numCell(x.tokens)+'</td>'+
      '<td><div style="display:flex;align-items:center;gap:8px"><div class="bar" style="flex:1"><i style="width:'+
        Math.min(100,x.share)+'%"></i></div><span class="mono" style="color:var(--muted);width:48px;text-align:right">'+
        pct(x.share)+'</span></div></td></tr>';
  }

  var logHtml='';
  if(!LOGS.length) logHtml='<div class="empty">暂无请求记录</div>';
  else{
    logHtml='<div class="logbox">';
    var start=Math.max(0,LOGS.length-60);
    for(var q=start;q<LOGS.length;q++){
      var line=LOGS[q];
      var idx=line.indexOf('] ');
      if(idx>0) logHtml+='<span class="t">'+esc(line.substring(0,idx+1))+'</span>'+esc(line.substring(idx+1))+'\n';
      else logHtml+=esc(line)+'\n';
    }
    logHtml+='</div>';
  }

  var port=CFG.settings.port;
  return '<div class="pagehead"><div><h2>概览</h2>'+
    '<div class="sub">'+esc(u.time||'')+' · OpenAI 兼容中转服务</div></div><div class="sp"></div>'+
    '<button class="btn" onclick="refreshAll(this)">刷新</button></div>'+
   '<div class="grid">'+
     metric('用户数',fmt(users),(CFG.users||[]).filter(function(x){return x.enabled;}).length+' 个启用')+
     metric('AI 分组',fmt(groups),countApis()+' 个上游接口')+
     metric('今日用量',fmtBig(day.tokens),'tokens',day.tokens)+
     metric('累计用量',fmtBig(all.tokens),fmt(all.requests)+' 次请求',all.tokens)+
   '</div>'+
   '<div class="card"><div class="card-h">接入信息</div><div class="card-b">'+
     '<div class="row2">'+
       '<div><div class="hint" style="margin:0 0 4px">API 基础地址</div><code>http://127.0.0.1:'+port+'/v1</code></div>'+
       '<div><div class="hint" style="margin:0 0 4px">模型列表</div><code>GET http://127.0.0.1:'+port+'/v1/models</code></div>'+
     '</div>'+
     '<div class="hint" style="margin-top:12px">请求头需携带 <code>Authorization: Bearer &lt;用户 APIKey&gt;</code>。'+
     '无效 Key 访问 /v1 时按不存在路径处理（404）。</div>'+
   '</div></div>'+
   '<div class="card"><div class="card-h">用户用量'+
     '<span class="label gray">'+RANGE_LABEL[RANGE]+'</span><div class="sp"></div>'+rangeTabs()+
     '</div><div class="card-b tight">'+
     '<table><thead><tr><th>用户</th><th class="num">用量（tokens）</th>'+
     '<th style="width:210px">占全体比</th></tr></thead><tbody>'+rows+'</tbody></table>'+
   '</div></div>'+
   '<div class="card"><div class="card-h">最近请求</div><div class="card-b">'+logHtml+'</div></div>';
}

function metric(k,v,d,raw){
  var t=(raw!==undefined&&raw!==null)?' title="'+esc(fmt(raw))+' tokens"':'';
  return '<div class="metric"><div class="k">'+esc(k)+'</div><div class="v"'+t+'>'+esc(v)+'</div>'+
    '<div class="d">'+esc(d)+'</div></div>';
}
function countApis(){
  var n=0; (CFG.groups||[]).forEach(function(g){n+=(g.apis||[]).length;}); return n;
}

/* ---------------- groups ---------------- */

function viewGroups(){
  var gs=CFG.groups||[];
  var lim=CFG.failLimit||3;
  var html='<div class="pagehead"><div><h2>API 分组</h2>'+
    '<div class="sub">每组可配置多个上游 API，模型名对外加分组前缀；上游连续异常 '+lim+
    ' 次后自动切到组内下一个提供同名（设置名）模型的上游，次日 0 点（北京时间）重置</div></div>'+
    '<div class="sp"></div>'+
    '<button class="btn" onclick="resetApiFails(this)">重置异常计数</button>'+
    '<button class="btn primary" onclick="editGroup(null)">新建分组</button></div>';
  if(!gs.length){
    html+='<div class="card"><div class="empty">还没有分组。新建一个分组，填入上游 OpenAI 兼容地址与密钥。</div></div>';
    return html;
  }
  for(var i=0;i<gs.length;i++){
    var g=gs[i];
    var active=null, allTripped=(g.apis||[]).length>0;
    for(var q=0;q<(g.apis||[]).length;q++){
      if(g.apis[q].id===g.activeApi){ active=g.apis[q]; }
      if(!g.apis[q].tripped) allTripped=false;
    }
    var activeLabel = active
      ? '<span class="label '+(allTripped?'red':'green')+'" title="组内第一个未熔断的上游">当前上游 '+
        esc(active.name||active.baseUrl)+'</span>'
      : '<span class="label gray">未配置上游</span>';
    var apis='';
    if(!g.apis||!g.apis.length){
      apis='<div class="empty" style="padding:16px">该分组暂无上游 API</div>';
    }else{
      apis+='<table><thead><tr><th>名称</th><th>地址</th><th>密钥</th><th class="num">模型</th>'+
            '<th>状态</th><th style="width:160px"></th></tr></thead><tbody>';
      for(var k=0;k<g.apis.length;k++){
        var a=g.apis[k];
        var st;
        if(a.tripped) st='<span class="label red">已熔断 '+(a.fails||0)+'/'+lim+'</span>';
        else if(a.fails>0) st='<span class="label gray">异常 '+(a.fails||0)+'/'+lim+'</span>';
        else st='<span class="label green">正常</span>';
        if(a.id===g.activeApi) st+=' <span class="label blue">在用</span>';
        var resetBtn = a.fails>0
          ? '<button class="btn sm" onclick="resetApiFails(this,\''+esc(a.id)+'\')">重置</button>'
          : '';
        apis+='<tr><td>'+esc(a.name||'-')+'</td>'+
          '<td><code>'+esc(a.baseUrl)+'</code></td>'+
          '<td><code>'+esc(mask(a.apiKey))+'</code></td>'+
          '<td class="num" title="'+esc((a.models||[]).join(', '))+'">'+(a.models||[]).length+'</td>'+
          '<td>'+st+'</td>'+
          '<td class="num"><button class="btn sm" onclick="testApi('+i+','+k+',this)">测试</button> '+
          resetBtn+'</td></tr>';
      }
      apis+='</tbody></table>';
    }
    html+='<div class="card"><div class="card-h">'+
      '<span>'+esc(g.name)+'</span>'+
      '<span class="label blue">前缀 '+esc(g.prefix)+'</span>'+
      '<span class="label gray">'+g.apis.length+' 个 API</span>'+
      activeLabel+
      '<div class="sp"></div>'+
      '<button class="btn sm" onclick="editGroup('+i+')">编辑</button>'+
      '<button class="btn sm danger" onclick="delGroup('+i+')">删除</button>'+
      '</div><div class="card-b tight">'+apis+'</div></div>';
  }
  return html;
}

function resetApiFails(btn,apiId){
  var msg=apiId?'确认重置该上游的异常计数？':'确认重置全部上游的异常计数？';
  if(!window.confirm(msg)) return;
  btn.disabled=true;
  api('/api/apifail/reset','POST',apiId?{apiId:apiId}:{}).then(function(r){
    btn.disabled=false;
    if(r.status===200){ toast('异常计数已重置'); refreshAll(); }
    else { toast('重置失败',true); }
  });
}

function mask(k){
  if(!k) return '未设置';
  if(k.length<=10) return k;
  return k.substring(0,6)+'...'+k.substring(k.length-4);
}

function testApi(gi,ai,btn){
  var g=CFG.groups[gi], a=g.apis[ai];
  btn.disabled=true; btn.textContent='测试中';
  api('/api/test','POST',{baseUrl:a.baseUrl,apiKey:a.apiKey}).then(function(r){
    btn.disabled=false; btn.textContent='测试';
    var d=r.data||{};
    toast((d.ok?'连接成功：':'连接失败：')+(d.info||''),!d.ok);
  });
}

/* ---------------- users ---------------- */

function viewUsers(){
  var us=CFG.users||[];
  var html='<div class="pagehead"><div><h2>用户</h2>'+
    '<div class="sub">为平台用户分配 APIKey、额度与可调用的 AI 分组</div></div>'+
    '<div class="sp"></div><button class="btn primary" onclick="editUser(null)">新建用户</button></div>';
  if(!us.length){
    html+='<div class="card"><div class="empty">还没有用户。新建用户后即可用其 APIKey 调用 /v1。</div></div>';
    return html;
  }
  html+='<div class="card"><div class="card-b tight"><table><thead><tr>'+
    '<th>用户</th><th>APIKey</th><th>可用分组</th><th class="num">总额度</th><th class="num">每日</th>'+
    '<th class="num">今日已用</th><th class="num">累计已用</th><th>状态</th>'+
    '<th style="width:130px"></th></tr></thead><tbody>';
  for(var i=0;i<us.length;i++){
    var u=us[i];
    var gnames=[];
    for(var k=0;k<(u.groupIds||[]).length;k++){
      for(var m=0;m<(CFG.groups||[]).length;m++){
        if(CFG.groups[m].id===u.groupIds[k]){gnames.push(CFG.groups[m].name);break;}
      }
    }
    var usage=findUsage(u.id);
    var qs=(usage&&usage.quotaState)||(u.enabled?'ok':'disabled');
    var stBadge = qs==='disabled' ? '<span class="label gray">停用</span>'
      : qs==='total' ? '<span class="label red">总额度用尽</span>'
      : qs==='daily' ? '<span class="label red">当日额度用尽</span>'
      : '<span class="label green">正常</span>';
    var dailyCell=(u.dailyQuota>0)?numCell(u.dailyQuota):'<span class="hint">不限</span>';
    html+='<tr><td><div style="font-weight:500">'+esc(u.name)+'</div>'+
        (u.note?'<div class="hint" style="margin:0">'+esc(u.note)+'</div>':'')+'</td>'+
      '<td><div class="keyline">'+
        '<code class="keymask" data-full="'+esc(u.apiKey)+'" onclick="toggleKey(this)" '+
          'title="点击展开 / 收起">'+esc(mask(u.apiKey))+'</code>'+
        '<button class="btn sm" data-copy="'+esc(u.apiKey)+'">复制</button></div></td>'+
      '<td>'+(gnames.length?gnames.map(function(n){return '<span class="label">'+esc(n)+'</span>';}).join(' '):
        '<span class="label red">无</span>')+'</td>'+
      '<td class="num">'+(u.unlimited?'<span class="label green">无限</span>':numCell(u.quota))+'</td>'+
      '<td class="num">'+dailyCell+'</td>'+
      '<td class="num">'+numCell(usage.today)+'</td>'+
      '<td class="num">'+numCell(usage.total)+'</td>'+
      '<td>'+stBadge+'</td>'+
      '<td class="num"><button class="btn sm" onclick="editUser('+i+')">编辑</button> '+
        '<button class="btn sm danger" onclick="delUser('+i+')">删除</button></td></tr>';
  }
  html+='</tbody></table></div></div>';
  return html;
}

// Key 折叠显示：默认只露前 6 位与后 4 位，点击可展开/收起；复制始终送出完整 Key
function toggleKey(el){
  var full=el.getAttribute('data-full')||'';
  var ex=(' '+el.className+' ').indexOf(' expanded ')>-1;
  if(ex){
    el.className=(' '+el.className+' ').replace(' expanded ',' ').replace(/^\s+|\s+$/g,'');
    el.textContent=mask(full);
  }else{
    el.className=(el.className?el.className+' ':'')+'expanded';
    el.textContent=full;
  }
}

function findUsage(id){
  var us=(CFG.usage&&CFG.usage.users)||[];
  for(var i=0;i<us.length;i++) if(us[i].id===id) return us[i];
  return {today:0,week:0,total:0,requests:0,share:0,models:[]};
}

// 直接写入剪贴板：https/localhost 用 Clipboard API，HTTP 面板用隐藏 textarea 兼容方案，
// 两条路都失败才提示手动选中，绝不弹窗打断操作
function copyText(t){
  var done=function(){ toast('已复制'); };
  var legacy=function(){
    var ta=document.createElement('textarea');
    ta.value=t;
    ta.setAttribute('readonly','');
    ta.style.position='fixed';
    ta.style.top='-100px';
    ta.style.left='-100px';
    ta.style.opacity='0';
    var ok=false;
    document.body.appendChild(ta);
    try{
      ta.focus();
      ta.select();
      if(ta.setSelectionRange) ta.setSelectionRange(0,t.length);
      ok=document.execCommand('copy');
    }catch(e){ ok=false; }
    if(ta.parentNode) ta.parentNode.removeChild(ta);
    return !!ok;
  };
  if(navigator.clipboard&&navigator.clipboard.writeText){
    navigator.clipboard.writeText(t).then(done,function(){
      if(legacy()) done(); else toast('复制失败，请手动选中 Key 复制',true);
    });
  }else if(legacy()){
    done();
  }else{
    toast('复制失败，请手动选中 Key 复制',true);
  }
}

document.addEventListener('click',function(e){
  var el=e.target;
  while(el&&el!==document){
    if(el.getAttribute&&el.getAttribute('data-copy')!==null){
      copyText(el.getAttribute('data-copy'));return;
    }
    if(el.getAttribute&&el.getAttribute('data-theme-toggle')!==null){
      setTheme(THEME==='dark'?'light':'dark');return;
    }
    el=el.parentNode;
  }
});

/* ---------------- stats ---------------- */

function viewStats(){
  var u=CFG.usage||{byRange:{},allTotal:0};
  var rd=rangeData(RANGE);
  var rows='';
  var list=rd.users.filter(function(x){return x.tokens>0;})
                   .sort(function(a,b){return b.tokens-a.tokens;});
  for(var i=0;i<list.length;i++){
    var x=list[i];
    var mt='';
    var ms=(x.models||[]).slice().sort(function(a,b){return b.tokens-a.tokens;});
    for(var k=0;k<ms.length;k++){
      mt+='<div style="display:flex;align-items:center;gap:8px;margin:2px 0">'+
        '<span class="mono" title="'+esc(ms[k].model)+'" style="font-size:12px;flex:0 0 150px;'+
        'width:150px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:var(--muted)">'+
        esc(ms[k].model)+'</span>'+
        '<div class="bar g" style="flex:1"><i style="width:'+Math.min(100,ms[k].share)+'%"></i></div>'+
        '<span class="mono" style="width:44px;text-align:right;color:var(--muted)">'+pct(ms[k].share)+'</span></div>';
    }
    if(!mt) mt='<span class="hint">-</span>';
    rows+='<tr><td>'+esc(x.name)+'</td>'+
      '<td class="num">'+numCell(x.tokens)+'</td>'+
      '<td class="num">'+pct(x.share)+'</td>'+
      '<td style="min-width:220px">'+mt+'</td></tr>';
  }
  if(!rows) rows='<tr><td colspan="4"><div class="empty">暂无统计数据</div></td></tr>';

  var mrows='';
  var models=(rd.models||[]).slice().sort(function(a,b){return b.tokens-a.tokens;});
  for(var q=0;q<models.length;q++){
    mrows+='<tr><td class="mono">'+esc(models[q].model)+'</td>'+
      '<td class="num">'+numCell(models[q].tokens)+'</td>'+
      '<td><div style="display:flex;align-items:center;gap:8px"><div class="bar" style="flex:1"><i style="width:'+
      Math.min(100,models[q].share)+'%"></i></div><span class="mono" style="color:var(--muted);width:48px;text-align:right">'+
      pct(models[q].share)+'</span></div></td></tr>';
  }
  if(!mrows) mrows='<tr><td colspan="3"><div class="empty">暂无统计数据</div></td></tr>';

  var daily=(CFG.usage&&CFG.usage.daily)||{days:[],total:[],users:[]};
  var labels=daily.days||[];
  var totalSum=0;
  (daily.total||[]).forEach(function(v){ totalSum+=v; });
  var totalChart='<div class="empty">暂无用量数据</div>';
  if(labels.length&&totalSum>0){
    totalChart=svgLineChart(labels,[{name:'全体',color:accentHex(),values:daily.total}],{area:true});
  }
  var usersAll=(daily.users||[]).slice(0,12);
  var series=usersAll.map(function(x,i){
    return {name:x.name,color:CHART_COLORS[i%CHART_COLORS.length],values:x.series||[]};
  });
  var shown=series.filter(function(s){return !HIDDEN[s.name];});
  var userChart='<div class="empty">暂无用量数据</div>';
  if(series.length){
    userChart=chartLegend(series)+
      (shown.length?svgLineChart(labels,shown,{}):'<div class="empty">已全部隐藏，点击图例恢复</div>');
  }

  if(TREND_BUSY){ totalChart='<div class="empty">载入中…</div>'; userChart='<div class="empty">载入中…</div>'; }

  var fail=(CFG.usage&&CFG.usage.failures)||{};
  var fr=fail[RANGE]||{total:0,apis:[],models:[]};
  var fth=(fail.threshold!==undefined)?fail.threshold:(CFG.failLimit||0);
  var fnote=(fth>0)?('阈值 '+fth+' 次'):'阈值 0（不熔断，异常立即重试）';
  // 模型请求次数 = 成功（用量记录）+ 失败（异常记录），合成一条进度条
  var mmap={};
  var mlist=(rd.models||[]);
  for(var mi=0;mi<mlist.length;mi++){
    var mo=mlist[mi];
    if(!mmap[mo.model]) mmap[mo.model]={model:mo.model,ok:0,bad:0};
    mmap[mo.model].ok+=mo.requests||0;
  }
  var fms=(fr.models||[]);
  for(var fi=0;fi<fms.length;fi++){
    var fmx=fms[fi];
    if(!mmap[fmx.model]) mmap[fmx.model]={model:fmx.model,ok:0,bad:0};
    mmap[fmx.model].bad+=fmx.count||0;
  }
  var mrows2=[];
  for(var key in mmap){
    if(!Object.prototype.hasOwnProperty.call(mmap,key)) continue;
    var it=mmap[key];
    it.total=it.ok+it.bad;
    mrows2.push(it);
  }
  mrows2.sort(function(a,b){return b.total-a.total;});
  var reqTotal=0;
  for(var ri=0;ri<mrows2.length;ri++) reqTotal+=mrows2[ri].total;
  var mut='';
  for(var rj=0;rj<mrows2.length;rj++){
    var rw=mrows2[rj];
    var okPct=rw.total>0?(rw.ok*100/rw.total):0;
    var badPct=rw.total>0?(rw.bad*100/rw.total):0;
    var sharePct=reqTotal>0?(rw.total*100/reqTotal):0;
    mut+='<tr><td class="mono">'+esc(rw.model)+'</td>'+
      '<td class="num">'+fmt(rw.total)+'</td>'+
      '<td class="num">'+pct(sharePct)+'</td>'+
      '<td style="min-width:220px"><div style="display:flex;align-items:center;gap:8px">'+
      '<div class="bar g" style="flex:1"><i style="width:'+okPct.toFixed(2)+'%"></i>'+
      (badPct>0?'<b style="width:'+badPct.toFixed(2)+'%"></b>':'')+'</div>'+
      '<span class="mono" style="flex:0 0 96px;text-align:right;color:var(--muted);'+
      'white-space:nowrap;overflow:hidden">'+
      fmt(rw.ok)+'/'+fmt(rw.total)+'</span></div></td></tr>';
  }
  if(!mut) mut='<tr><td colspan="4"><div class="empty">暂无请求记录</div></td></tr>';

  return '<div class="pagehead"><div><h2>统计</h2>'+
    '<div class="sub">'+esc(u.time||'')+' · '+RANGE_LABEL[RANGE]+'共 '+
    '<span title="'+fmt(rd.tokens)+'">'+fmtBig(rd.tokens)+'</span> tokens · '+
    fmt(rd.requests)+' 次请求 · 失败 '+fmt(fr.total||0)+' 次</div></div><div class="sp"></div>'+
    rangeTabs()+
    '<button class="btn" onclick="refreshAll(this)">刷新</button></div>'+
   '<div class="card"><div class="card-h">各用户用量'+
     '<span class="label gray">'+RANGE_LABEL[RANGE]+'</span></div><div class="card-b tight">'+
     '<table><thead><tr><th>用户</th><th class="num">用量（tokens）</th>'+
     '<th class="num">占全体</th><th>模型占比</th></tr></thead>'+
     '<tbody>'+rows+'</tbody></table></div></div>'+
   '<div class="card"><div class="card-h">模型用量分布'+
     '<span class="label gray">'+RANGE_LABEL[RANGE]+'</span></div><div class="card-b tight">'+
     '<table><thead><tr><th>模型</th><th class="num">用量（tokens）</th><th>占比</th></tr></thead>'+
     '<tbody>'+mrows+'</tbody></table></div></div>'+
   '<div class="card"><div class="card-h">模型请求次数'+
     '<span class="label gray">'+RANGE_LABEL[RANGE]+'</span>'+
     '<div class="sp"></div><span class="hint" style="margin:0">共 '+fmt(reqTotal)+
     ' 次 · 失败 '+fmt(fr.total||0)+' 次</span></div>'+
     '<div class="card-b tight"><table><thead><tr><th>模型</th>'+
     '<th class="num">请求次数</th><th class="num">占比</th>'+
     '<th style="width:260px">成功 / 总数</th></tr></thead>'+
     '<tbody>'+mut+'</tbody></table></div></div>'+
   '<div class="card"><div class="card-h">每日用量趋势（全体）'+
     '<span class="label gray">'+esc(trendLabel(daily))+'</span>'+
     '<div class="sp"></div>'+trendCtl()+'</div>'+
     '<div class="card-b">'+totalChart+'</div></div>'+
   '<div class="card"><div class="card-h">每人每日用量'+
     '<span class="label gray">'+esc(trendLabel(daily))+'</span>'+
     '<div class="sp"></div><span class="hint" style="margin:0">点击图例可隐藏/显示</span></div>'+
     '<div class="card-b">'+userChart+'</div></div>';
}

function refreshAll(btn){
  var label=btn?btn.innerHTML:'';
  if(btn){ btn.disabled=true; btn.innerHTML='刷新中…'; }
  loadAll().then(function(){ render(); }, function(){
    if(btn){ btn.disabled=false; btn.innerHTML=label; }
  });
}

/* ---------------- security ---------------- */

function ipTag(list, action){
  if(!list||!list.length) return '<span class="hint">（空）</span>';
  return list.map(function(ip){
    return '<span class="label gray" style="margin:0 6px 6px 0;display:inline-flex;align-items:center;gap:6px">'+
      esc(ip)+'<a href="#" data-sec-remove="'+action+'" data-ip="'+esc(ip)+
      '" style="color:var(--red);text-decoration:none">×</a></span>';
  }).join('');
}

function viewSecurity(){
  var s=SEC||{locked:false,penalty:0,limit:3600,entries:[],whitelist:[],blacklist:[]};
  var rows='';
  var list=(s.entries||[]).slice().sort(function(a,b){return b.fails-a.fails;});
  for(var i=0;i<list.length;i++){
    var e=list[i];
    rows+='<tr><td class="mono">'+esc(e.ip)+'</td>'+
      '<td class="num">'+e.fails+'</td>'+
      '<td class="num">'+(e.retryIn>0?e.retryIn+' 秒':'可尝试')+'</td>'+
      '<td>'+(e.whitelisted?'<span class="label green">白名单</span>':'')+'</td>'+
      '<td class="num"><button class="btn sm" data-sec-action="clear" data-ip="'+esc(e.ip)+'">清除</button> '+
      '<button class="btn sm danger" data-sec-action="blacklist" data-ip="'+esc(e.ip)+'">拉黑</button></td></tr>';
  }
  if(!rows) rows='<tr><td colspan="5"><div class="empty">暂无登录失败记录</div></td></tr>';

  return '<div class="pagehead"><div><h2>安全</h2>'+
    '<div class="sub">登录锁定、失败来源与 IP 访问控制</div></div><div class="sp"></div>'+
    '<button class="btn" onclick="secReload(this)">刷新</button></div>'+
   '<div class="card"><div class="card-h">登录锁定</div><div class="card-b">'+
     '<div class="row2">'+
       '<div><div class="hint" style="margin:0 0 4px">状态</div>'+
         (s.locked?'<span class="label red">已锁定</span>':'<span class="label green">正常</span>')+'</div>'+
       '<div><div class="hint" style="margin:0 0 4px">累计惩罚</div>'+
         '<span class="mono">'+fmt(s.penalty||0)+' / '+fmt(s.limit||3600)+' 秒</span></div>'+
     '</div>'+
     '<div class="hint" style="margin-top:10px">本机 127.0.0.1 不受延迟与锁定限制；白名单 IP 不受全局锁定，但仍按 1/2/4/8/16 秒递增延迟。</div>'+
     '<div style="display:flex;gap:8px;margin-top:12px">'+
       '<button class="btn primary" data-sec-action="unlock">解除锁定</button>'+
       '<button class="btn" data-sec-action="clear">清除全部失败记录</button>'+
     '</div>'+
   '</div></div>'+
   '<div class="card"><div class="card-h">登录失败名单</div><div class="card-b tight">'+
     '<table><thead><tr><th>IP</th><th class="num">密码错误次数</th><th class="num">下次可尝试</th>'+
     '<th>标记</th><th style="width:170px"></th></tr></thead><tbody>'+rows+'</tbody></table></div></div>'+
   '<div class="row2">'+
     '<div class="card"><div class="card-h">IP 白名单</div><div class="card-b">'+
       '<div style="margin-bottom:10px">'+ipTag(s.whitelist,'whitelist-remove')+'</div>'+
       '<div class="keyline"><input type="text" id="sec_wl" placeholder="例如 10.10.18.0/24">'+
       '<button class="btn sm" id="sec_wl_add">添加</button></div>'+
       '<div class="hint">白名单不受 1 小时全局锁定，延迟规则不变。</div>'+
     '</div></div>'+
     '<div class="card"><div class="card-h">IP 黑名单</div><div class="card-b">'+
       '<div style="margin-bottom:10px">'+ipTag(s.blacklist,'unblacklist')+'</div>'+
       '<div class="keyline"><input type="text" id="sec_bl" placeholder="例如 203.0.113.5">'+
       '<button class="btn sm danger" id="sec_bl_add">拉黑</button></div>'+
       '<div class="hint">拉黑后该 IP 访问任何路径都不返回内容（含网页），需在面板解除。</div>'+
     '</div></div>'+
   '</div>';
}

function secAction(action,ip){
  var body={action:action};
  if(ip) body.ip=ip;
  api('/api/security','POST',body).then(function(r){
    if(r.status!==200){ toast('操作失败',true); return; }
    toast('已更新');
    secReload();
  });
}

/* ---------------- settings ---------------- */

var UPD=null, UPD_BUSY=false;

function updBody(){
  var u=UPD;
  if(UPD_BUSY) return '<div class="tlsline">正在处理…</div>';
  if(!u) return '<div class="hint" style="margin:0">仓库：<code>LhyCloud/AI-API-Relay-Station</code>，'+
    '点「检查更新」查询最新 Release。</div>';
  if(u.error) return '<div class="tlsline bad">检查失败：'+esc(u.error)+'</div>';
  var h='';
  if(u.hasUpdate){
    h+='<div class="tlsline warn">发现新版本 <b>'+esc(u.latest)+'</b>（当前 '+esc(u.current)+'）</div>';
  }else{
    h+='<div class="tlsline ok">已是最新版本（'+esc(u.current)+'）</div>';
  }
  if(u.latest){
    h+='<div class="tlsrow"><span>最新版本</span><code>'+esc(u.latest)+'</code></div>';
    h+='<div class="tlsrow"><span>发布时间</span><code>'+esc(u.publishedAt||'-')+'</code></div>';
    h+='<div class="tlsrow"><span>更新包</span><code>'+esc(u.assetName||'（无 ai-relay.exe）')+'</code></div>';
  }
  if(u.hasUpdate&&!u.canApply){
    h+='<div class="hint">这个 Release 里没有 ai-relay.exe，无法自动更新，请手动下载。</div>';
  }
  if(u.notes){
    h+='<div class="updnote">'+esc(u.notes)+'</div>';
  }
  return h;
}

function checkUpdate(btn){
  if(UPD_BUSY) return;
  UPD_BUSY=true;
  if(btn){ btn.disabled=true; btn.innerHTML='检查中…'; }
  api('/api/update/check','POST',{}).then(function(r){
    UPD_BUSY=false;
    if(r.status!==200||!r.data){ UPD={error:'请求失败（HTTP '+r.status+'）'}; }
    else if(r.data.ok===false){ UPD={error:r.data.error||'检查失败'}; }
    else { UPD=r.data; }
    render();
  },function(){
    UPD_BUSY=false; UPD={error:'网络请求失败'}; render();
  });
}

function applyUpdate(btn){
  if(UPD_BUSY||!UPD||!UPD.canApply) return;
  if(!confirm('更新到 v'+UPD.latest+'？\n会下载新版本、替换 ai-relay.exe 并自动重启服务（约 1~3 秒内完成切换）。')) return;
  UPD_BUSY=true;
  if(btn){ btn.disabled=true; btn.innerHTML='正在下载…'; }
  api('/api/update/apply','POST',{}).then(function(r){
    if(r.status!==200){
      UPD_BUSY=false;
      toast((r.data&&r.data.error&&r.data.error.message)||'更新失败',true);
      render();
      return;
    }
    toast((r.data&&r.data.message)||'正在重启…');
    waitRestart();
  },function(){
    UPD_BUSY=false; toast('更新请求失败',true); render();
  });
}

function waitRestart(){
  var tries=0;
  var timer=setInterval(function(){
    tries++;
    api('/api/session').then(function(r){
      if(r.status===200){ clearInterval(timer); location.reload(); }
    },function(){});
    if(tries>60){ clearInterval(timer); toast('服务未在 2 分钟内恢复，请手动启动 ai-relay.exe',true); }
  },2000);
}

function viewSettings(){
  var s=CFG.settings;
  var off=Number(s.resetOffsetMinutes||0);
  var fl=(s.failLimit===undefined?3:s.failLimit);
  return '<div class="pagehead"><div><h2>设置</h2><div class="sub">服务端口、面板名称、访问范围、熔断与重置规则、管理密码</div></div></div>'+
   '<div class="card" style="max-width:560px"><div class="card-h">基础设置</div><div class="card-b">'+
     '<div class="form-row"><label>监听端口</label>'+
       '<input type="number" id="s_port" min="1" max="65535" value="'+s.port+'">'+
       '<div class="hint">修改后服务会立即切换到新端口重新监听。</div></div>'+
     '<div class="form-row"><label>面板名称</label>'+
       '<input type="text" id="s_name" value="'+esc(s.siteName)+'"></div>'+
     '<div class="form-row"><label>访问范围</label>'+
       '<label class="check"><input type="checkbox" id="s_public"'+(s.panelPublic?' checked':'')+'> 允许公网访问控制面板</label>'+
       '<div class="hint">关闭后仅本机可打开面板，外网访问 / 与 /api 一律返回 404；/v1 不受影响。</div></div>'+
     '<div class="form-row"><label>上游熔断阈值（连续异常次数）</label>'+
       '<input type="number" id="s_fail" min="0" max="99" value="'+fl+'">'+
       '<div class="hint">默认 3：同一上游连续 3 次异常后不再选它，改用组内下一个提供同名模型的上游；'+
       '到重置时间点自动恢复。<br>填 0 表示不熔断：异常不返回给调用方，立即顺延到组内下一个上游重试一次'+
       '（只顺延一次，第二次仍失败才返回错误）。</div></div>'+
     '<div class="form-row"><label>每日重置时间偏移（小时）</label>'+
       '<input type="number" id="s_offset" min="-12" max="12" step="1" value="'+trimZero((off/60).toFixed(2))+'">'+
       '<div class="hint">0 = 北京时间 0 点；填 4 表示凌晨 4 点重置，填 -2 表示前一天 22 点。'+
       '影响用户每日额度、上游每日限额与上游异常计数。</div></div>'+
     '<div class="form-row"><label>修改管理密码</label>'+
       '<input type="password" id="s_pw" autocomplete="new-password" placeholder="留空则不修改">'+
       '<div class="hint">密码经加盐哈希后传输与保存，明文不会出现在网络上或配置文件中。</div></div>'+
     '<div style="display:flex;gap:8px"><button class="btn primary" id="saveSettings">保存设置</button></div>'+
   '</div></div>'+
   '<div class="card" style="max-width:560px"><div class="card-h">版本更新'+
     '<span class="label gray">v'+esc(VER)+'</span></div><div class="card-b">'+
     updBody()+
     '<div style="display:flex;gap:8px;margin-top:12px">'+
       '<button class="btn" id="updCheck">检查更新</button>'+
       '<button class="btn primary" id="updApply"'+
         ((UPD&&UPD.canApply&&!UPD_BUSY)?'':' disabled')+'>下载并更新</button>'+
     '</div>'+
     '<div class="hint">更新会下载 ai-relay.exe 覆盖当前程序并自动重启（旧文件备份为 ai-relay.exe.old）；'+
     'Release 里没有 ai-relay.exe 时不会做任何改动。</div>'+
   '</div></div>'+
   '<div class="card"><div class="card-h">数据文件</div><div class="card-b">'+
     '<div class="hint">所有配置与用量保存在程序目录下的 <code>relay-data.json</code>，'+
     '日志写入 <code>relay.log</code>。备份该文件即可迁移。</div></div></div>';
}

/* ---------------- modal ---------------- */

function renderModal(){
  var m=MODAL;
  var prevScroll=0;
  var prevBody=document.querySelector('#overlay .mb');
  if(prevBody) prevScroll=prevBody.scrollTop;
  var old=document.getElementById('overlay');
  if(old) old.parentNode.removeChild(old);
  var box=document.createElement('div');
  box.className='overlay'; box.id='overlay';
  var html='';
  if(m.type==='group') html=groupForm(m.data);
  else if(m.type==='user') html=userForm(m.data);
  box.innerHTML='<div class="modal">'+html+'</div>';
  var newBody=box.querySelector('.mb');
  if(newBody) newBody.scrollTop=prevScroll;
  document.body.appendChild(box);
  bindModal();
}

function closeModal(){
  MODAL=null;
  var old=document.getElementById('overlay');
  if(old) old.parentNode.removeChild(old);
}

function groupForm(g){
  var apis='';
  for(var i=0;i<g.apis.length;i++){
    var a=g.apis[i];
    apis+='<div class="apibox">'+
      '<div class="h"><span class="t">上游 API #'+(i+1)+'</span><div class="sp"></div>'+
      '<button class="btn sm" title="上移（优先级更高）" onclick="moveApi('+i+',-1)"'+
        (i===0?' disabled':'')+'>↑ 上移</button>'+
      '<button class="btn sm" title="下移" onclick="moveApi('+i+',1)"'+
        (i===g.apis.length-1?' disabled':'')+'>↓ 下移</button>'+
      '<button class="btn sm danger" onclick="removeApi('+i+')">移除</button></div>'+
      '<div class="row2">'+
        '<div class="form-row"><label>名称</label><input type="text" data-f="name" data-i="'+i+
          '" value="'+esc(a.name)+'" placeholder="例如 官方 / 备用"></div>'+
        '<div class="form-row"><label>APIKey</label><input type="text" data-f="apiKey" data-i="'+i+
          '" value="'+esc(a.apiKey)+'" placeholder="sk-..."></div>'+
      '</div>'+
      '<div class="row2">'+
        '<div class="form-row"><label>基础地址</label><input type="text" data-f="baseUrl" data-i="'+i+
          '" value="'+esc(a.baseUrl)+'" placeholder="https://api.openai.com/v1"></div>'+
        '<div class="form-row"><label>每日限额（M tokens，0=不限）</label>'+
          '<input type="number" min="0" step="0.001" data-f="dailyLimitM" data-i="'+i+
          '" value="'+(a.dailyLimitM||0)+'"><div class="hint">北京时间 0 点重置，超限自动切换组内下一个 API</div></div>'+
      '</div>'+
      modelBoxHtml(i,a)+
    '</div>';
  }
  if(!g.apis.length) apis='<div class="empty" style="padding:8px 0 16px">暂无上游 API，点击下方按钮添加。</div>';
  return '<div class="mh"><span>'+(g.id?'编辑分组':'新建分组')+'</span><div class="sp"></div>'+
    '<button class="x" onclick="closeModal()">×</button></div>'+
    '<div class="mb">'+
      '<div class="row2">'+
        '<div class="form-row"><label>分组名称</label><input type="text" id="g_name" value="'+esc(g.name)+
          '" placeholder="例如 DeepSeek"></div>'+
        '<div class="form-row"><label>模型前缀</label><input type="text" id="g_prefix" value="'+esc(g.prefix)+
          '" placeholder="例如 ds/"></div>'+
      '</div>'+
      '<div class="hint" style="margin:-6px 0 14px">对外模型名 = 前缀 + 模型名，例如 <code>ds/deepseek-chat</code>。前缀留空则用分组名。<br>'+
        '上游按从上到下的顺序取用：越靠前越优先，额度用尽或连续异常时才用后面的。</div>'+
      '<div style="display:flex;align-items:center;gap:8px;margin-bottom:8px">'+
        '<div style="font-weight:600;font-size:12px">上游 API</div><div style="flex:1"></div>'+
        '<button class="btn sm" onclick="addApi()">添加 API</button></div>'+
      apis+
    '</div>'+
    '<div class="mf"><button class="btn" onclick="closeModal()">取消</button>'+
      '<button class="btn primary" onclick="saveGroup()">保存分组</button></div>';
}

function apiModelNames(a){
  var out=(a.available||[]).slice();
  (a.models||[]).forEach(function(m){ if(out.indexOf(m)<0) out.push(m); });
  return out;
}

function modelBoxHtml(i,a){
  var names=apiModelNames(a);
  var sel=a.models||[];
  var rows='';
  for(var k=0;k<names.length;k++){
    var n=names[k];
    var as=(a.alias&&a.alias[n])||'';
    rows+='<div class="mrow" data-row="'+esc(n)+'">'+
      '<label class="check"><input type="checkbox" data-m="'+i+'" data-name="'+esc(n)+'"'+
      (sel.indexOf(n)>=0?' checked':'')+'> <span class="mono" title="'+esc(n)+'">'+esc(n)+'</span></label>'+
      '<input type="text" class="alias" data-i="'+i+'" data-name="'+esc(n)+'" value="'+esc(as)+
      '" placeholder="对外名称（可选）"></div>';
  }
  if(!rows) rows='<div class="empty" style="padding:8px">尚未获取模型，点击右上角「获取模型列表」</div>';
  return '<div class="form-row">'+
    '<div style="display:flex;align-items:center;gap:8px">'+
      '<label style="margin:0">模型</label>'+
      '<span class="hint" id="mc_'+i+'" style="margin:0">已选 '+sel.length+' / 可选 '+names.length+'</span>'+
      '<div style="flex:1"></div>'+
      '<button class="btn sm" onclick="fetchApiModels('+i+')">获取模型列表</button>'+
    '</div>'+
    '<div class="modelbox" id="mb_'+i+'">'+
      (names.length?'<input type="text" class="mfilter" data-i="'+i+'" placeholder="筛选模型">':'')+
      rows+
    '</div>'+
    '<div class="keyline" style="margin-top:6px">'+
      '<input type="text" id="ma_'+i+'" placeholder="手动添加模型名，回车确认">'+
      '<button class="btn sm" onclick="addApiModel('+i+')">添加</button>'+
    '</div>'+
    '<div class="hint">只勾选的模型会出现在 /v1/models；右侧可给该模型起对外名称（同名模型可分开中转）。</div>'+
  '</div>';
}

function updateModelCount(i){
  var a=MODAL&&MODAL.data.apis[i];
  var el=document.getElementById('mc_'+i);
  if(!a||!el) return;
  el.textContent='已选 '+(a.models||[]).length+' / 可选 '+apiModelNames(a).length;
}

function fetchApiModels(i){
  var a=MODAL&&MODAL.data.apis[i];
  if(!a) return;
  var url=(a.baseUrl||'').trim();
  if(!url){ toast('请先填写基础地址',true); return; }
  toast('正在获取模型列表…');
  api('/api/models','POST',{baseUrl:url,apiKey:a.apiKey||''}).then(function(r){
    var d=r.data||{};
    if(r.status!==200||!d.ok){
      toast('获取失败：'+(d.error||('HTTP '+r.status)),true);
      return;
    }
    a.available=d.models||[];
    renderModal();
    toast('已获取 '+a.available.length+' 个模型');
  });
}

function addApiModel(i){
  var box=document.getElementById('ma_'+i);
  var a=MODAL&&MODAL.data.apis[i];
  if(!box||!a) return;
  var name=(box.value||'').trim();
  if(!name) return;
  if((a.available||[]).indexOf(name)<0) a.available=(a.available||[]).concat([name]);
  if((a.models||[]).indexOf(name)<0) a.models=(a.models||[]).concat([name]);
  renderModal();
  var again=document.getElementById('ma_'+i);
  if(again) again.focus();
}

function filterModels(i,text){
  var box=document.getElementById('mb_'+i);
  if(!box) return;
  var q=(text||'').toLowerCase();
  var rows=box.querySelectorAll('.mrow');
  for(var k=0;k<rows.length;k++){
    var n=rows[k].getAttribute('data-row')||'';
    rows[k].style.display=(!q||n.toLowerCase().indexOf(q)>=0)?'':'none';
  }
}

function userForm(u){
  var gs='';
  var groups=CFG.groups||[];
  if(!groups.length){
    gs='<div class="hint">还没有分组，请先到「API 分组」创建。</div>';
  }else{
    for(var i=0;i<groups.length;i++){
      var on=(u.groupIds||[]).indexOf(groups[i].id)>=0;
      gs+='<label class="check" style="margin:3px 0"><input type="checkbox" data-g="'+esc(groups[i].id)+'"'+
        (on?' checked':'')+'> '+esc(groups[i].name)+
        ' <span class="hint" style="margin:0">'+esc(groups[i].prefix)+'</span></label>';
    }
  }
  return '<div class="mh"><span>'+(u.id?'编辑用户':'新建用户')+'</span><div class="sp"></div>'+
    '<button class="x" onclick="closeModal()">×</button></div>'+
    '<div class="mb">'+
      '<div class="row2">'+
        '<div class="form-row"><label>用户名</label><input type="text" id="u_name" value="'+esc(u.name)+
          '" placeholder="例如 张三"></div>'+
        '<div class="form-row"><label>APIKey</label>'+
          '<div style="display:flex;gap:6px"><input type="text" id="u_key" value="'+esc(u.apiKey)+'" placeholder="留空自动生成">'+
          '<button class="btn" onclick="genKey()" style="white-space:nowrap">生成</button></div></div>'+
      '</div>'+
      '<div class="form-row"><label>可用 AI 分组</label>'+gs+'</div>'+
      '<div class="row2">'+
        '<div class="form-row"><label>总额度（M tokens）</label>'+
          '<input type="number" id="u_quota" min="0" step="0.01" value="'+mValue(u.quota)+'"'+(u.unlimited?' disabled':'')+'>'+
          '<label class="check" style="margin-top:8px"><input type="checkbox" id="u_unlimited"'+
          (u.unlimited?' checked':'')+' onchange="toggleUnlimited()"> 无限额度</label>'+
          '<div class="hint">1 M = 1,000,000 tokens。不勾无限且填 0 表示不允许调用。</div></div>'+
        '<div class="form-row"><label>每日额度（M tokens，0=不限）</label>'+
          '<input type="number" id="u_daily" min="0" step="0.01" value="'+mValue(u.dailyQuota)+'">'+
          '<div class="hint">按重置时间点每天清零，与总额度同时生效，任一用尽即返回 429。</div></div>'+
      '</div>'+
      '<div class="form-row"><label>备注</label><input type="text" id="u_note" value="'+esc(u.note)+
        '" placeholder="可选"></div>'+
      '<label class="check"><input type="checkbox" id="u_enabled"'+(u.enabled?' checked':'')+'> 启用该用户</label>'+
    '</div>'+
    '<div class="mf"><button class="btn" onclick="closeModal()">取消</button>'+
      '<button class="btn primary" onclick="saveUser()">保存用户</button></div>';
}

function toggleUnlimited(){
  document.getElementById('u_quota').disabled=document.getElementById('u_unlimited').checked;
}

function genKey(){
  var chars='0123456789abcdef';
  var s='sk-';
  if(window.crypto&&window.crypto.getRandomValues){
    var arr=new Uint8Array(24); window.crypto.getRandomValues(arr);
    for(var i=0;i<arr.length;i++) s+=chars[arr[i]&15];
  }else{
    for(var k=0;k<48;k++) s+=chars[Math.floor(Math.random()*16)];
  }
  document.getElementById('u_key').value=s;
}

function bindModal(){
  if(!MODAL) return;
  var m=MODAL;
  if(m.type==='group'){
    var inputs=document.querySelectorAll('#overlay [data-f]');
    for(var i=0;i<inputs.length;i++){
      inputs[i].oninput=function(){
        var idx=parseInt(this.getAttribute('data-i'),10);
        var f=this.getAttribute('data-f');
        var a=m.data.apis[idx];
        if(!a) return;
        if(f==='dailyLimitM'){
          a.dailyLimitM=Math.max(0,parseFloat(this.value)||0);
        }else{
          a[f]=this.value;
        }
      };
    }
    var nm=document.getElementById('g_name');
    if(nm) nm.oninput=function(){ m.data.name=this.value; };
    var pf=document.getElementById('g_prefix');
    if(pf) pf.oninput=function(){ m.data.prefix=this.value; };

    var als=document.querySelectorAll('#overlay .alias');
    for(var ai=0;ai<als.length;ai++){
      als[ai].oninput=function(){
        var idx=parseInt(this.getAttribute('data-i'),10);
        var name=this.getAttribute('data-name');
        var a=m.data.apis[idx];
        if(!a) return;
        var v=(this.value||'').trim();
        if(!a.alias) a.alias={};
        if(v&&v!==name) a.alias[name]=v;
        else delete a.alias[name];
      };
    }

    var cbs=document.querySelectorAll('#overlay [data-m]');
    for(var k=0;k<cbs.length;k++){
      cbs[k].onchange=function(){
        var idx=parseInt(this.getAttribute('data-m'),10);
        var name=this.getAttribute('data-name');
        var a=m.data.apis[idx];
        if(!a) return;
        var list=(a.models||[]).slice();
        var pos=list.indexOf(name);
        if(this.checked){ if(pos<0) list.push(name); }
        else if(pos>=0){ list.splice(pos,1); }
        a.models=list;
        updateModelCount(idx);
      };
    }
    var fs=document.querySelectorAll('#overlay .mfilter');
    for(var q=0;q<fs.length;q++){
      fs[q].oninput=function(){
        filterModels(parseInt(this.getAttribute('data-i'),10),this.value);
      };
    }
    var mas=document.querySelectorAll('#overlay [id^=ma_]');
    for(var z=0;z<mas.length;z++){
      mas[z].onkeydown=function(e){
        if(e.key==='Enter'){ e.preventDefault(); addApiModel(parseInt(this.id.substring(3),10)); }
      };
    }
  }
}

function addApi(){
  MODAL.data.apis.push({id:'',name:'',baseUrl:'',apiKey:'',models:[],available:[],alias:{},dailyLimitM:0});
  renderModal();
}
function removeApi(i){
  MODAL.data.apis.splice(i,1);
  renderModal();
}
function moveApi(i,d){
  var list=MODAL&&MODAL.data&&MODAL.data.apis;
  if(!list) return;
  var j=i+d;
  if(j<0||j>=list.length) return;
  var t=list[i]; list[i]=list[j]; list[j]=t;
  renderModal();
}

function editGroup(i){
  var g=null;
  if(i===null||i===undefined){
    g={id:'',name:'',prefix:'',apis:[]};
  }else{
    g=clone(CFG.groups[i]);
    if(!g.apis) g.apis=[];
  }
  MODAL={type:'group',index:i,data:g};
  renderModal();
}

function saveGroup(){
  var g=MODAL.data;
  g.name=(g.name||'').trim();
  if(!g.name){ toast('请填写分组名称',true); return; }
  g.prefix=(g.prefix||'').trim();
  var payload=clone(CFG);
  delete payload.usage;
  var list=payload.groups||[];
  if(MODAL.index===null||MODAL.index===undefined){
    list.push(g);
  }else{
    list[MODAL.index]=g;
  }
  payload.groups=list;
  sendConfig(payload,'分组已保存');
}

function delGroup(i){
  var g=CFG.groups[i];
  if(!confirm('删除分组「'+g.name+'」？引用它的用户将失去该分组。')) return;
  var payload=clone(CFG);
  delete payload.usage;
  payload.groups.splice(i,1);
  payload.users.forEach(function(u){
    u.groupIds=(u.groupIds||[]).filter(function(id){return id!==g.id;});
  });
  sendConfig(payload,'分组已删除');
}

function editUser(i){
  var u;
  if(i===null||i===undefined){
    u={id:'',name:'',apiKey:'',note:'',unlimited:false,enabled:true,quota:1000000,dailyQuota:0,groupIds:[],created:0};
    var gs=CFG.groups||[];
    for(var k=0;k<gs.length;k++) u.groupIds.push(gs[k].id);
  }else{
    u=clone(CFG.users[i]);
  }
  MODAL={type:'user',index:i,data:u};
  renderModal();
}

function saveUser(){
  var u=MODAL.data;
  u.name=(document.getElementById('u_name').value||'').trim();
  if(!u.name){ toast('请填写用户名',true); return; }
  u.apiKey=(document.getElementById('u_key').value||'').trim();
  u.note=document.getElementById('u_note').value||'';
  u.unlimited=document.getElementById('u_unlimited').checked;
  u.quota=mToTokens(document.getElementById('u_quota').value);
  u.dailyQuota=mToTokens(document.getElementById('u_daily').value);
  u.enabled=document.getElementById('u_enabled').checked;
  u.groupIds=[];
  var cbs=document.querySelectorAll('#overlay [data-g]');
  for(var i=0;i<cbs.length;i++){
    if(cbs[i].checked) u.groupIds.push(cbs[i].getAttribute('data-g'));
  }
  var payload=clone(CFG);
  delete payload.usage;
  var list=payload.users||[];
  if(MODAL.index===null||MODAL.index===undefined) list.push(u);
  else list[MODAL.index]=u;
  payload.users=list;
  sendConfig(payload,'用户已保存');
}

function delUser(i){
  var u=CFG.users[i];
  if(!confirm('删除用户「'+u.name+'」？其 APIKey 将立即失效。')) return;
  var payload=clone(CFG);
  delete payload.usage;
  payload.users.splice(i,1);
  sendConfig(payload,'用户已删除');
}

function sendConfig(payload,okMsg){
  if(BUSY) return;
  BUSY=true;
  var oldPort=CFG.settings.port;
  api('/api/config','POST',payload).then(function(r){
    BUSY=false;
    if(r.status!==200||!r.data||!r.data.ok){
      toast((r.data&&r.data.error&&r.data.error.message)||'保存失败',true);
      return;
    }
    closeModal();
    toast(okMsg);
    var newPort=r.data.port;
    if(r.data.rebind&&newPort!==oldPort){
      var link='http://127.0.0.1:'+newPort+'/';
      toast('端口已切换至 '+newPort+'，正在跳转...');
      setTimeout(function(){ location.href=link; },900);
      return;
    }
    loadAll().then(function(){ render(); });
  });
}

function bindView(){
  var ucb=document.getElementById('updCheck');
  if(ucb) ucb.onclick=function(){ checkUpdate(this); };
  var uab=document.getElementById('updApply');
  if(uab) uab.onclick=function(){ applyUpdate(this); };
  var tds=document.querySelectorAll('#app [data-trend]');
  for(var ti=0;ti<tds.length;ti++){
    tds[ti].onclick=function(){
      setTrend(parseInt(this.getAttribute('data-trend'),10),0);
    };
  }
  var tss=document.querySelectorAll('#app [data-trend-shift]');
  for(var ts=0;ts<tss.length;ts++){
    tss[ts].onclick=function(){
      var dir=parseInt(this.getAttribute('data-trend-shift'),10)||1;
      setTrend(TREND_DAYS,Math.max(0,TREND_OFF+dir*TREND_DAYS));
    };
  }
  var rbs=document.querySelectorAll('#app [data-range]');
  for(var i=0;i<rbs.length;i++){
    rbs[i].onclick=function(){
      RANGE=this.getAttribute('data-range');
      render();
    };
  }
  var lgs=document.querySelectorAll('#app [data-series]');
  for(var j=0;j<lgs.length;j++){
    lgs[j].onclick=function(){
      var n=this.getAttribute('data-series');
      if(HIDDEN[n]) delete HIDDEN[n]; else HIDDEN[n]=true;
      render();
    };
  }
  var acts=document.querySelectorAll('#app [data-sec-action]');
  for(var k=0;k<acts.length;k++){
    acts[k].onclick=function(){
      var act=this.getAttribute('data-sec-action');
      var ip=this.getAttribute('data-ip')||'';
      if(act==='blacklist'&&!confirm('拉黑 '+ip+' 后该 IP 访问任何路径都不会有响应，确定？')) return;
      if(act==='clear'&&!ip&&!confirm('清除全部失败记录？（不会解除全局锁定，如需解锁请点「解除锁定」）')) return;
      secAction(act,ip);
    };
  }
  var rms=document.querySelectorAll('#app [data-sec-remove]');
  for(var ri=0;ri<rms.length;ri++){
    rms[ri].onclick=function(e){
      if(e) e.preventDefault();
      secAction(this.getAttribute('data-sec-remove'),this.getAttribute('data-ip'));
    };
  }
  var wlAdd=document.getElementById('sec_wl_add');
  if(wlAdd){
    wlAdd.onclick=function(){
      var v=(document.getElementById('sec_wl').value||'').trim();
      if(!v) return;
      secAction('whitelist-add',v);
    };
  }
  var blAdd=document.getElementById('sec_bl_add');
  if(blAdd){
    blAdd.onclick=function(){
      var v=(document.getElementById('sec_bl').value||'').trim();
      if(!v) return;
      if(!confirm('拉黑 '+v+' 后该 IP 访问任何路径都不会有响应，确定？')) return;
      secAction('blacklist',v);
    };
  }
  var s=document.getElementById('saveSettings');
  if(s){
    s.onclick=function(){
      var port=parseInt(document.getElementById('s_port').value,10);
      if(!(port>0&&port<65536)){ toast('端口不合法',true); return; }
      var pub=document.getElementById('s_public').checked;
      var fl=parseInt(document.getElementById('s_fail').value,10);
      if(!(fl>=0&&fl<=99)){ toast('熔断阈值需在 0 - 99 之间',true); return; }
      var oh=parseFloat(document.getElementById('s_offset').value);
      if(!isFinite(oh)||oh<-12||oh>12){ toast('重置偏移需在 -12 到 12 小时之间',true); return; }
      var np=document.getElementById('s_pw').value||'';
      if(np&&np.length<4){ toast('密码至少 4 位',true); return; }
      var payload=clone(CFG);
      delete payload.usage;
      payload.settings.port=port;
      payload.settings.siteName=document.getElementById('s_name').value||'AI Relay';
      payload.settings.panelPublic=pub;
      payload.settings.failLimit=fl;
      payload.settings.resetOffsetMinutes=Math.round(oh*60);
      var go=function(){ sendConfig(payload,'设置已保存'); };
      if(!np){ go(); return; }
      s.disabled=true;
      api('/api/auth').then(function(a){
        s.disabled=false;
        if(!a.data||!a.data.salt){ toast('无法获取登录参数',true); return; }
        payload.settings.adminPasswordHash=kdfHash(a.data.salt,np);
        go();
      });
    };
  }
}

/* ---------------- boot ---------------- */

function boot(){
  api('/api/session').then(function(s){
    if(s.data&&s.data.version) VER=s.data.version;
    if(s.data&&s.data.ok){
      loadAll().then(function(){ render(); });
    }else{
      location.replace('/');
    }
  });
}
boot();
