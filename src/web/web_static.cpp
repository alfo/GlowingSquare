#include "web/web_static.h"

#include <Arduino.h>
#include <pgmspace.h>

#include "app_config.h"

// One literal in flash. Adjacent string literals are concatenated by the
// compiler, so the comments between them cost nothing and the rules stay
// grouped the way they were written.
static const char APP_CSS[] PROGMEM =
      // The dark palette is the default. --accent comes from the project
      // (APP_ACCENT_DARK / APP_ACCENT_LIGHT, written into the <html> style
      // by web_shell.cpp); every fill that carries it is mixed from it here,
      // so a project sets one colour and the page follows.
      ":root{"
      "--ground:#06080f;--surface:#0c1828;--surface-2:#0a1220;"
      "--line:#1a3060;--line-soft:#14243c;"
      "--ink:#e8f0ff;--ink-2:#c8d8f0;--ink-3:#4a6fa0;--ink-4:#2a4060;"
      "--ink-soft:#6d8cb8;"
      "--accent:var(--accent-d,#28d49a);"
      "--warn:#f0b838;--bad:#ff6b6b;--w:860px;"
      "--hover:#0f1e33;--good:#28d49a;"
      "--quiet:#0f1b2c;--quiet-hover:#16273e;"
      "--warn-line:#55420f;--warn-bg:#221a06;"
      "--bad-line:#6b2626;--bad-bg:#2a0f0f;--bad-hover:#3a1616;"
      "--sans:ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
      "--mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}"
      ":root,:root[data-theme]{"
      "--accent-dim:color-mix(in srgb,var(--accent) 22%,var(--ground));"
      "--accent-line:color-mix(in srgb,var(--accent) 50%,var(--ground));"
      "--btn:color-mix(in srgb,var(--accent) 14%,var(--ground));"
      "--btn-hover:color-mix(in srgb,var(--accent) 24%,var(--ground));"
      "--ok-bg:color-mix(in srgb,var(--good) 16%,var(--ground))}"
      // Only the colours change; every rule below reads them through the
      // variables. data-theme is set by app.js before the body is drawn:
      // the stored choice, or the browser's when the choice is auto.
      ":root[data-theme=light]{"
      "--ground:#eef3f9;--surface:#ffffff;--surface-2:#f4f7fb;"
      "--line:#b9c8dd;--line-soft:#dbe3ee;"
      "--ink:#0f1a2a;--ink-2:#243449;--ink-3:#4a6488;--ink-4:#a6b6cc;--ink-soft:#50627c;"
      "--accent:var(--accent-l,#0a8a60);"
      "--warn:#9a6a00;--bad:#c62f2f;"
      "--hover:#e3eaf4;--good:#0a8a60;"
      "--quiet:#eef2f8;--quiet-hover:#e1e8f2;"
      "--warn-line:#e2c77a;--warn-bg:#fdf4dc;"
      "--bad-line:#eaa4a4;--bad-bg:#fdeaea;--bad-hover:#f9d9d9}"
      "*{box-sizing:border-box;margin:0;padding:0}"
      "body{background:var(--ground);color:var(--ink);font-family:var(--sans);"
      "font-size:15px;line-height:1.5;-webkit-font-smoothing:antialiased;"
      "padding:28px 20px 40px;display:flex;flex-direction:column;align-items:center;"
      "gap:20px;min-height:100vh}"
      ".wrap{width:100%;max-width:var(--w);display:flex;flex-direction:column;gap:18px}"
      // header
      ".head{display:grid;grid-template-columns:52px 1fr auto;align-items:center;gap:16px}"
      ".mark{width:52px;height:52px;border-radius:13px;overflow:hidden;"
      "border:1px solid var(--line)}"
      ".mark img{width:100%;height:100%;display:block;object-fit:cover}"
      ".wordmark{font-size:21px;font-weight:650;letter-spacing:-.02em;color:var(--accent)}"
      ".wordmark span{color:var(--ink)}"
      ".subline{font-size:12px;color:var(--ink-3);margin-top:2px;font-variant-numeric:tabular-nums}"
      ".addr{display:flex;flex-direction:column;line-height:1.35;background:var(--surface-2);"
      "border:1px solid var(--line-soft);border-radius:10px;padding:8px 12px}"
      ".addr b{font-family:var(--mono);font-size:13px;font-weight:500;color:var(--accent)}"
      ".addr i{font-family:var(--mono);font-size:11px;font-style:normal;color:var(--ink-3)}"
      // nav
      ".nav{display:flex;flex-wrap:wrap;gap:4px;background:var(--surface-2);"
      "border:1px solid var(--line-soft);border-radius:12px;padding:5px}"
      ".nav a{color:var(--ink-3);text-decoration:none;font-size:13.5px;font-weight:500;"
      "padding:8px 14px;border-radius:8px;transition:background .14s,color .14s}"
      ".nav a:hover{color:var(--ink-2);background:var(--hover)}"
      ".nav a.on{background:var(--accent-dim);color:var(--ink);"
      "box-shadow:inset 0 0 0 1px var(--accent-line)}"
      // cards
      ".grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}"
      ".card{background:var(--surface);border:1px solid var(--line-soft);"
      "border-radius:14px;padding:18px 20px 20px}"
      ".card.wide{grid-column:1/-1}"
      // Spans both columns like .wide, but takes a column of its own once the
      // grid has three - the status page's inventory card.
      ".card.w2{grid-column:1/-1}"
      ".card h2{font-size:11px;font-weight:650;letter-spacing:.1em;text-transform:uppercase;"
      "color:var(--ink-3);margin-bottom:14px}"
      ".note{font-size:12.5px;color:var(--ink-soft);line-height:1.55;margin-top:12px}"
      // A card that folds away: the whole heading is the handle, a chevron
      // on the right says which way it goes.
      "details.card>summary{list-style:none;cursor:pointer;display:flex;align-items:center;justify-content:space-between}"
      "details.card>summary::-webkit-details-marker{display:none}"
      "details.card>summary h2{margin-bottom:0}"
      "details.card[open]>summary{margin-bottom:14px}"
      "details.card>summary::after{content:'';width:7px;height:7px;margin:-4px 4px 0 0;"
      "border-right:2px solid var(--ink-3);border-bottom:2px solid var(--ink-3);transform:rotate(45deg)}"
      "details.card[open]>summary::after{transform:rotate(-135deg);margin-top:4px}"
      // A list's tail that folds away under one quiet line.
      "details.more>summary{cursor:pointer;color:var(--ink-soft);padding:8px 0}"
      // Numbered steps, the number in a round badge instead of a list marker.
      ".steps{list-style:none;counter-reset:s;display:flex;flex-direction:column;gap:12px;margin-top:14px}"
      ".steps>li{counter-increment:s;display:grid;grid-template-columns:26px 1fr;gap:10px;align-items:start}"
      ".steps>li::before{content:counter(s);width:26px;height:26px;border-radius:50%;display:grid;place-items:center;"
      "background:var(--accent-dim);color:var(--accent);font-size:13px;font-weight:600}"
      ".steps>li>div{padding-top:3px}.steps a{color:var(--accent)}.steps p.hint{margin-top:6px}"
      ".addrline{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:6px}"
      ".addrline .mono{background:var(--quiet);border:1px solid var(--line);border-radius:8px;padding:5px 10px;color:var(--ink)}"
      ".note b{color:var(--ink-2);font-weight:500}"
      // data rows: mono only for machine strings
      ".rows{display:flex;flex-direction:column}"
      ".row{display:flex;align-items:baseline;justify-content:space-between;gap:16px;"
      "padding:9px 0;border-bottom:1px solid var(--line-soft);font-size:14px}"
      ".row:last-child{border-bottom:0;padding-bottom:0}.row:first-child{padding-top:0}"
      ".k{color:var(--ink-3)}"
      ".v{color:var(--ink-2);text-align:right;font-variant-numeric:tabular-nums}"
      ".v.mono{font-family:var(--mono);font-size:13px}"
      ".v em{font-style:normal;color:var(--ink-soft);font-size:12.5px;margin-left:6px}"
      // state as a shape, not only a word
      ".pill{display:inline-flex;align-items:center;gap:6px;font-size:11.5px;font-weight:600;"
      "letter-spacing:.04em;padding:3px 9px;border-radius:999px;border:1px solid}"
      ".pill:before{content:'';width:6px;height:6px;border-radius:50%;background:currentColor}"
      ".ok{color:var(--good);border-color:var(--accent-line);background:var(--ok-bg)}"
      ".wr{color:var(--warn);border-color:var(--warn-line);background:var(--warn-bg)}"
      ".bd{color:var(--bad);border-color:var(--bad-line);background:var(--bad-bg)}"
      ".sig{display:inline-flex;gap:2px;align-items:flex-end;height:12px;margin-left:8px}"
      ".sig i{width:3px;border-radius:1px;background:var(--ink-4)}"
      ".sig i:nth-child(1){height:4px}.sig i:nth-child(2){height:7px}"
      ".sig i:nth-child(3){height:10px}.sig i:nth-child(4){height:13px}"
      // forms
      ".field{display:flex;flex-direction:column;gap:7px}.field+.field{margin-top:16px}"
      // The same step around a field or a list of values whatever stands next
      // to it, so a save button or a hint is never glued to the control. Pairs
      // used to be spaced one by one, and every new pair started at 0 px.
      // :where() keeps it at zero specificity: any margin a class or a style
      // attribute sets still wins.
      ":where(:is(.card,details)>:is(.field,.rows)+*,:is(.card,.rows)>.row+:not(.row),"
      ":is(.card,details)>:not(h2,summary)+.field,:is(.card,details)>.inrow+.inrow){margin-top:16px}"
      ".field label{font-size:13px;color:var(--ink-2)}"
      ".hint{font-size:12px;color:var(--ink-soft);line-height:1.55}"
      ".inrow{display:flex;gap:10px;align-items:center;flex-wrap:wrap}"
      "input[type=text],input[type=password],input[type=number],input[type=file],select{"
      "flex:1;min-width:0;background:var(--ground);border:1px solid var(--line);"
      "border-radius:9px;color:var(--ink);font:inherit;font-size:14px;padding:9px 12px}"
      "input[type=number]{flex:0 0 86px;text-align:center}"
      "input:focus,select:focus,button:focus-visible,a:focus-visible{"
      "outline:2px solid var(--accent);outline-offset:1px}"
      ".suffix{font-family:var(--mono);font-size:13px;color:var(--ink-3);white-space:nowrap}"
      ".msg{font-size:12.5px;color:var(--accent);min-height:1.2em}"
      ".msg.bad{color:var(--bad)}"
      "button{appearance:none;cursor:pointer;font:inherit;font-size:13.5px;font-weight:550;"
      "padding:9px 16px;border-radius:9px;background:var(--btn);color:var(--accent);"
      "border:1px solid var(--accent-line);transition:background .14s;white-space:nowrap}"
      "button:hover{background:var(--btn-hover)}"
      "button:disabled{opacity:.5;cursor:default}"
      "button.quiet{background:var(--quiet);color:var(--ink-2);border-color:var(--line)}"
      "button.quiet:hover{background:var(--quiet-hover)}"
      "button.danger{background:var(--bad-bg);color:var(--bad);border-color:var(--bad-line)}"
      "button.danger:hover{background:var(--bad-hover)}"
      "button.block{width:100%;margin-top:16px}"
      // A file input in the page's own button vocabulary. The native control
      // stays in the label, hidden, so the label click opens the picker and
      // the form still posts the file; the chosen name shows beside it.
      ".filebtn{display:inline-flex;align-items:center;cursor:pointer;font:inherit;"
      "font-size:13.5px;font-weight:550;padding:9px 16px;border-radius:9px;"
      "background:var(--quiet);color:var(--ink-2);border:1px solid var(--line);white-space:nowrap}"
      ".filebtn:hover{background:var(--quiet-hover)}"
      ".filebtn input{display:none}"
      ".fname{flex:1;min-width:0;font-family:var(--mono);font-size:12.5px;color:var(--ink-soft);"
      "overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
      ".range{display:flex;align-items:center;gap:12px}"
      "input[type=range]{flex:1;accent-color:var(--accent)}"
      ".range output{font-family:var(--mono);font-size:13px;color:var(--ink);width:34px;text-align:right}"
      // tables and lists
      "table{width:100%;border-collapse:collapse;font-size:13.5px}"
      "th{font-size:10.5px;font-weight:650;letter-spacing:.08em;text-transform:uppercase;"
      "color:var(--ink-soft);text-align:left;padding:0 8px 8px 0}"
      "td{padding:5px 8px 5px 0;border-top:1px solid var(--line-soft);color:var(--ink-2)}"
      ".listrow{display:flex;align-items:center;justify-content:space-between;gap:12px;"
      "padding:10px 12px;border-radius:9px;background:var(--surface-2);"
      "border:1px solid var(--line-soft)}"
      ".listrow+.listrow{margin-top:7px}"
      ".listrow .nm{font-family:var(--mono);font-size:13px;color:var(--ink-2)}"
      ".listrow .sz{font-family:var(--mono);font-size:11.5px;color:var(--ink-soft)}"
      // Links, one row at the bottom. They take the page's own button
      // vocabulary rather than a look of their own: the quiet button for three
      // of them, the primary one for Ko-fi. They used to sit at 3.64:1 text on
      // a fill darker than any real button, with a 1.20:1 border - text on a
      // hint of surface, not something that reads as pressable.
      ".links{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}"
      ".links a{display:flex;align-items:center;justify-content:center;gap:8px;"
      "padding:11px 8px;border-radius:9px;text-decoration:none;"
      "font-size:13.5px;font-weight:550;"
      "background:var(--quiet);color:var(--ink-2);border:1px solid var(--line);"
      "transition:background .14s}"
      ".links a:hover{background:var(--quiet-hover)}"
      ".links a.support{background:var(--btn);color:var(--accent);"
      "border-color:var(--accent-line)}"
      ".links a.support:hover{background:var(--btn-hover)}"
      // currentColor, so each mark takes the colour of the button it sits in:
      // green inside Ko-fi, grey in the other three.
      ".links svg{width:15px;height:15px;flex:none;fill:currentColor}"
      // Ko-fi carries the long label and falls back to the bare name where it
      // will not fit. display:none takes the hidden one out of the flex row
      // and out of the accessibility tree, so there is neither a phantom gap
      // nor a link that reads itself twice.
      ".links .sm{display:none}"
      // Was --ink-4, which is 1.91:1 against the page - the last line still
      // carrying the fault the hints and notes were moved off in beta.38.
      ".foot{font-size:11.5px;color:var(--ink-soft);text-align:center;line-height:1.6}"
      // Three columns for the status page on a wide screen: two cards in a
      // 860 px column left two thirds of a 1440 px window empty.
      "@media(min-width:1100px){.g3{grid-template-columns:repeat(3,1fr)}"
      ".g3 .card.w2{grid-column:auto}}"
      "@media(max-width:700px){.grid{grid-template-columns:1fr}"
      ".links{grid-template-columns:1fr 1fr}"
      // The mark follows its column: a 52 px image in a 44 px cell pushed the
      // wordmark over by 8 px.
      ".head{grid-template-columns:44px 1fr}.mark{width:44px;height:44px}"
      ".addr{grid-column:1/-1}}"
      // A log row carries three buttons. Below 500 px they take a line of their
      // own under the name rather than squeezing it out.
      "@media(max-width:500px){.listrow{flex-wrap:wrap}"
      ".listrow>span:last-child{width:100%;justify-content:flex-end}}"
      // Derived, not guessed: at two columns the cell is (V-50)/2 wide, and
      // the German label needs 150px, which solves to V >= 350. German is the
      // longer language here and sets the edge.
      "@media(max-width:360px){.links .lg{display:none}"
      ".links .sm{display:inline}}"
      "@media(prefers-reduced-motion:reduce){*{transition:none!important}}"
      // Switches. There was no style for a checkbox at all: the two the
      // interface already had were browser default, forced to 16x16 by an
      // inline style, and wrapped in a label whose styles were copied from
      // one page to the other by hand. A generated settings row needs one
      // shape it can rely on.
      //
      // Built from the tokens the rest of the sheet uses, so it reads as the
      // same control family as the buttons: the accent green for on, the
      // quiet line colour for off. The real input stays on top of the track
      // at full size rather than being hidden, which keeps the hit area, the
      // keyboard focus and the label association that a div cannot fake.
      ".switch{position:relative;display:inline-block;flex:0 0 40px;width:40px;height:22px}"
      ".switch input{position:absolute;inset:0;width:100%;height:100%;margin:0;"
      "opacity:0;cursor:pointer;z-index:1}"
      ".switch i{position:absolute;inset:0;border-radius:999px;background:var(--surface-2);"
      "border:1px solid var(--line);transition:background .14s,border-color .14s}"
      ".switch i:after{content:'';position:absolute;left:3px;top:3px;width:14px;height:14px;"
      "border-radius:50%;background:var(--ink-3);"
      "transition:transform .14s,background .14s}"
      ".switch input:checked+i{background:var(--btn);border-color:var(--accent-line)}"
      ".switch input:checked+i:after{transform:translateX(18px);background:var(--accent)}"
      ".switch input:focus-visible+i{outline:2px solid var(--accent);outline-offset:1px}"
      ".switch input:disabled{cursor:default}"
      ".switch input:disabled+i{opacity:.5}"
      // The label a switch sits in. Both existing checkboxes carried this as
      // an inline style copied from one page to the other; the second copy
      // even says so in its comment.
      ".check{display:flex;align-items:center;gap:10px;font-size:13px;"
      "color:var(--ink-2);cursor:pointer}"
      ".check+.check{margin-top:10px}"

      // A settings row: name and its one line of explanation on the left, the
      // control on the right. Close to .row/.k/.v, which carries a label and a
      // value - but a setting needs a second line under the name, and .row has
      // nowhere to put one.
      ".orow{display:flex;justify-content:space-between;align-items:flex-start;"
      "gap:16px;padding:12px 0;border-bottom:1px solid var(--line-soft)}"
      ".orow:last-child{border-bottom:0;padding-bottom:0}"
      // A row with a field rather than a switch: the field gets the room.
      ".orow.ow{flex-wrap:wrap}.orow.ow .ov{flex:1 1 320px;justify-content:flex-end}"
      ".orow.ow .ov .inrow{width:100%;justify-content:flex-end}"
      ".orow.ow .ov input[type=text],.orow.ow .ov input[type=password],.orow.ow .ov textarea{flex:1 1 200px}"
      ".srows{display:flex;flex-direction:column}"
      ".orow:first-child{padding-top:0}"
      ".ol{display:flex;flex-direction:column;gap:3px;min-width:0}"
      ".on{font-size:14px;color:var(--ink-2)}"
      // Quiet text goes on --ink-soft. --ink-4 is a shape colour for rules and
      // inactive bars; as text on a card it is 1.7:1 and invisible.
      ".os{font-size:12px;color:var(--ink-soft);line-height:1.45}"
      ".ov{flex:0 0 auto;display:flex;align-items:center;gap:10px;padding-top:2px}"
      // The help circle the device shows as a "?" next to the row. Same idea
      // here, and it opens the same text - as a line under the row rather than
      // a modal, because a page has room and a 480px panel does not.
      ".oq{flex:0 0 22px;width:22px;height:22px;padding:0;border-radius:50%;"
      "font-size:12px;line-height:1;display:flex;align-items:center;"
      "justify-content:center}"
      ".oi{font-size:12.5px;color:var(--ink-soft);line-height:1.55;"
      "padding:0 0 12px;display:none}"
      ".oi.open{display:block}"
      // A row the device owns. Says so instead of showing a control that
      // cannot work here.
      ".od{font-size:12px;color:var(--ink-3);white-space:nowrap}"
      // A segmented choice: segments side by side, the active one disabled
      // rather than merely highlighted, because there is nothing to switch to.
      ".btabs{display:flex;gap:8px;flex-wrap:wrap}"
      ".btab{flex:1 1 0;min-width:104px;justify-content:center;"
      "background:var(--quiet);color:var(--ink-2);border-color:var(--line)}"
      ".btab:hover{background:var(--quiet-hover)}"
      ".btab.on{background:var(--btn);color:var(--accent);"
      "border-color:var(--accent-line);opacity:1;cursor:default}"
      ".btab.open{color:var(--accent);border-color:var(--accent-line)}"
      // The question ask() puts, in the page's own look instead of the
      // browser's box at the top of the window.
      ".ask{display:none;position:fixed;left:0;right:0;top:var(--vis-top,0px);"
      "bottom:var(--vis-bot,0px);z-index:100;align-items:center;justify-content:center;"
      "padding:16px;background:var(--ground);"
      "background:color-mix(in srgb,var(--ground) 80%,transparent)}"
      ".ask.open{display:flex}"
      ".ask .ac{width:100%;max-width:400px;max-height:100%;overflow:auto;"
      "background:var(--surface);border:1px solid var(--line);border-radius:14px;"
      "padding:24px}"
      ".ask p{color:var(--ink);font-size:15px;line-height:1.5;white-space:pre-line;"
      "overflow-wrap:anywhere;margin-bottom:20px}"
      ".ask .inrow{justify-content:flex-end}"
      // A message line pinned to the bottom of the window, for a page whose
      // controls sit further down than its own line: the Settings page, where
      // a Save at the top of a long list reported to a line off the screen.
      ".msg.toast{position:fixed;left:50%;bottom:calc(var(--vis-bot,0px) + 20px);"
      "transform:translateX(-50%);z-index:90;max-width:calc(100% - 32px);"
      "padding:10px 18px;border-radius:10px;font-size:14px;min-height:0;"
      "background:var(--surface);border:1px solid var(--accent-line);"
      "box-shadow:0 6px 24px rgba(0,0,0,.35);pointer-events:none}"
      ".msg.toast.bad{border-color:var(--bad-line)}"
      ".msg.toast:empty{display:none}"
