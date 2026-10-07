/**
 * WiFi Server for Pico Synth
 *
 * Creates an Access Point and serves a web interface.
 * Includes a simple DHCP server for client IP assignment.
 */

#include "wifi_server.h"
#include "shared_state.h"
#include "display.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

// ============================================================
// DHCP Server (minimal implementation)
// ============================================================

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_MAGIC_COOKIE 0x63825363

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

#define DHCP_OPT_SUBNET_MASK    1
#define DHCP_OPT_ROUTER         3
#define DHCP_OPT_DNS            6
#define DHCP_OPT_LEASE_TIME     51
#define DHCP_OPT_MSG_TYPE       53
#define DHCP_OPT_SERVER_ID      54
#define DHCP_OPT_END            255

typedef struct __attribute__((packed)) {
    uint8_t op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint32_t ciaddr, yiaddr, siaddr, giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic;
    uint8_t options[312];
} dhcp_msg_t;

static struct udp_pcb *dhcp_pcb = NULL;
static ip4_addr_t server_ip, subnet_mask, client_ip;

static uint8_t* dhcp_add_option(uint8_t *opt, uint8_t type, uint8_t len, const void *data) {
    *opt++ = type;
    *opt++ = len;
    memcpy(opt, data, len);
    return opt + len;
}

static void dhcp_send_response(struct udp_pcb *pcb, dhcp_msg_t *request, uint8_t msg_type) {
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(dhcp_msg_t), PBUF_RAM);
    if (!p) return;

    dhcp_msg_t *resp = (dhcp_msg_t *)p->payload;
    memset(resp, 0, sizeof(dhcp_msg_t));
    resp->op = 2;
    resp->htype = 1;
    resp->hlen = 6;
    resp->xid = request->xid;
    resp->yiaddr = client_ip.addr;
    resp->siaddr = server_ip.addr;
    memcpy(resp->chaddr, request->chaddr, 16);
    resp->magic = PP_HTONL(DHCP_MAGIC_COOKIE);

    uint8_t *opt = resp->options;
    opt = dhcp_add_option(opt, DHCP_OPT_MSG_TYPE, 1, &msg_type);
    opt = dhcp_add_option(opt, DHCP_OPT_SERVER_ID, 4, &server_ip.addr);
    uint32_t lease = PP_HTONL(86400);
    opt = dhcp_add_option(opt, DHCP_OPT_LEASE_TIME, 4, &lease);
    opt = dhcp_add_option(opt, DHCP_OPT_SUBNET_MASK, 4, &subnet_mask.addr);
    opt = dhcp_add_option(opt, DHCP_OPT_ROUTER, 4, &server_ip.addr);
    opt = dhcp_add_option(opt, DHCP_OPT_DNS, 4, &server_ip.addr);
    *opt = DHCP_OPT_END;

    ip_addr_t broadcast;
    IP4_ADDR(&broadcast, 255, 255, 255, 255);
    udp_sendto(pcb, p, &broadcast, DHCP_CLIENT_PORT);
    pbuf_free(p);
}

static void dhcp_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                                const ip_addr_t *addr, u16_t port) {
    if (p->len < sizeof(dhcp_msg_t) - 312) { pbuf_free(p); return; }
    dhcp_msg_t *msg = (dhcp_msg_t *)p->payload;
    if (PP_NTOHL(msg->magic) != DHCP_MAGIC_COOKIE) { pbuf_free(p); return; }

    uint8_t msg_type = 0;
    uint8_t *opt = msg->options;
    while (*opt != DHCP_OPT_END && opt < msg->options + 312) {
        if (*opt == DHCP_OPT_MSG_TYPE) { msg_type = opt[2]; break; }
        if (*opt == 0) opt++; else opt += 2 + opt[1];
    }

    if (msg_type == DHCP_DISCOVER) dhcp_send_response(pcb, msg, DHCP_OFFER);
    else if (msg_type == DHCP_REQUEST) dhcp_send_response(pcb, msg, DHCP_ACK);
    pbuf_free(p);
}

