// net_portal.cpp - Captive setup portal (see net_portal.h).
#include "net_portal.h"
#include "config.h"
#include "net_wifi.h"
#include "net_mqtt.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "PORTAL";

static httpd_handle_t s_server = NULL;
static TaskHandle_t s_dns_task = NULL;
static volatile bool s_dns_running = false;

// ============================================================================
// Setup page (single self-contained page, mobile friendly)
// ============================================================================
static const char SETUP_HTML[] =
"<!DOCTYPE html><html lang=\"en\"><head>"
"<meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Lumina setup</title>"
"<style>"
"*{box-sizing:border-box}"
"body{margin:0;font-family:-apple-system,Segoe UI,Roboto,sans-serif;background:#111;color:#eee}"
".wrap{max-width:420px;margin:0 auto;padding:24px 18px}"
"h1{font-weight:600;font-size:1.3rem;margin:8px 0 4px}"
"p.sub{color:#888;font-size:.85rem;margin:0 0 24px}"
".card{background:#1d1d22;border-radius:16px;padding:20px;margin-bottom:18px}"
"label{display:block;font-size:.8rem;color:#aaa;margin:14px 0 6px;text-transform:uppercase;letter-spacing:.05em}"
"input,select{width:100%;padding:12px;border:none;border-radius:10px;background:#2a2a31;color:#eee;font-size:1rem}"
"button{width:100%;padding:14px;border:none;border-radius:12px;font-size:1rem;cursor:pointer}"
"#scan{background:#2a2a31;color:#eee;margin-bottom:4px}"
"#save{background:#4c7dff;color:#fff;font-weight:600;margin-top:20px}"
"#msg{margin-top:14px;font-size:.9rem;text-align:center;color:#2ecc71}"
"details{margin-top:16px}summary{color:#888;font-size:.85rem;cursor:pointer}"
"small{color:#666;display:block;text-align:center;margin-top:10px}"
"</style></head><body><div class=\"wrap\">"
"<h1>&#128161; Lumina setup</h1>"
"<p class=\"sub\">Connect Lumina to your WiFi network</p>"
"<div class=\"card\">"
"<button id=\"scan\">Scan networks</button>"
"<select id=\"nets\" hidden></select>"
"<label>Network name (SSID)</label><input id=\"ssid\" maxlength=\"32\" autocapitalize=\"off\">"
"<label>Password</label><input id=\"pass\" type=\"password\" maxlength=\"64\">"
"<details><summary>Advanced: MQTT broker</summary>"
"<label>Broker URI</label><input id=\"broker\" placeholder=\"mqtt://broker.hivemq.com:1883\">"
"</details>"
"<button id=\"save\">Save &amp; connect</button>"
"<div id=\"msg\"></div>"
"</div>"
"<small>Lumina &middot; the device restarts after saving</small>"
"</div><script>"
"var nets=document.getElementById('nets'),msg=document.getElementById('msg');"
"fetch('/api/config').then(r=>r.json()).then(c=>{document.getElementById('broker').value=c.broker;});"
"document.getElementById('scan').addEventListener('click',function(){"
" msg.textContent='Scanning...';"
" fetch('/api/scan').then(r=>r.json()).then(function(list){"
"  nets.innerHTML=list.map(n=>`<option value=\"${n.ssid}\">${n.ssid} (${n.rssi} dBm)</option>`).join('');"
"  nets.hidden=list.length===0;msg.textContent=list.length?'':'No networks found';"
"  if(list.length)document.getElementById('ssid').value=list[0].ssid;"
" }).catch(()=>{msg.textContent='Scan failed';});"
"});"
"nets.addEventListener('change',()=>{document.getElementById('ssid').value=nets.value;});"
"document.getElementById('save').addEventListener('click',function(){"
" var body={ssid:document.getElementById('ssid').value,"
"  password:document.getElementById('pass').value,"
"  broker:document.getElementById('broker').value};"
" if(!body.ssid){msg.textContent='Enter a network name';return;}"
" fetch('/api/provision',{method:'POST',headers:{'Content-Type':'application/json'},"
"  body:JSON.stringify(body)}).then(r=>r.json()).then(function(res){"
"   msg.textContent=res.ok?'Saved! Lumina is restarting...':(res.error||'Error');"
" }).catch(()=>{msg.textContent='Error saving';});"
"});"
"</script></body></html>";

// ============================================================================
// HTTP handlers
// ============================================================================
static esp_err_t index_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, SETUP_HTML, HTTPD_RESP_USE_STRLEN);
}

