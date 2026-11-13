# -*- coding: utf-8 -*-
"""0.0.1：版本号、白色顶栏、明暗主题、主题色选择。"""
import io

ROOT = r"D:\DSH\ai-relay"


def read(path):
    with io.open(path, encoding="utf-8", newline="") as f:
        return f.read().replace("\r\n", "\n")


def write(path, text):
    with io.open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


# ============================ admin.html ============================
p = ROOT + r"\src\admin.html"
s = read(p)

# 1) 变量与主题类
old = "*{box-sizing:border-box}"
new = """*{box-sizing:border-box}
:root{
 --bg:#f6f8fa; --panel:#fff; --panel2:#f6f8fa; --panel3:#f3f4f6;
 --text:#1f2328; --muted:#656d76; --border:#d0d7de; --border-soft:#eaeef2; --track:#eaeef2;
 --accent:#0969da; --accent-bg:#ddf4ff; --green:#1a7f37; --green-bg:#dafbe1;
 --red:#cf222e; --red-bg:#ffebe9;
 --topbar-bg:#fff; --topbar-fg:#1f2328; --topbar-border:#d0d7de; --topbar-hover:#f3f4f6;
}
:root.theme-dark{
 --bg:#0d1117; --panel:#161b22; --panel2:#1c2128; --panel3:#22272e;
 --text:#e6edf3; --muted:#8b949e; --border:#30363d; --border-soft:#21262d; --track:#30363d;
 --accent:#58a6ff; --accent-bg:#0d2d6b; --green:#3fb950; --green-bg:#0f2f1a;
 --red:#f85149; --red-bg:#3d1418;
 --topbar-bg:#161b22; --topbar-fg:#e6edf3; --topbar-border:#30363d; --topbar-hover:#22272e;
}
:root.accent-green{--accent:#1a7f37;--accent-bg:#dafbe1}
:root.accent-purple{--accent:#8250df;--accent-bg:#fbefff}
:root.accent-pink{--accent:#bf3989;--accent-bg:#ffeff7}
:root.accent-orange{--accent:#bc4c00;--accent-bg:#fff1e5}
:root.theme-dark.accent-green{--accent:#3fb950;--accent-bg:#0f2f1a}
:root.theme-dark.accent-purple{--accent:#a371f7;--accent-bg:#271052}
:root.theme-dark.accent-pink{--accent:#db61a2;--accent-bg:#3f1330}
:root.theme-dark.accent-orange{--accent:#f0883e;--accent-bg:#3d1f08}"""
assert old in s
s = s.replace(old, new, 1)

# 2) 顶栏：白色 + 亮色文字 + 主题控件
old = """.topbar{background:#24292f;color:#f6f8fa;display:flex;align-items:center;height:48px;padding:0 16px;gap:10px}
.topbar .brand{display:flex;align-items:center;gap:8px;font-weight:600;font-size:15px;color:#fff}
.topbar .mark{width:20px;height:20px;border-radius:4px;background:#f6f8fa;color:#24292f;display:flex;
 align-items:center;justify-content:center;font-size:12px;font-weight:700}
.topbar .ver{font-size:11px;line-height:16px;color:#8b949e;border:1px solid #444c56;border-radius:2em;padding:0 7px}
.topbar .sp{flex:1}
.topbar .stat{font-size:12px;color:#8b949e;display:flex;align-items:center;gap:6px}
.dot{width:8px;height:8px;border-radius:50%;background:#3fb950;display:inline-block}
.topbar button{background:transparent;border:1px solid #444c56;color:#f6f8fa;border-radius:6px;padding:3px 10px;
 font-size:12px;cursor:pointer}
.topbar button:hover{background:#30363d;border-color:#8b949e}"""
new = """.topbar{background:var(--topbar-bg);color:var(--topbar-fg);display:flex;align-items:center;height:48px;
 padding:0 16px;gap:10px;border-bottom:1px solid var(--topbar-border);
 box-shadow:0 1px 2px rgba(27,31,36,.04)}
.topbar .brand{display:flex;align-items:center;gap:8px;font-weight:600;font-size:15px;color:var(--topbar-fg)}
.topbar .mark{width:20px;height:20px;border-radius:4px;background:var(--accent);color:#fff;display:flex;
 align-items:center;justify-content:center;font-size:12px;font-weight:700}
.topbar .ver{font-size:11px;line-height:16px;color:var(--muted);border:1px solid var(--topbar-border);
 border-radius:2em;padding:0 7px}
.topbar .sp{flex:1}
.topbar .stat{font-size:12px;color:var(--muted);display:flex;align-items:center;gap:6px}
.dot{width:8px;height:8px;border-radius:50%;background:var(--green);display:inline-block}
.topbar button{background:var(--panel);border:1px solid var(--topbar-border);color:var(--topbar-fg);
 border-radius:6px;padding:3px 10px;font-size:12px;cursor:pointer;font-family:inherit}
.topbar button:hover{background:var(--topbar-hover);border-color:var(--accent)}
.swatches{display:flex;align-items:center;gap:6px;padding:0 2px}
.sw{width:16px;height:16px;border-radius:50%;border:1px solid rgba(27,31,36,.15);cursor:pointer;padding:0}
.sw.blue{background:#0969da}.sw.green{background:#1a7f37}.sw.purple{background:#8250df}
.sw.pink{background:#bf3989}.sw.orange{background:#bc4c00}
.sw.on{box-shadow:0 0 0 2px var(--panel),0 0 0 4px var(--accent)}
.topbar .themebtn{display:inline-flex;align-items:center;gap:5px;min-width:62px;justify-content:center}"""
assert old in s
s = s.replace(old, new, 1)