static bool dhcp_server_init(void) {
    IP4_ADDR(&server_ip, 192, 168, 4, 1);
    IP4_ADDR(&subnet_mask, 255, 255, 255, 0);
    IP4_ADDR(&client_ip, 192, 168, 4, 2);

    dhcp_pcb = udp_new();
    if (!dhcp_pcb) return false;
    if (udp_bind(dhcp_pcb, IP_ADDR_ANY, DHCP_SERVER_PORT) != ERR_OK) {
        udp_remove(dhcp_pcb);
        return false;
    }
    udp_recv(dhcp_pcb, dhcp_recv_callback, NULL);
    return true;
}

// ============================================================
// HTTP Server
// ============================================================

static bool server_running = false;
static struct tcp_pcb *server_pcb = NULL;
static char ip_address_str[16] = "192.168.4.1";

// Tuning page for polyphony experimentation
static const char HTML_TUNE[] =
"<!DOCTYPE html>"
"<html><head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>"
"<title>Synth Tuner</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:system-ui;background:#111;color:#eee;padding:8px;font-size:12px}"
"h1{text-align:center;color:#0ff;margin-bottom:8px;font-size:16px}"
".presets{display:flex;flex-wrap:wrap;gap:4px;margin-bottom:10px;justify-content:center}"
".preset{padding:6px 10px;border:1px solid #333;background:#222;color:#888;border-radius:4px;cursor:pointer;font-size:11px}"
".preset.active{background:#0ff;color:#000;border-color:#0ff}"
".preset:hover{border-color:#0ff}"
".section{background:#1a1a1a;border-radius:6px;padding:8px;margin-bottom:8px}"
".section h2{color:#e94560;font-size:10px;margin-bottom:6px;text-transform:uppercase}"
".param{display:flex;align-items:center;margin-bottom:4px}"
".param label{width:70px;color:#888;font-size:10px}"
".param input[type=range]{flex:1;height:18px;background:#333;border-radius:3px;cursor:pointer}"
".param .val{width:30px;text-align:right;color:#0ff;font-weight:bold}"
".row{display:flex;gap:8px}"
".row .section{flex:1}"
".mode-btns{display:flex;gap:4px}"
".mode-btn{flex:1;padding:4px;border:1px solid #333;background:#222;color:#666;border-radius:3px;cursor:pointer;font-size:9px;text-align:center}"
".mode-btn.active{background:#e94560;color:#fff;border-color:#e94560}"
"#status{text-align:center;color:#555;margin-top:6px;font-size:10px}"
"@media screen and (orientation:landscape){"
"body{padding:4px;font-size:10px;height:100vh;overflow:auto}"
"h1{font-size:12px;margin-bottom:4px}"
".presets{gap:2px;margin-bottom:4px}"
".preset{padding:3px 6px;font-size:9px}"
".section{padding:4px;margin-bottom:2px}"
".section h2{font-size:8px;margin-bottom:2px}"
".param{margin-bottom:1px}"
".param label{width:50px;font-size:8px}"
".param input[type=range]{height:12px}"
".param .val{width:20px;font-size:9px}"
".row{gap:4px}"
".mode-btns{gap:2px}"
".mode-btn{padding:2px;font-size:7px}"
"#status{margin-top:2px;font-size:8px}"
"a{font-size:9px!important;padding:4px!important;margin-top:4px!important}"
"}"
"</style>"
"</head><body>"
"<h1>SYNTH TUNER</h1>"
"<div class='presets' id='presets'></div>"
"<div class='row'>"
"<div class='section'><h2>Envelope</h2>"
"<div class='param'><label>Attack</label><input type='range' id='atk' min='1' max='64' oninput='set(\"atk\",this.value)'><span class='val' id='atkV'>2</span></div>"
"<div class='param'><label>Decay</label><input type='range' id='dec' min='1' max='32' oninput='set(\"dec\",this.value)'><span class='val' id='decV'>1</span></div>"
"<div class='param'><label>Sustain</label><input type='range' id='sus' min='0' max='255' oninput='set(\"sus\",this.value)'><span class='val' id='susV'>180</span></div>"
"<div class='param'><label>Release</label><input type='range' id='rel' min='1' max='32' oninput='set(\"rel\",this.value)'><span class='val' id='relV'>2</span></div>"
"</div>"
"<div class='section'><h2>Gain</h2>"
"<div class='param'><label>Headroom</label><input type='range' id='head' min='50' max='150' oninput='set(\"head\",this.value)'><span class='val' id='headV'>100</span></div>"
"<div class='param'><label>Sat Thresh</label><input type='range' id='satt' min='50' max='150' oninput='set(\"satt\",this.value)'><span class='val' id='sattV'>100</span></div>"
"<div class='param'><label>Sat Hard</label><input type='range' id='sath' min='0' max='100' oninput='set(\"sath\",this.value)'><span class='val' id='sathV'>50</span></div>"
"<div style='margin-top:4px'><label style='color:#888;font-size:9px'>Gain Mode:</label>"
"<div class='mode-btns' id='gainMode'>"
"<div class='mode-btn' onclick='setMode(\"gm\",0)'>Lin</div>"
"<div class='mode-btn active' onclick='setMode(\"gm\",1)'>Sqrt</div>"
"<div class='mode-btn' onclick='setMode(\"gm\",2)'>Const</div>"
"<div class='mode-btn' onclick='setMode(\"gm\",3)'>Soft</div>"
"</div></div>"
"</div>"
"</div>"
"<div class='row'>"
"<div class='section'><h2>Detune</h2>"
"<div class='param'><label>Amount</label><input type='range' id='deta' min='0' max='100' oninput='set(\"deta\",this.value)'><span class='val' id='detaV'>50</span></div>"
"<div style='margin-top:4px'><label style='color:#888;font-size:9px'>Mode:</label>"
"<div class='mode-btns' id='detMode'>"
"<div class='mode-btn' onclick='setMode(\"dm\",0)'>Off</div>"
"<div class='mode-btn active' onclick='setMode(\"dm\",1)'>Alt</div>"
"<div class='mode-btn' onclick='setMode(\"dm\",2)'>Spread</div>"
"<div class='mode-btn' onclick='setMode(\"dm\",3)'>Rnd</div>"
"</div></div>"
"</div>"
"<div class='section'><h2>Phase</h2>"
"<div class='param'><label>Stereo</label><input type='range' id='ster' min='0' max='100' oninput='set(\"ster\",this.value)'><span class='val' id='sterV'>0</span></div>"
"<div style='margin-top:4px'><label style='color:#888;font-size:9px'>Start Phase:</label>"
"<div class='mode-btns' id='phMode'>"
"<div class='mode-btn' onclick='setMode(\"pm\",0)'>Sync</div>"
"<div class='mode-btn active' onclick='setMode(\"pm\",1)'>Rnd</div>"
"<div class='mode-btn' onclick='setMode(\"pm\",2)'>Spread</div>"
"</div></div>"
"</div>"
"</div>"
"<div id='status'>Connecting...</div>"
"<a href='/' style='display:block;text-align:center;margin-top:8px;color:#0ff;font-size:11px;text-decoration:none;padding:8px;background:#1a1a1a;border-radius:4px;border:1px solid #333'>&larr; Back to Main</a>"
"<script>"
"const PRESETS=['STOCK','CLEAN','ANALOG','BRIGHT','PAD','TIGHT','WIDE','CUSTOM'];"
"let curPreset=0,curParams={};"
"function init(){"
"let h='';PRESETS.forEach((n,i)=>h+='<div class=\"preset\" onclick=\"loadPreset('+i+')\" id=\"p'+i+'\">'+n+'</div>');"
"document.getElementById('presets').innerHTML=h;poll()}"
"function upd(d){"
"curPreset=d.pr;curParams=d.p;"
"PRESETS.forEach((_,i)=>document.getElementById('p'+i).classList.toggle('active',i==d.pr));"
"['atk','dec','sus','rel','head','satt','sath','deta','ster'].forEach(k=>{"
"if(document.getElementById(k)){"
"document.getElementById(k).value=d.p[k];"
"document.getElementById(k+'V').textContent=d.p[k]}});"
"['gainMode','detMode','phMode'].forEach((id,i)=>{"
"const v=[d.p.gm,d.p.dm,d.p.pm][i];"
"document.getElementById(id).querySelectorAll('.mode-btn').forEach((b,j)=>b.classList.toggle('active',j==v))});"
"document.getElementById('status').textContent='Preset: '+PRESETS[d.pr]}"
"function poll(){fetch('/tune/api').then(r=>r.json()).then(upd).catch(()=>document.getElementById('status').textContent='...');setTimeout(poll,300)}"
"function set(k,v){fetch('/tune/set?'+k+'='+v).catch(()=>{})}"
"function setMode(k,v){fetch('/tune/set?'+k+'='+v).catch(()=>{})}"
"function loadPreset(i){fetch('/tune/set?pr='+i).catch(()=>{})}"
"init();"
"</script>"
"</body></html>";