;

const char* webStaticVersion() { return FW_VERSION; }

// The block every page's script used to carry its own copy of. There were
// three flash() implementations with three different signatures, and the POST
// boilerplate stood nine times in page_config.cpp alone.
//
// It is a route rather than something the shell pastes into each page,
// because the last attempt at sharing it was a copy: page_config.cpp still
// carries the scar in a comment - "when the pages were split the shared block
// stayed behind on one of them and every Save button here called a function
// that was no longer on the page".
//
// No translated string lives in here. The device serves pages in whichever
// language it is set to, and this file is cached across both - so the strings
// stay in the page and are assigned into WS.
static const char APP_JS[] PROGMEM =
  "var WS={ok:'OK',err:'Error',yes:'OK',no:'Cancel'};\n"

  "function $(i){return document.getElementById(i);}\n"

  // One message line, for every page. ms omitted leaves the text standing,
  // which is what a host test result or a rejected name wants; after() lets a
  // caller hand the line back to whatever else writes there instead of
  // clearing it.
  "function flash(id,t,bad,ms,after){"
  "var e=$(id);if(!e)return;"
  "e.textContent=t;e.classList.add('msg');e.classList.toggle('bad',!!bad);"
  "if(e._ft){clearTimeout(e._ft);e._ft=0;}"
  "if(ms)e._ft=setTimeout(function(){e._ft=0;"
  "if(after)after();else e.textContent='';},ms);}\n"

  // Never rejects. A closed gate answers /api/* with 403 as text/plain,
  // r.json() throws on that, and an uncaught rejection leaves an empty card
  // that looks like a fault in the firmware rather than a shut switch.
  //
  // Hands back both readings of the body so a caller can take whichever its
  // route speaks: .text for the plain replies, .json for the ones that answer
  // with an object. That is what lets the two formats coexist while they do.
  "function post(u,body){"
  "return fetch(u,{method:'POST',headers:{'Content-Type':'text/plain'},"
  "body:String(body)})"
  ".then(function(r){return r.text().then(function(t){"
  "var j=null;try{j=JSON.parse(t);}catch(e){}"
  "return{ok:r.ok,text:t,json:j};});})"
  ".catch(function(){return{ok:false,text:'',json:null};});}\n"

  // POST and report in one call. A route that answers with prose says it
  // itself - /api/host replies with the result of a real health check, and
  // overwriting that with \"saved\" would throw away the only useful part.
  "function postFlash(u,body,id,ms,after){"
  "return post(u,body).then(function(r){"
  "var okv=r.json?(r.json.ok!==false):r.ok;"
  "var msg=(r.json===null&&r.text)?r.text:(okv?WS.ok:WS.err);"
  "flash(id,msg,!okv,ms,okv?after:null);"
  "return r;});}\n"

  // Resolves with null instead of rejecting, same reason as post().
  "function getJson(u){"
  "return fetch(u,{cache:'no-store'}).then(function(r){"
  "if(!r.ok)throw 0;return r.json();}).catch(function(){return null;});}\n"

  // A yes/no question in the page's own look, resolving true or false.
  // confirm() draws the browser's box at the top of the window instead.
  // yes names the action ("Delete"), WS.yes stands in; danger paints it red
  // and puts the focus on Cancel. The text goes in as text: a tag's contents
  // can be part of it. Built on the first question, so a page that never
  // asks carries nothing; a second question answers the first with no.
  "var ASK=null;\n"
  "function ask(text,yes,danger){"
  "if(ASK&&ASK.done)ASK.done(false);"
  "if(!ASK){var b=document.createElement('div');b.className='ask';"
  "b.innerHTML='<div class=\"ac\" role=\"dialog\" aria-modal=\"true\" aria-describedby=\"askt\">'"
  "+'<p id=\"askt\"></p><div class=\"inrow\"><button type=\"button\" class=\"quiet\"></button>'"
  "+'<button type=\"button\"></button></div></div>';"
  "document.body.appendChild(b);var bs=b.querySelectorAll('button');"
  "ASK={box:b,txt:b.querySelector('p'),no:bs[0],yes:bs[1],done:null};"
  "b.addEventListener('click',function(e){if(!ASK.done)return;"
  "if(e.target===b)ASK.done(false);else if(e.target===ASK.no)ASK.done(false);"
  "else if(e.target===ASK.yes)ASK.done(true);});"
  // Esc says no; Tab stays on the two buttons instead of wandering into
  // the page behind the scrim.
  "b.addEventListener('keydown',function(e){if(!ASK.done)return;"
  "if(e.key==='Escape'){e.preventDefault();ASK.done(false);}"
  "else if(e.key==='Tab'){e.preventDefault();"
  "(document.activeElement===ASK.yes?ASK.no:ASK.yes).focus();}});}"
  "var prev=document.activeElement;"
  "ASK.txt.textContent=text;ASK.yes.textContent=yes||WS.yes;ASK.no.textContent=WS.no;"
  "ASK.yes.className=danger?'danger':'';"
  "ASK.box.classList.add('open');(danger?ASK.no:ASK.yes).focus();"
  "return new Promise(function(res){ASK.done=function(v){ASK.done=null;"
  "ASK.box.classList.remove('open');"
  "if(prev&&prev.focus){try{prev.focus();}catch(x){}}res(v);};});}\n"

  // The browser's position, once, on a click. A browser hands a position
  // only to a secure page (HTTPS or localhost) and the device serves plain
  // HTTP, so the question is asked by locate.html on the project's Pages site
  // (flasher/locate.html), in a window of its own, which posts the answer
  // back here. Resolves {lat,lon} or {err:'denied'|'blocked'|'closed'|'none'};
  // never rejects. Nothing keeps watching the position afterwards.
  "var PAGES='" APP_PAGES_URL "';\n"
  "function locateOnce(){return new Promise(function(res){"
  "if(window.isSecureContext&&navigator.geolocation){"
  "navigator.geolocation.getCurrentPosition(function(p){"
  "res({lat:p.coords.latitude,lon:p.coords.longitude});},"
  "function(e){res({err:e&&e.code===1?'denied':'none'});},"
  "{timeout:20000,maximumAge:600000});return;}"
  "var org=null;try{org=new URL(PAGES).origin;}catch(x){}"
  "if(!org){res({err:'none'});return;}"
  "var w=window.open(PAGES+'locate.html?from='+encodeURIComponent(location.origin),"
  "'locate','width=440,height=560');"
  "if(!w){res({err:'blocked'});return;}"
  "var t=setInterval(function(){if(w.closed)done({err:'closed'});},500);"
  "function on(e){var d=e.data;"
  "if(e.origin!==org||e.source!==w||!d||d.type!=='locate')return;"
  "if(typeof d.lat==='number'&&typeof d.lon==='number')done({lat:d.lat,lon:d.lon});"
  "else done({err:d.err==='denied'?'denied':'none'});}"
  "function done(r){clearInterval(t);window.removeEventListener('message',on);"
  "try{w.close();}catch(x){}res(r);}"
  "window.addEventListener('message',on);});}\n"

  // Light or dark: data-pref is the stored choice (dark, light, auto), set
  // by web_shell.cpp. auto follows the browser and keeps following it. Run
  // here in the head, where the stylesheet is already in and nothing is
  // drawn yet, so the page never flashes the wrong palette.
  "var SCH=(function(){var d=document.documentElement,"
  "mq=window.matchMedia?window.matchMedia('(prefers-color-scheme: light)'):null;\n"
  "function want(){var p=d.getAttribute('data-pref')||'auto';"
  "return p==='auto'?(mq&&mq.matches?'light':'dark'):p;}\n"
  "function apply(){d.setAttribute('data-theme',want());}\n"
  "function set(p){d.setAttribute('data-pref',p);apply();}\n"
  "if(mq){if(mq.addEventListener)mq.addEventListener('change',apply);else if(mq.addListener)mq.addListener(apply);}\n"
  "apply();return{set:set};})();\n"
;

void registerStaticRoutes(WebServer &srv) {
  srv.on("/app.css", HTTP_GET, [&srv]() {
    // Immutable for a year: the URL carries the firmware version, so the
    // content behind a given URL genuinely never changes. A new firmware
    // links a new URL rather than waiting for a cache to expire.
    srv.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
    srv.send_P(200, "text/css", APP_CSS);
  });

  srv.on("/app.js", HTTP_GET, [&srv]() {
    srv.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
    srv.send_P(200, "application/javascript", APP_JS);
  });
}
