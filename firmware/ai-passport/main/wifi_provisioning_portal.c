#include "wifi_provisioning_portal.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool append_text(char *output, size_t output_size, size_t *used, const char *format, ...)
{
    if (*used >= output_size) {
        return false;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(output + *used, output_size - *used, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= output_size - *used) {
        output[0] = '\0';
        return false;
    }
    *used += (size_t)written;
    return true;
}

static bool append_json_string(char *output, size_t output_size, size_t *used, const char *text)
{
    if (!append_text(output, output_size, used, "\"")) {
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; cursor++) {
        if (*cursor == '\"' || *cursor == '\\') {
            if (!append_text(output, output_size, used, "\\%c", *cursor)) {
                return false;
            }
        } else if (*cursor < 0x20) {
            if (!append_text(output, output_size, used, "\\u%04x", *cursor)) {
                return false;
            }
        } else if (!append_text(output, output_size, used, "%c", *cursor)) {
            return false;
        }
    }
    return append_text(output, output_size, used, "\"");
}

size_t wifi_provisioning_prepare_scan_results(
    const wifi_provisioning_scan_entry_t *input,
    size_t input_count,
    wifi_provisioning_scan_entry_t *output,
    size_t max_output)
{
    if (input == NULL || output == NULL || max_output == 0) {
        return 0;
    }

    size_t count = 0;
    for (size_t input_index = 0; input_index < input_count; input_index++) {
        const wifi_provisioning_scan_entry_t *candidate = &input[input_index];
        if (candidate->ssid[0] == '\0') {
            continue;
        }

        size_t existing = count;
        for (size_t output_index = 0; output_index < count; output_index++) {
            if (strcmp(output[output_index].ssid, candidate->ssid) == 0) {
                existing = output_index;
                break;
            }
        }
        if (existing < count) {
            if (candidate->rssi > output[existing].rssi) {
                output[existing] = *candidate;
            }
            continue;
        }

        if (count < max_output) {
            output[count++] = *candidate;
            continue;
        }

        size_t weakest = 0;
        for (size_t output_index = 1; output_index < count; output_index++) {
            if (output[output_index].rssi < output[weakest].rssi) {
                weakest = output_index;
            }
        }
        if (candidate->rssi > output[weakest].rssi) {
            output[weakest] = *candidate;
        }
    }

    for (size_t left = 0; left < count; left++) {
        size_t strongest = left;
        for (size_t right = left + 1; right < count; right++) {
            if (output[right].rssi > output[strongest].rssi) {
                strongest = right;
            }
        }
        if (strongest != left) {
            wifi_provisioning_scan_entry_t swap = output[left];
            output[left] = output[strongest];
            output[strongest] = swap;
        }
    }
    return count;
}

size_t wifi_provisioning_scan_results_json(
    const wifi_provisioning_scan_entry_t *entries,
    size_t entry_count,
    char *output,
    size_t output_size)
{
    if (output == NULL || output_size == 0 || (entries == NULL && entry_count > 0)) {
        return 0;
    }

    output[0] = '\0';
    size_t used = 0;
    if (!append_text(output, output_size, &used, "{\"networks\":[")) {
        return 0;
    }
    for (size_t index = 0; index < entry_count; index++) {
        if (index > 0 && !append_text(output, output_size, &used, ",")) {
            return 0;
        }
        if (!append_text(output, output_size, &used, "{\"ssid\":")) {
            return 0;
        }
        if (!append_json_string(output, output_size, &used, entries[index].ssid)) {
            return 0;
        }
        if (!append_text(output,
                         output_size,
                         &used,
                         ",\"rssi\":%d,\"secure\":%s}",
                         entries[index].rssi,
                         entries[index].secure ? "true" : "false")) {
            return 0;
        }
    }
    if (!append_text(output, output_size, &used, "]}")) {
        return 0;
    }
    return used;
}

const char *wifi_provisioning_portal_html(void)
{
    return
        "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1'>"
        "<title>网易云音乐设备配网</title><style>"
        "*{box-sizing:border-box}body{--bg:#f0e9da;--fg:#3c3026;--sub:#77624c;--edge:#c3ad91;--panel:#e7dcc9;--ac:#75452e;margin:0;background:var(--bg);color:var(--fg);font:14px/1.7 Arial,'Noto Sans CJK SC',sans-serif}"
        "@media(prefers-color-scheme:dark){body{--bg:#29251f;--fg:#e7ddc9;--sub:#baac96;--edge:#635242;--panel:#373027;--ac:#d4a47b}}"
        "button,input{font:inherit}button{cursor:pointer}button:focus-visible,input:focus-visible{outline:2px solid var(--ac);outline-offset:3px}.top,.wrap{max-width:520px;margin:0 auto;padding:24px}.top{padding-bottom:8px}.brand{font:10px monospace;letter-spacing:2px;color:var(--sub);border-bottom:1px solid var(--edge);padding-bottom:16px}.top h1{font:30px/1.4 Georgia,'Noto Serif CJK SC',serif;font-weight:400;margin:22px 0 12px;padding-bottom:12px;border-bottom:2px solid var(--fg)}.top p,.hint,.foot{font-size:12px;color:var(--sub)}.card{padding:0}.steps{display:flex;align-items:center;margin-bottom:24px}.step{display:flex;align-items:center;color:var(--sub);font-size:11px;white-space:nowrap}.step.on{color:var(--ac)}.dot{font:20px Georgia;margin-right:8px}.line{height:1px;background:var(--edge);flex:1;margin:0 10px}h2{font-size:16px;font-weight:400;margin:0 0 5px}.hint{margin:0 0 14px}.label{font-size:12px;margin:18px 0 8px;display:block}"
        "#networks{display:grid}.network{width:100%;border:0;border-top:1px solid var(--edge);background:transparent;color:var(--fg);border-radius:0;padding:12px 8px;text-align:left;display:flex;align-items:center;justify-content:space-between;font-size:14px}.network.selected{border-top:2px solid var(--ac);background:var(--panel);color:var(--ac)}.meta{font-size:11px;color:var(--sub);margin-left:10px}.scanstate{padding:18px 10px;text-align:center;color:var(--sub);background:var(--panel);font-size:12px}.tools{display:flex;justify-content:space-between;margin-top:10px}.link{border:0;background:none;color:var(--ac);padding:6px 0;font-size:12px}"
        "input{width:100%;height:46px;border:1px solid var(--edge);border-radius:2px;padding:0 10px;font-size:14px;background:transparent;color:var(--fg)}.pw{position:relative}.pw input{padding-right:58px}.eye{position:absolute;right:7px;top:7px;height:32px;border:0;background:var(--panel);color:var(--fg)}.advanced{margin-top:22px;padding-top:18px;border-top:1px solid var(--edge)}.advanced summary{font-size:14px;cursor:pointer}.submit{width:100%;height:48px;border:0;border-radius:0;background:var(--ac);color:var(--bg);font-size:14px;margin-top:24px}.foot{border-top:1px solid var(--edge);padding-top:14px;margin-top:24px}</style></head><body>"
#ifdef CONFIG_NETEASE_DIRECT
        "<header class='top'><div class='brand'>FOLO / SETUP · 余白</div><h1>给音乐，连上线。</h1><p>选择家里的 2.4 GHz Wi-Fi，完成后在设备屏幕扫码登录网易云。</p></header>"
#else
        "<header class='top'><div class='brand'>FOLO / SETUP · 余白</div><h1>给音乐，连上线。</h1><p>选择家里的 2.4 GHz Wi-Fi，完成后设备会自动连接中转服务。</p></header>"
#endif
        "<main class='wrap'><section class='card'><div class='steps'><div class='step on'><span class='dot'>1</span>选 Wi-Fi</div><i class='line'></i><div class='step'><span class='dot'>2</span>填信息</div><i class='line'></i><div class='step'><span class='dot'>3</span>完成</div></div>"
        "<h2>选择附近的 Wi-Fi</h2><p class='hint'>仅支持 2.4 GHz 网络。列表会按信号强弱排序。</p><div id='networks'><div class='scanstate'>正在搜索附近的网络…</div></div>"
        "<div class='tools'><button class='link' id='manual' type='button'>手动输入网络名称</button><button class='link' id='rescan' type='button'>重新扫描</button></div>"
        "<form method='post' action='/save' id='setup'><label class='label' for='ssid'>Wi-Fi 名称</label><input id='ssid' name='ssid' maxlength='32' autocomplete='off' required placeholder='请先选择上面的网络'>"
        "<label class='label' for='password'>Wi-Fi 密码</label><div class='pw'><input id='password' name='password' maxlength='64' type='password' autocomplete='new-password' placeholder='请输入 Wi-Fi 密码'><button class='eye' id='eye' type='button'>显示</button></div>"
#ifndef CONFIG_NETEASE_DIRECT
        "<details class='advanced' open><summary>中转服务信息</summary><p class='hint'>从电脑上的一键启动窗口复制下面两项。</p><label class='label' for='cloud_url'>Relay URL</label><input id='cloud_url' name='cloud_url' maxlength='127' type='url' required placeholder='如 http://192.168.1.10:18080'>"
        "<label class='label' for='registration_code'>Registration Code</label><input id='registration_code' name='registration_code' maxlength='95' required placeholder='如 pair-xxxxxxxx'></details>"
#endif
        "<button class='submit' type='submit'>连接设备</button></form></section><p class='foot'>提交后请保持设备通电，并回到设备屏幕查看连接结果。</p></main>"
        "<script>const list=document.getElementById('networks'),ssid=document.getElementById('ssid');"
        "function strength(r){return r>=-55?'信号强':r>=-70?'信号中':'信号弱'}"
        "function choose(n,b){ssid.value=n.ssid;document.querySelectorAll('.network').forEach(x=>x.classList.remove('selected'));if(b)b.classList.add('selected');document.getElementById('password').focus()}"
        "async function scan(){list.innerHTML=\"<div class='scanstate'>正在搜索附近的网络…</div>\";try{const r=await fetch('/scan');if(!r.ok)throw 0;const d=await r.json();list.innerHTML='';if(!d.networks.length){list.innerHTML=\"<div class='scanstate'>没有找到网络，请重新扫描或手动输入</div>\";return}d.networks.forEach(n=>{const b=document.createElement('button');b.type='button';b.className='network';const name=document.createElement('span');name.textContent=n.ssid;const meta=document.createElement('span');meta.className='meta';meta.textContent=(n.secure?'🔒 ':'')+strength(n.rssi);b.append(name,meta);b.onclick=()=>choose(n,b);list.appendChild(b)})}catch(e){list.innerHTML=\"<div class='scanstate'>搜索失败，请点击重新扫描或手动输入</div>\"}}"
        "document.getElementById('rescan').onclick=scan;document.getElementById('manual').onclick=()=>{ssid.value='';ssid.focus()};document.getElementById('eye').onclick=e=>{const p=document.getElementById('password'),show=p.type==='password';p.type=show?'text':'password';e.target.textContent=show?'隐藏':'显示'};scan()</script></body></html>";
}

const char *wifi_provisioning_success_html(void)
{
    return
        "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>正在连接</title><style>body{margin:0;background:#f0e9da;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','Microsoft YaHei',sans-serif;color:#3c3026}.card{max-width:420px;margin:12vh auto;padding:32px 24px;text-align:left;border-top:2px solid #3c3026}.icon{font:40px Georgia;color:#8e482f}h1{font:27px Georgia,serif;font-weight:400;margin:20px 0 10px}p{color:#77624c;font-size:14px;line-height:1.7}@media(prefers-color-scheme:dark){body{background:#29251f;color:#e7ddc9}.card{border-color:#e7ddc9}.icon{color:#d4a47b}p{color:#baac96}}</style></head>"
#ifdef CONFIG_NETEASE_DIRECT
        "<body><section class='card'><div class='icon'>✓</div><h1>正在连接</h1><p>设备正在连接 Wi-Fi。<br>请回到设备屏幕扫码登录网易云。</p></section></body></html>";
#else
        "<body><section class='card'><div class='icon'>✓</div><h1>正在连接</h1><p>信息已保存，设备正在连接 Wi-Fi 和中转服务。<br>请回到设备屏幕查看结果。</p></section></body></html>";
#endif
}