// Embedded HTML page with waveform/octave controls and filter slider
static const char HTML_PAGE[] =
"<!DOCTYPE html>"
"<html><head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>"
"<title>PicoSynth</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:system-ui;background:#1a1a2e;color:#eee;min-height:100vh;padding:12px}"
"h1{text-align:center;color:#0ff;margin-bottom:10px;text-shadow:0 0 10px #0ff;font-size:20px}"
".container{max-width:400px;margin:0 auto}"
".card{background:#16213e;border-radius:10px;padding:10px;margin-bottom:8px;border:1px solid #0f3460}"
".card h2{color:#e94560;font-size:10px;margin-bottom:6px;text-transform:uppercase;letter-spacing:1px}"
".wave-display{height:120px;background:#0d1b2a;border-radius:8px;overflow:hidden}"
"#waveCanvas{width:100%;height:100%}"
".wave-ctrl{display:flex;align-items:center;justify-content:space-between;margin-top:6px}"
".wave-name{font-size:18px;color:#0ff;font-weight:bold}"
".ctrl-group{display:flex;gap:4px;align-items:center}"
".ctrl-label{font-size:10px;color:#888;margin-right:4px}"
".ctrl-btn{width:36px;height:36px;border:1px solid #0f3460;background:#0d1b2a;color:#0ff;border-radius:6px;font-size:18px;font-weight:bold;cursor:pointer}"
".ctrl-btn:active{background:#0ff;color:#000}"
".ctrl-val{min-width:30px;text-align:center;font-size:16px;font-weight:bold;color:#0ff}"
".info-grid{display:grid;grid-template-columns:1fr;gap:6px}"
".info-item{background:#0d1b2a;padding:8px;border-radius:6px;text-align:center}"
".info-label{font-size:9px;color:#888;margin-bottom:2px}"
".info-value{font-size:22px;font-weight:bold;color:#0ff}"
".bar-container{height:6px;background:#222;border-radius:3px;overflow:hidden;margin-top:4px}"
".bar{height:100%;background:linear-gradient(90deg,#e94560,#0ff);transition:width 0.2s}"
".slider-ctrl{margin-top:10px;display:flex;align-items:center;gap:8px}"
".slider-ctrl label{font-size:11px;color:#888;min-width:55px}"
".slider-ctrl input{flex:1;height:28px}"
".slider-ctrl .val{min-width:35px;text-align:right;color:#0ff;font-weight:bold;font-size:16px}"
".notes{display:flex;gap:6px;flex-wrap:wrap;justify-content:center;min-height:60px;align-items:center;padding:12px 4px}"
".note{background:#e94560;color:#fff;padding:10px 14px;border-radius:6px;font-size:16px;font-weight:bold}"
".note.off{background:#333;color:#666}"
".status{text-align:center;font-size:9px;color:#555;margin-top:6px}"
"@media screen and (orientation:landscape){"
"body{padding:6px;height:100vh;overflow:auto}"
"h1{font-size:14px;margin-bottom:4px}"
".container{max-width:100%;display:flex;flex-wrap:wrap;gap:6px;align-content:flex-start}"
".card{flex:1 1 48%;margin-bottom:0;padding:6px;min-width:180px}"
".card h2{font-size:8px;margin-bottom:4px}"
".wave-display{height:60px}"
".wave-name{font-size:14px}"
".ctrl-btn{width:28px;height:28px;font-size:14px}"
".ctrl-val{font-size:12px}"
".info-grid{gap:4px}"
".info-item{padding:4px}"
".info-label{font-size:7px}"
".info-value{font-size:14px}"
".slider-ctrl{margin-top:4px}"
".slider-ctrl label{font-size:9px;min-width:45px}"
".slider-ctrl input{height:20px}"
".slider-ctrl .val{font-size:12px}"
".notes{min-height:50px;padding:8px 4px;gap:4px}"
".note{padding:8px 12px;font-size:14px}"
".status{font-size:8px;margin-top:4px}"
"a{font-size:8px!important;padding:4px!important;margin-top:4px!important}"
"}"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1>PicoSynth</h1>"
"<div class='card'>"
"<h2>Waveform</h2>"
"<div class='wave-display'><canvas id='waveCanvas'></canvas></div>"
"<div class='wave-ctrl'>"
"<div class='ctrl-group'><span class='ctrl-label'>WAVE</span><button class='ctrl-btn' onclick='send(\"wf=-1\")'>-</button><span class='ctrl-val' id='waveType'>SINE</span><button class='ctrl-btn' onclick='send(\"wf=1\")'>+</button></div>"
"<div class='ctrl-group'><span class='ctrl-label'>OCT</span><button class='ctrl-btn' onclick='send(\"oc=-1\")'>-</button><span class='ctrl-val' id='octave'>4</span><button class='ctrl-btn' onclick='send(\"oc=1\")'>+</button></div>"
"</div>"
"</div>"
"<div class='card'>"
"<h2>Controls</h2>"
"<div class='info-grid'>"
"<div class='info-item'><div class='info-label'>VOLUME</div><div class='info-value' id='volume'>50</div><div class='bar-container'><div class='bar' id='volBar'></div></div></div>"
"</div>"
"<div class='slider-ctrl'>"
"<label>VIBRATO</label><input type='range' id='vibSlider' min='0' max='100' value='0' oninput='setVib(this.value)'><span class='val' id='vibVal'>0</span>"
"</div>"
"<div class='slider-ctrl'>"
"<label>FILTER</label><input type='range' id='fltSlider' min='0' max='100' value='100' oninput='setFlt(this.value)'><span class='val' id='fltVal'>100</span>"
"</div>"
"</div>"
"<div class='card'>"
"<h2>Notes</h2>"
"<div class='notes' id='notes'></div>"
"</div>"
"<div class='status' id='status'>Connecting...</div>"
"<a href='/tune' style='display:block;text-align:center;margin-top:8px;color:#e94560;font-size:11px;text-decoration:none;padding:8px;background:#0d1b2a;border-radius:6px;border:1px solid #0f3460'>&#9881; Advanced Tuning</a>"
"</div>"
"<script>"
"const canvas=document.getElementById('waveCanvas'),ctx=canvas.getContext('2d');"
"let waveType=0,phase=0,vibOverride=false,fltOverride=false;"
"function resize(){canvas.width=canvas.offsetWidth*2;canvas.height=canvas.offsetHeight*2;ctx.scale(2,2)}resize();"
"function draw(){const w=canvas.width/2,h=canvas.height/2;ctx.fillStyle='#0d1b2a';ctx.fillRect(0,0,w,h);"
"ctx.strokeStyle='#0ff';ctx.lineWidth=2;ctx.beginPath();"
"for(let x=0;x<w;x++){let y=h/2,t=(x/w+phase)*Math.PI*4;"
"if(waveType==0)y+=Math.sin(t)*h*0.4;"
"else if(waveType==1)y+=(((t/Math.PI)%2)-1)*h*0.4;"
"else if(waveType==2)y+=(t%(Math.PI*2)<Math.PI?1:-1)*h*0.35;"
"else y+=(1-4*Math.abs(((t/Math.PI/2)%1)-0.5))*h*0.4;"
"x==0?ctx.moveTo(x,y):ctx.lineTo(x,y)}ctx.stroke();phase+=0.01;requestAnimationFrame(draw)}draw();"
"const NOTES=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'],WAVES=['SINE','SAW','SQUARE','TRI'];"
"function upd(d){waveType=d.w;document.getElementById('waveType').textContent=WAVES[d.w]||'SINE';"
"document.getElementById('octave').textContent=d.o+4;"
"document.getElementById('volume').textContent=d.v;document.getElementById('volBar').style.width=d.v+'%';"
"if(!vibOverride){document.getElementById('vibSlider').value=d.b;document.getElementById('vibVal').textContent=d.b}"
"if(!fltOverride){document.getElementById('fltSlider').value=d.f;document.getElementById('fltVal').textContent=d.f}"
"let n='';for(let i=0;i<12;i++){const on=(d.p>>i)&1;n+='<span class=\"note'+(on?'':' off')+'\">'+NOTES[i]+'</span>'}"
"document.getElementById('notes').innerHTML=n;document.getElementById('status').textContent='Connected'}"
"function poll(){fetch('/api').then(r=>r.json()).then(upd).catch(()=>document.getElementById('status').textContent='...');setTimeout(poll,200)}poll();"
"function send(p){fetch('/set?'+p).catch(()=>{})}"
"function setVib(v){vibOverride=true;document.getElementById('vibVal').textContent=v;send('vo=1&vv='+v)}"
"function setFlt(v){fltOverride=true;document.getElementById('fltVal').textContent=v;send('fo=1&fv='+v)}"
"</script>"
"</body></html>";