# 3) 其余规则改用变量
s = s.replace(".btn.primary{background:#1f883d;border-color:rgba(31,35,40,.15);color:#fff}",
              ".btn.primary{background:var(--accent);border-color:rgba(31,35,40,.15);color:#fff}", 1)
s = s.replace(".btn.primary:hover{background:#1a7f37}", ".btn.primary:hover{filter:brightness(.92)}", 1)
s = s.replace(".bar{height:6px;border-radius:3px;background:#eaeef2;",
              ".bar{height:6px;border-radius:3px;background:var(--track);", 1)
s = s.replace(".tab.on{font-weight:600;border-bottom-color:#fd8c73}",
              ".tab.on{font-weight:600;border-bottom-color:var(--accent)}", 1)

pairs = [
    ("#f6f8fa", "var(--panel2)"),
    ("#fff", "var(--panel)"),
    ("#1f2328", "var(--text)"),
    ("#656d76", "var(--muted)"),
    ("#d0d7de", "var(--border)"),
    ("#eaeef2", "var(--border-soft)"),
    ("#0969da", "var(--accent)"),
    ("#1a7f37", "var(--green)"),
    ("#cf222e", "var(--red)"),
    ("#dafbe1", "var(--green-bg)"),
    ("#ffebe9", "var(--red-bg)"),
    ("#ddf4ff", "var(--accent-bg)"),
    ("#f3f4f6", "var(--panel3)"),
    ("#24292f", "var(--text)"),
]
for a, b in pairs:
    s = s.replace(a, b)
write(p, s)

# ============================ admin.js ============================
p = ROOT + r"\src\admin.js"
s = read(p)

# 1) 主题状态 + 工具函数
old = "var TREND_DAYS=14, TREND_OFF=0, TREND_BUSY=false;   // 趋势窗口：7/14/30 天，可向前翻页"
new = old + """
var THEME=(function(){ try{ return localStorage.getItem('relay.theme')==='dark'?'dark':'light'; }catch(e){ return 'light'; } })();
var ACCENT=(function(){ try{ var a=localStorage.getItem('relay.accent'); return a||'blue'; }catch(e){ return 'blue'; } })();
var ACCENT_LIST=[['blue','蓝'],['green','绿'],['purple','紫'],['pink','粉'],['orange','橙']];
var ACCENT_HEX={blue:{l:'#0969da',d:'#58a6ff'},green:{l:'#1a7f37',d:'#3fb950'},
  purple:{l:'#8250df',d:'#a371f7'},pink:{l:'#bf3989',d:'#db61a2'},orange:{l:'#bc4c00',d:'#f0883e'}};

function applyTheme(){
  var el=document.documentElement;
  if(el){
    if(THEME==='dark') el.classList.add('theme-dark'); else el.classList.remove('theme-dark');
    for(var i=0;i<ACCENT_LIST.length;i++) el.classList.remove('accent-'+ACCENT_LIST[i][0]);
    el.classList.add('accent-'+ACCENT);
  }
}
function setTheme(t){
  THEME=(t==='dark')?'dark':'light';
  try{ localStorage.setItem('relay.theme',THEME); }catch(e){}
  applyTheme();
  if(CFG) render();
}
function setAccent(a){
  for(var i=0;i<ACCENT_LIST.length;i++){ if(ACCENT_LIST[i][0]===a) ACCENT=a; }
  try{ localStorage.setItem('relay.accent',ACCENT); }catch(e){}
  applyTheme();
  if(CFG) render();
}
function accentHex(){
  var x=ACCENT_HEX[ACCENT]||ACCENT_HEX.blue;
  return THEME==='dark'?x.d:x.l;
}
function chartInk(){
  return THEME==='dark'
    ? {grid:'#30363d',text:'#8b949e',dot:'#161b22'}
    : {grid:'#eaeef2',text:'#656d76',dot:'#fff'};
}
applyTheme();"""
assert old in s
s = s.replace(old, new, 1)

