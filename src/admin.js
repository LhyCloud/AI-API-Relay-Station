
var CFG=null, LOGS=[], TAB='overview', MODAL=null, BUSY=false, VER='', RANGE='day';
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
  var svg='<svg viewBox="0 0 '+W+' '+H+'" class="chart">';
  for(var g=0;g<=4;g++){
    var gy=PT+ih*g/4;
    svg+='<line x1="'+PL+'" y1="'+gy.toFixed(1)+'" x2="'+(W-PR)+'" y2="'+gy.toFixed(1)+
      '" stroke="#eaeef2" stroke-width="1"/>';
    svg+='<text x="'+(PL-8)+'" y="'+(gy+4).toFixed(1)+'" text-anchor="end" font-size="11" fill="#656d76">'+
      fmtBig(top*(4-g)/4)+'</text>';
  }
  for(var i=0;i<n;i++){
    if(n>8&&i%2!==0&&i!==n-1) continue;
    svg+='<text x="'+X(i).toFixed(1)+'" y="'+(H-8)+'" text-anchor="middle" font-size="11" fill="#656d76">'+
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
      dots+='<circle cx="'+x+'" cy="'+y+'" r="3.2" fill="#fff" stroke="'+s.color+
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
  return api('/api/config').then(function(r){
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

function secReload(){
  SEC=null;
  ensureSecurity();
  render();
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
     '<div class="brand"><span class="mark">R</span><span>'+esc(CFG.settings.siteName||'AI Relay')+'</span>'+
     '<span class="ver">v'+esc(VER)+'</span></div>'+
     '<div class="sp"></div>'+
     '<div class="stat"><span class="dot"></span>运行中 · 端口 '+CFG.settings.port+'</div>'+
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
        Math.min(100,x.share)+'%"></i></div><span class="mono" style="color:#656d76;width:48px;text-align:right">'+
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
    '<button class="btn" onclick="refreshAll()">刷新</button></div>'+
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
  var html='<div class="pagehead"><div><h2>API 分组</h2>'+
    '<div class="sub">每组可配置多个上游 API，模型名对外加分组前缀</div></div>'+
    '<div class="sp"></div><button class="btn primary" onclick="editGroup(null)">新建分组</button></div>';
  if(!gs.length){
    html+='<div class="card"><div class="empty">还没有分组。新建一个分组，填入上游 OpenAI 兼容地址与密钥。</div></div>';
    return html;
  }
  for(var i=0;i<gs.length;i++){
    var g=gs[i];
    var apis='';
    if(!g.apis||!g.apis.length){
      apis='<div class="empty" style="padding:16px">该分组暂无上游 API</div>';
    }else{
      apis+='<table><thead><tr><th>名称</th><th>地址</th><th>密钥</th><th class="num">模型</th>'+
            '<th style="width:120px"></th></tr></thead><tbody>';
      for(var k=0;k<g.apis.length;k++){
        var a=g.apis[k];
        apis+='<tr><td>'+esc(a.name||'-')+'</td>'+
          '<td><code>'+esc(a.baseUrl)+'</code></td>'+
          '<td><code>'+esc(mask(a.apiKey))+'</code></td>'+
          '<td class="num" title="'+esc((a.models||[]).join(', '))+'">'+(a.models||[]).length+'</td>'+
          '<td class="num"><button class="btn sm" onclick="testApi('+i+','+k+',this)">测试</button></td></tr>';
      }
      apis+='</tbody></table>';
    }
    html+='<div class="card"><div class="card-h">'+
      '<span>'+esc(g.name)+'</span>'+
      '<span class="label blue">前缀 '+esc(g.prefix)+'</span>'+
      '<span class="label gray">'+g.apis.length+' 个 API</span>'+
      '<div class="sp"></div>'+
      '<button class="btn sm" onclick="editGroup('+i+')">编辑</button>'+
      '<button class="btn sm danger" onclick="delGroup('+i+')">删除</button>'+
      '</div><div class="card-b tight">'+apis+'</div></div>';
  }
  return html;
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
    '<th>用户</th><th>APIKey</th><th>可用分组</th><th class="num">额度</th><th class="num">已用</th>'+
    '<th>状态</th><th style="width:130px"></th></tr></thead><tbody>';
  for(var i=0;i<us.length;i++){
    var u=us[i];
    var gnames=[];
    for(var k=0;k<(u.groupIds||[]).length;k++){
      for(var m=0;m<(CFG.groups||[]).length;m++){
        if(CFG.groups[m].id===u.groupIds[k]){gnames.push(CFG.groups[m].name);break;}
      }
    }
    var usage=findUsage(u.id);
    html+='<tr><td><div style="font-weight:500">'+esc(u.name)+'</div>'+
        (u.note?'<div class="hint" style="margin:0">'+esc(u.note)+'</div>':'')+'</td>'+
      '<td><div class="keyline"><code>'+esc(u.apiKey)+'</code>'+
        '<button class="btn sm" data-copy="'+esc(u.apiKey)+'">复制</button></div></td>'+
      '<td>'+(gnames.length?gnames.map(function(n){return '<span class="label">'+esc(n)+'</span>';}).join(' '):
        '<span class="label red">无</span>')+'</td>'+
      '<td class="num">'+(u.unlimited?'<span class="label green">无限</span>':numCell(u.quota))+'</td>'+
      '<td class="num">'+numCell(usage.total)+'</td>'+
      '<td>'+(u.enabled?'<span class="label green">启用</span>':'<span class="label gray">停用</span>')+'</td>'+
      '<td class="num"><button class="btn sm" onclick="editUser('+i+')">编辑</button> '+
        '<button class="btn sm danger" onclick="delUser('+i+')">删除</button></td></tr>';
  }
  html+='</tbody></table></div></div>';
  return html;
}

function findUsage(id){
  var us=(CFG.usage&&CFG.usage.users)||[];
  for(var i=0;i<us.length;i++) if(us[i].id===id) return us[i];
  return {today:0,week:0,total:0,requests:0,share:0,models:[]};
}

function copyText(t){
  if(navigator.clipboard&&navigator.clipboard.writeText){
    navigator.clipboard.writeText(t).then(function(){toast('已复制');},function(){window.prompt('复制以下内容',t);});
  }else{
    window.prompt('复制以下内容',t);
  }
}

document.addEventListener('click',function(e){
  var el=e.target;
  while(el&&el!==document){
    if(el.getAttribute&&el.getAttribute('data-copy')!==null){
      copyText(el.getAttribute('data-copy'));return;
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
        'width:150px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:#656d76">'+
        esc(ms[k].model)+'</span>'+
        '<div class="bar g" style="flex:1"><i style="width:'+Math.min(100,ms[k].share)+'%"></i></div>'+
        '<span class="mono" style="width:44px;text-align:right;color:#656d76">'+pct(ms[k].share)+'</span></div>';
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
      Math.min(100,models[q].share)+'%"></i></div><span class="mono" style="color:#656d76;width:48px;text-align:right">'+
      pct(models[q].share)+'</span></div></td></tr>';
  }
  if(!mrows) mrows='<tr><td colspan="3"><div class="empty">暂无统计数据</div></td></tr>';

  var daily=(CFG.usage&&CFG.usage.daily)||{days:[],total:[],users:[]};
  var labels=daily.days||[];
  var totalSum=0;
  (daily.total||[]).forEach(function(v){ totalSum+=v; });
  var totalChart='<div class="empty">暂无用量数据</div>';
  if(labels.length&&totalSum>0){
    totalChart=svgLineChart(labels,[{name:'全体',color:'#0969da',values:daily.total}],{area:true});
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

  return '<div class="pagehead"><div><h2>统计</h2>'+
    '<div class="sub">'+esc(u.time||'')+' · '+RANGE_LABEL[RANGE]+'共 '+
    '<span title="'+fmt(rd.tokens)+'">'+fmtBig(rd.tokens)+'</span> tokens · '+
    fmt(rd.requests)+' 次请求</div></div><div class="sp"></div>'+
    rangeTabs()+
    '<button class="btn" onclick="refreshAll()">刷新</button></div>'+
   '<div class="card"><div class="card-h">各用户用量'+
     '<span class="label gray">'+RANGE_LABEL[RANGE]+'</span></div><div class="card-b tight">'+
     '<table><thead><tr><th>用户</th><th class="num">用量（tokens）</th>'+
     '<th class="num">占全体</th><th>模型占比</th></tr></thead>'+
     '<tbody>'+rows+'</tbody></table></div></div>'+
   '<div class="card"><div class="card-h">模型用量分布'+
     '<span class="label gray">'+RANGE_LABEL[RANGE]+'</span></div><div class="card-b tight">'+
     '<table><thead><tr><th>模型</th><th class="num">用量（tokens）</th><th>占比</th></tr></thead>'+
     '<tbody>'+mrows+'</tbody></table></div></div>'+
   '<div class="card"><div class="card-h">每日用量趋势（全体 · 近 14 天）</div>'+
     '<div class="card-b">'+totalChart+'</div></div>'+
   '<div class="card"><div class="card-h">每人每日用量（近 14 天）'+
     '<div class="sp"></div><span class="hint" style="margin:0">点击图例可隐藏/显示</span></div>'+
     '<div class="card-b">'+userChart+'</div></div>';
}

function refreshAll(){
  loadAll().then(function(){ render(); });
}

/* ---------------- security ---------------- */

function ipTag(list, action){
  if(!list||!list.length) return '<span class="hint">（空）</span>';
  return list.map(function(ip){
    return '<span class="label gray" style="margin:0 6px 6px 0;display:inline-flex;align-items:center;gap:6px">'+
      esc(ip)+'<a href="#" data-sec-remove="'+action+'" data-ip="'+esc(ip)+
      '" style="color:#cf222e;text-decoration:none">×</a></span>';
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
    '<button class="btn" onclick="secReload()">刷新</button></div>'+
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

function viewSettings(){
  var s=CFG.settings;
  return '<div class="pagehead"><div><h2>设置</h2><div class="sub">服务端口、面板名称、访问范围与管理密码</div></div></div>'+
   '<div class="card" style="max-width:560px"><div class="card-h">基础设置</div><div class="card-b">'+
     '<div class="form-row"><label>监听端口</label>'+
       '<input type="number" id="s_port" min="1" max="65535" value="'+s.port+'">'+
       '<div class="hint">修改后服务会立即切换到新端口重新监听。</div></div>'+
     '<div class="form-row"><label>面板名称</label>'+
       '<input type="text" id="s_name" value="'+esc(s.siteName)+'"></div>'+
     '<div class="form-row"><label>访问范围</label>'+
       '<label class="check"><input type="checkbox" id="s_public"'+(s.panelPublic?' checked':'')+'> 允许公网访问控制面板</label>'+
       '<div class="hint">关闭后仅本机可打开面板，外网访问 / 与 /api 一律返回 404；/v1 不受影响。</div></div>'+
     '<div class="form-row"><label>修改管理密码</label>'+
       '<input type="password" id="s_pw" autocomplete="new-password" placeholder="留空则不修改">'+
       '<div class="hint">密码经加盐哈希后传输与保存，明文不会出现在网络上或配置文件中。</div></div>'+
     '<div style="display:flex;gap:8px"><button class="btn primary" id="saveSettings">保存设置</button></div>'+
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
      '<div class="hint" style="margin:-6px 0 14px">对外模型名 = 前缀 + 模型名，例如 <code>ds/deepseek-chat</code>。前缀留空则用分组名。</div>'+
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
        '<div class="form-row"><label>额度（tokens）</label>'+
          '<input type="number" id="u_quota" min="0" value="'+u.quota+'"'+(u.unlimited?' disabled':'')+'>'+
          '<label class="check" style="margin-top:8px"><input type="checkbox" id="u_unlimited"'+
          (u.unlimited?' checked':'')+' onchange="toggleUnlimited()"> 无限额度</label></div>'+
        '<div class="form-row"><label>备注</label><input type="text" id="u_note" value="'+esc(u.note)+
          '" placeholder="可选"></div>'+
      '</div>'+
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
    u={id:'',name:'',apiKey:'',note:'',unlimited:false,enabled:true,quota:1000000,groupIds:[],created:0};
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
  u.quota=parseInt(document.getElementById('u_quota').value,10)||0;
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
      var np=document.getElementById('s_pw').value||'';
      if(np&&np.length<4){ toast('密码至少 4 位',true); return; }
      var payload=clone(CFG);
      delete payload.usage;
      payload.settings.port=port;
      payload.settings.siteName=document.getElementById('s_name').value||'AI Relay';
      payload.settings.panelPublic=pub;
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