static char json_buffer[256];

static int build_json_response(char *buf, int max_len) {
    return snprintf(buf, max_len,
        "{\"w\":%d,\"o\":%d,\"v\":%d,\"b\":%d,\"f\":%d,\"p\":%lu}",
        (int)shared_get_waveform(), (int)shared_get_octave(),
        (int)shared_get_volume(), (int)shared_get_vibrato(),
        (int)shared_get_filter(), (unsigned long)shared_get_pressed());
}

static int build_tune_json(char *buf, int max_len) {
    SynthParams p = shared_get_params();
    return snprintf(buf, max_len,
        "{\"pr\":%d,\"p\":{"
        "\"atk\":%d,\"dec\":%d,\"sus\":%d,\"rel\":%d,"
        "\"deta\":%d,\"dm\":%d,\"gm\":%d,\"head\":%d,"
        "\"satt\":%d,\"sath\":%d,\"pm\":%d,\"ster\":%d}}",
        (int)shared_get_preset(),
        (int)p.attack_rate, (int)p.decay_rate, (int)p.sustain_level, (int)p.release_rate,
        (int)p.detune_amount, (int)p.detune_mode, (int)p.gain_mode, (int)p.headroom,
        (int)p.sat_threshold, (int)p.sat_hardness, (int)p.phase_mode, (int)p.stereo_width);
}