// Current configuration shown by the portal (broker URI only - never the
// WiFi password).
static esp_err_t config_handler(httpd_req_t* req) {
    char broker[128];
    netMqttGetBrokerUri(broker, sizeof(broker));
    char json[192];
    snprintf(json, sizeof(json), "{\"broker\":\"%s\"}", broker);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

// Blocking scan of nearby networks (a few seconds). The station interface is
// available because the hotspot runs in APSTA mode.
static esp_err_t scan_handler(httpd_req_t* req) {
    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "[]", HTTPD_RESP_USE_STRLEN);
    }

    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    if (count > 20) count = 20;

    wifi_ap_record_t records[20];
    esp_wifi_scan_get_ap_records(&count, records);

    cJSON* list = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        if (records[i].ssid[0] == '\0') continue;
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (const char*)records[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", records[i].rssi);
        cJSON_AddItemToArray(list, item);
    }

    char* json = cJSON_PrintUnformatted(list);
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json);
    cJSON_Delete(list);
    return ret;
}

// Delayed restart so the HTTP response reaches the browser first.
static void restart_task(void* arg) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

// POST /api/provision  {"ssid": "...", "password": "...", "broker": "..."}
static esp_err_t provision_handler(httpd_req_t* req) {
    char body[512];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "{\"ok\":false,\"error\":\"empty body\"}", HTTPD_RESP_USE_STRLEN);
    }
    body[len] = '\0';

    cJSON* root = cJSON_Parse(body);
    const cJSON* ssid = root ? cJSON_GetObjectItem(root, "ssid") : NULL;
    const cJSON* pass = root ? cJSON_GetObjectItem(root, "password") : NULL;
    const cJSON* broker = root ? cJSON_GetObjectItem(root, "broker") : NULL;

    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(ssid) && ssid->valuestring[0] != '\0') {
        err = netWifiSaveCredentials(ssid->valuestring,
                                     cJSON_IsString(pass) ? pass->valuestring : "");
    }
    if (err == ESP_OK && cJSON_IsString(broker) && broker->valuestring[0] != '\0') {
        netMqttSaveBrokerUri(broker->valuestring);
    }
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "{\"ok\":false,\"error\":\"invalid ssid\"}", HTTPD_RESP_USE_STRLEN);
    }

    esp_err_t ret = httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG, "provisioned - restarting to join '%s'", "network");
    xTaskCreate(restart_task, "restart", TASK_STACK_SIZE_SMALL, NULL, TASK_PRIORITY_LOW, NULL);
    return ret;
}

// Any other URI (including OS captive-portal probes such as /generate_204 or
// /hotspot-detect.html) redirects to the setup page, which makes phones show
// the portal automatically.
static esp_err_t redirect_handler(httpd_req_t* req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

// ============================================================================
// Minimal captive DNS: answer every A query with 192.168.4.1
// ============================================================================
static void dns_server_task(void* arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket failed");
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(sock, (struct sockaddr*)&addr, sizeof(addr));

    struct timeval timeout = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    ESP_LOGI(TAG, "captive DNS up");
    uint8_t buf[512];
    while (s_dns_running) {
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);
        int len = recvfrom(sock, buf, sizeof(buf) - 16, 0,
                           (struct sockaddr*)&client, &client_len);
        if (len < 12) continue;  // shorter than a DNS header (or timeout)

        // Turn the query into a response: QR=1, RA=1, one answer.
        buf[2] = 0x81; buf[3] = 0x80;             // flags
        buf[6] = buf[4]; buf[7] = buf[5];         // ANCOUNT = QDCOUNT
        buf[8] = buf[9] = buf[10] = buf[11] = 0;  // NSCOUNT/ARCOUNT = 0

        // Append one A record pointing at the portal address.
        static const uint8_t answer[] = {
            0xC0, 0x0C,             // name: pointer to the question
            0x00, 0x01, 0x00, 0x01, // type A, class IN
            0x00, 0x00, 0x00, 0x3C, // TTL 60s
            0x00, 0x04,             // RDLENGTH 4
            192, 168, 4, 1          // RDATA: portal IP
        };
        memcpy(buf + len, answer, sizeof(answer));
        sendto(sock, buf, len + sizeof(answer), 0,
               (struct sockaddr*)&client, client_len);
    }

    close(sock);
    ESP_LOGI(TAG, "captive DNS stopped");
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

// ============================================================================
// Start / stop
// ============================================================================
esp_err_t netPortalStart(void) {
    if (s_server != NULL) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = SETUP_HTTP_PORT;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 8;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t uris[] = {
        { .uri = "/",              .method = HTTP_GET,  .handler = index_handler,     .user_ctx = NULL },
        { .uri = "/api/config",    .method = HTTP_GET,  .handler = config_handler,    .user_ctx = NULL },
        { .uri = "/api/scan",      .method = HTTP_GET,  .handler = scan_handler,      .user_ctx = NULL },
        { .uri = "/api/provision", .method = HTTP_POST, .handler = provision_handler, .user_ctx = NULL },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, redirect_handler);

    s_dns_running = true;
    xTaskCreate(dns_server_task, "dns_captive", TASK_STACK_SIZE_MEDIUM,
                NULL, TASK_PRIORITY_LOW, &s_dns_task);

    ESP_LOGI(TAG, "setup portal started on port %d", config.server_port);
    return ESP_OK;
}

void netPortalStop(void) {
    s_dns_running = false;
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