# 2) 图表颜色跟随主题
s = s.replace("""  var svg='<svg viewBox="0 0 '+W+' '+H+'" class="chart">';""",
              """  var ink=chartInk();
  var svg='<svg viewBox="0 0 '+W+' '+H+'" class="chart">';""", 1)
s = s.replace("""       '" stroke="#eaeef2" stroke-width="1"/>';""",
              """       '" stroke="'+ink.grid+'" stroke-width="1"/>';""", 1)
s = s.replace("""'" text-anchor="end" font-size="11" fill="#656d76">'+""",
              """'" text-anchor="end" font-size="11" fill="'+ink.text+'">'+""", 1)
s = s.replace("""'" text-anchor="middle" font-size="11" fill="#656d76">'+""",
              """'" text-anchor="middle" font-size="11" fill="'+ink.text+'">'+""", 1)
s = s.replace("""'" r="3.2" fill="#fff" stroke="'+s.color+""",
              """'" r="3.2" fill="'+ink.dot+'" stroke="'+s.color+""", 1)
s = s.replace("""totalChart=svgLineChart(labels,[{name:'全体',color:'#0969da',values:daily.total}],{area:true});""",
              """totalChart=svgLineChart(labels,[{name:'全体',color:accentHex(),values:daily.total}],{area:true});""", 1)

# 3) 行内颜色改变量
s = s.replace("color:#656d76", "color:var(--muted)")
s = s.replace("""'" style="color:#cf222e;text-decoration:none">×</a></span>';""",
              """'" style="color:var(--red);text-decoration:none">×</a></span>';""", 1)

# 4) 顶栏加入主题控件
old = """     '<div class="stat"><span class="dot"></span>运行中 · 端口 '+CFG.settings.port+'</div>'+
     '<button id="logout">退出</button>'+"""
new = """     '<div class="stat"><span class="dot"></span>运行中 · 端口 '+CFG.settings.port+'</div>'+
     '<div class="swatches" title="主题色">'+
       ACCENT_LIST.map(function(a){
         return '<button class="sw '+a[0]+(ACCENT===a[0]?' on':'')+'" data-accent="'+a[0]+
           '" title="'+a[1]+'色" aria-label="主题色 '+a[1]+'"></button>';
       }).join('')+
     '</div>'+
     '<button class="themebtn" id="themeBtn" data-theme-toggle="1" title="切换明暗主题">'+
       (THEME==='dark'?'☀ 亮色':'🌙 暗色')+'</button>'+
     '<button id="logout">退出</button>'+"""
assert old in s
s = s.replace(old, new, 1)

# 5) 点击处理
old = """document.addEventListener('click',function(e){
  var el=e.target;
  while(el&&el!==document){
    if(el.getAttribute&&el.getAttribute('data-copy')!==null){
      copyText(el.getAttribute('data-copy'));return;
    }
    el=el.parentNode;
  }
});"""
new = """document.addEventListener('click',function(e){
  var el=e.target;
  while(el&&el!==document){
    if(el.getAttribute&&el.getAttribute('data-copy')!==null){
      copyText(el.getAttribute('data-copy'));return;
    }
    if(el.getAttribute&&el.getAttribute('data-accent')){
      setAccent(el.getAttribute('data-accent'));return;
    }
    if(el.getAttribute&&el.getAttribute('data-theme-toggle')!==null){
      setTheme(THEME==='dark'?'light':'dark');return;
    }
    el=el.parentNode;
  }
});"""
assert old in s
s = s.replace(old, new, 1)
write(p, s)

# ============================ 版本号 ============================
p = ROOT + r"\src\relay.h"
s = read(p)
s = s.replace('#define RELAY_VERSION "1.0.1"', '#define RELAY_VERSION "0.0.1"', 1)
assert '"0.0.1"' in s
write(p, s)

p = ROOT + r"\test\panel_render_test.cjs"
s = read(p)
s = s.replace("version: '1.0.1'", "version: '0.0.1'", 1)
write(p, s)

p = ROOT + r"\README.md"
s = read(p)
s = s.replace("AI Relay 1.0.1", "AI Relay 0.0.1", 1)
s = s.replace("### 1.0.1", "### 0.0.1", 1)
s = s.replace("### 0.0.1\n\n- 版本号升到 1.0.1", "### 0.0.1\n\n- 版本号 0.0.1", 1)
write(p, s)
print("patched")