static const char HTTP_HEADER_HTML[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";
static const char HTTP_HEADER_JSON[] =
    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
static const char HTTP_OK[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nOK";

// Parse tune query string and apply synth parameters
static void handle_tune_set(const char *query) {
    const char *p = query;
    while (p && *p) {
        int val = -1;
        if (strncmp(p, "pr=", 3) == 0) {
            val = atoi(p + 3);
            if (val >= 0 && val < PRESET_COUNT) shared_set_preset(val);
        }
        else if (strncmp(p, "atk=", 4) == 0) {
            val = atoi(p + 4);
            if (val >= 1 && val <= 64) shared_set_param_attack(val);
        }
        else if (strncmp(p, "dec=", 4) == 0) {
            val = atoi(p + 4);
            if (val >= 1 && val <= 32) shared_set_param_decay(val);
        }
        else if (strncmp(p, "sus=", 4) == 0) {
            val = atoi(p + 4);
            if (val >= 0 && val <= 255) shared_set_param_sustain(val);
        }
        else if (strncmp(p, "rel=", 4) == 0) {
            val = atoi(p + 4);
            if (val >= 1 && val <= 32) shared_set_param_release(val);
        }
        else if (strncmp(p, "deta=", 5) == 0) {
            val = atoi(p + 5);
            if (val >= 0 && val <= 100) shared_set_param_detune_amount(val);
        }
        else if (strncmp(p, "dm=", 3) == 0) {
            val = atoi(p + 3);
            if (val >= 0 && val <= 3) shared_set_param_detune_mode(val);
        }
        else if (strncmp(p, "gm=", 3) == 0) {
            val = atoi(p + 3);
            if (val >= 0 && val <= 3) shared_set_param_gain_mode(val);
        }
        else if (strncmp(p, "head=", 5) == 0) {
            val = atoi(p + 5);
            if (val >= 50 && val <= 150) shared_set_param_headroom(val);
        }
        else if (strncmp(p, "satt=", 5) == 0) {
            val = atoi(p + 5);
            if (val >= 50 && val <= 150) shared_set_param_sat_threshold(val);
        }
        else if (strncmp(p, "sath=", 5) == 0) {
            val = atoi(p + 5);
            if (val >= 0 && val <= 100) shared_set_param_sat_hardness(val);
        }
        else if (strncmp(p, "pm=", 3) == 0) {
            val = atoi(p + 3);
            if (val >= 0 && val <= 2) shared_set_param_phase_mode(val);
        }
        else if (strncmp(p, "ster=", 5) == 0) {
            val = atoi(p + 5);
            if (val >= 0 && val <= 100) shared_set_param_stereo(val);
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

// Parse query string and apply settings
static void handle_set_request(const char *query) {
    // Parse parameters
    // wf=0, oc=0 means "no change" - only act if explicitly set to 1 or -1
    int vo = -1, vv = -1, fo = -1, fv = -1, wf = 0, oc = 0;

    const char *p = query;
    while (p && *p) {
        if (strncmp(p, "vo=", 3) == 0) vo = atoi(p + 3);
        else if (strncmp(p, "vv=", 3) == 0) vv = atoi(p + 3);
        else if (strncmp(p, "fo=", 3) == 0) fo = atoi(p + 3);
        else if (strncmp(p, "fv=", 3) == 0) fv = atoi(p + 3);
        else if (strncmp(p, "wf=", 3) == 0) wf = atoi(p + 3);
        else if (strncmp(p, "oc=", 3) == 0) oc = atoi(p + 3);
        p = strchr(p, '&');
        if (p) p++;
    }

    if (vo >= 0 || vv >= 0) {
        bool enabled = (vo > 0);
        uint8_t val = (vv >= 0 && vv <= 100) ? vv : 0;
        shared_set_web_vibrato(enabled, val);
        printf("[WEB] Vibrato override=%d val=%d\n", enabled, val);
    }
    if (fo >= 0 || fv >= 0) {
        bool enabled = (fo > 0);
        uint8_t val = (fv >= 0 && fv <= 100) ? fv : 100;
        shared_set_web_filter(enabled, val);
        printf("[WEB] Filter override=%d val=%d\n", enabled, val);
    }
    if (wf != 0) {
        // wf=1 for next, wf=-1 for previous
        WaveformType cur = shared_get_waveform();
        int next = (int)cur + wf;
        if (next < 0) next = 3;  // Wrap to TRI
        if (next > 3) next = 0;  // Wrap to SINE
        shared_set_waveform((WaveformType)next);
        printf("[WEB] Waveform -> %d\n", next);
    }
    if (oc != 0) {
        // oc=1 for up, oc=-1 for down
        int8_t cur = shared_get_octave();
        int next = cur + oc;
        if (next < -1) next = 1;  // Wrap
        if (next > 1) next = -1;
        shared_set_octave((int8_t)next);
        printf("[WEB] Octave -> %d\n", next);
    }
}

static err_t tcp_server_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    if (p == NULL) { tcp_close(pcb); return ERR_OK; }

    char *request = (char *)p->payload;

    // Route detection
    bool is_api = (strstr(request, "GET /api") != NULL);
    bool is_set = (strstr(request, "GET /set?") != NULL);
    bool is_root = (strstr(request, "GET / ") != NULL);
    bool is_tune = (strstr(request, "GET /tune ") != NULL || strstr(request, "GET /tune/") != NULL);
    bool is_tune_api = (strstr(request, "GET /tune/api") != NULL);
    bool is_tune_set = (strstr(request, "GET /tune/set?") != NULL);

    tcp_recved(pcb, p->tot_len);

    if (is_tune_set) {
        // Tuning parameter change
        char *query = strstr(request, "/tune/set?");
        if (query) {
            query += 10;  // Skip "/tune/set?"
            char *end = strchr(query, ' ');
            if (end) *end = '\0';
            handle_tune_set(query);
        }
        tcp_write(pcb, HTTP_OK, strlen(HTTP_OK), TCP_WRITE_FLAG_COPY);
    } else if (is_tune_api) {
        // Tuning API endpoint
        tcp_write(pcb, HTTP_HEADER_JSON, strlen(HTTP_HEADER_JSON), TCP_WRITE_FLAG_COPY);
        int len = build_tune_json(json_buffer, sizeof(json_buffer));
        tcp_write(pcb, json_buffer, len, TCP_WRITE_FLAG_COPY);
    } else if (is_tune) {
        // Tuning page
        tcp_write(pcb, HTTP_HEADER_HTML, strlen(HTTP_HEADER_HTML), TCP_WRITE_FLAG_COPY);
        tcp_write(pcb, HTML_TUNE, strlen(HTML_TUNE), TCP_WRITE_FLAG_COPY);
    } else if (is_set) {
        char *query = strstr(request, "/set?");
        if (query) {
            query += 5;  // Skip "/set?"
            char *end = strchr(query, ' ');
            if (end) *end = '\0';
            handle_set_request(query);
        }
        tcp_write(pcb, HTTP_OK, strlen(HTTP_OK), TCP_WRITE_FLAG_COPY);
    } else if (is_api) {
        tcp_write(pcb, HTTP_HEADER_JSON, strlen(HTTP_HEADER_JSON), TCP_WRITE_FLAG_COPY);
        int len = build_json_response(json_buffer, sizeof(json_buffer));
        tcp_write(pcb, json_buffer, len, TCP_WRITE_FLAG_COPY);
    } else if (is_root) {
        tcp_write(pcb, HTTP_HEADER_HTML, strlen(HTTP_HEADER_HTML), TCP_WRITE_FLAG_COPY);
        tcp_write(pcb, HTML_PAGE, strlen(HTML_PAGE), TCP_WRITE_FLAG_COPY);
    }

    pbuf_free(p);
    tcp_output(pcb);
    tcp_close(pcb);
    return ERR_OK;
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    if (err != ERR_OK || newpcb == NULL) return ERR_VAL;
    tcp_setprio(newpcb, TCP_PRIO_MIN);
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

bool wifi_server_init(void) {
    printf("[WiFi] Initializing...\n");
    if (cyw43_arch_init()) return false;

    cyw43_arch_enable_ap_mode(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);
    sleep_ms(100);

    ip4_addr_t gw, mask;
    IP4_ADDR(&gw, 192, 168, 4, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    struct netif *n = netif_list;
    while (n) { netif_set_addr(n, &gw, &mask, &gw); netif_set_up(n); n = n->next; }

    dhcp_server_init();

    for (int i = 0; i < 10; i++) { cyw43_arch_poll(); sleep_ms(10); }

    server_pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!server_pcb || tcp_bind(server_pcb, IP_ADDR_ANY, HTTP_PORT) != ERR_OK) return false;
    server_pcb = tcp_listen_with_backlog(server_pcb, 2);
    if (!server_pcb) return false;
    tcp_accept(server_pcb, tcp_server_accept);

    server_running = true;
    printf("[WiFi] Ready! SSID='%s'\n", WIFI_SSID);
    printf("  Main UI: http://%s/\n", ip_address_str);
    printf("  Tuner:   http://%s/tune\n", ip_address_str);
    return true;
}

void wifi_server_poll(void) { cyw43_arch_poll(); }
bool wifi_server_is_running(void) { return server_running; }
const char* wifi_server_get_ip(void) { return ip_address_str; }
