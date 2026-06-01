#include "http_file_server.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "http_fs";

#define FS_BASE "/littlefs"

/* ------------------------------------------------------------------ */
/*  MIME type helper                                                   */
/* ------------------------------------------------------------------ */

static const char *mime_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".be") == 0)    return "text/plain";
    if (strcmp(dot, ".bep") == 0)   return "text/plain";
    if (strcmp(dot, ".txt") == 0)   return "text/plain";
    if (strcmp(dot, ".html") == 0)  return "text/html";
    if (strcmp(dot, ".css") == 0)   return "text/css";
    if (strcmp(dot, ".js") == 0)    return "application/javascript";
    if (strcmp(dot, ".json") == 0)  return "application/json";
    if (strcmp(dot, ".png") == 0)   return "image/png";
    if (strcmp(dot, ".jpg") == 0
        || strcmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dot, ".svg") == 0)   return "image/svg+xml";
    if (strcmp(dot, ".ico") == 0)   return "image/x-icon";
    if (strcmp(dot, ".xml") == 0)   return "application/xml";
    return "application/octet-stream";
}

/* ------------------------------------------------------------------ */
/*  Chunked HTML helpers                                                */
/* ------------------------------------------------------------------ */

/* Send a chunk of HTML. Returns ESP_OK on success. */
static esp_err_t chunk(httpd_req_t *req, const char *s)
{
    return httpd_resp_send_chunk(req, s, strlen(s));
}

#define CHUNK(...) do { \
    char _b[256]; \
    int _n = snprintf(_b, sizeof(_b), __VA_ARGS__); \
    if (_n > 0) { \
        if (_n >= (int)sizeof(_b)) _n = (int)sizeof(_b) - 1; \
        if (chunk(req, _b) != ESP_OK) return; \
    } \
} while(0)

/* ------------------------------------------------------------------ */
/*  HTML listing — streamed as chunks                                  */
/* ------------------------------------------------------------------ */

static void send_listing(httpd_req_t *req, const char *fs_path,
                         const char *uri_path)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    /* Header + styles + JS */
    chunk(req,
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<title>LittleFS</title>"
        "<style>"
        "*{box-sizing:border-box}"
        "body{font-family:system-ui,sans-serif;margin:0;padding:16px;background:#1e1e1e;color:#ccc;font-size:14px}"
        "h1{color:#eee;font-size:1.1em;margin:0 0 12px}"
        "table{width:100%;border-collapse:collapse}"
        "th,td{text-align:left;padding:6px 8px;border-bottom:1px solid #333}"
        "th{color:#888;font-weight:500;font-size:12px}"
        "a{color:#4fc3f7;text-decoration:none}"
        "a:hover{text-decoration:underline}"
        ".s{color:#888;font-size:12px;width:70px}"
        ".del{color:#f44;cursor:pointer;font-size:13px;padding:2px 6px;border-radius:3px}"
        ".del:hover{background:#f441}"
        ".up-area{border:2px dashed #444;border-radius:8px;padding:24px;text-align:center;margin-top:16px;color:#666}"
        ".up-area.dragover{border-color:#4fc3f7;background:#2a2a3e;color:#4fc3f7}"
        ".up-area input[type=file]{display:none}"
        ".up-area label{cursor:pointer;color:#4fc3f7;text-decoration:underline}"
        "#status{font-size:12px;margin-top:8px;color:#4fc3f7;display:none}"
        ".bread{font-size:12px;margin-bottom:12px;color:#888}"
        ".bread a{color:#4fc3f7}"
        "</style>"
        "<script>"
        "function st(t){var s=document.getElementById('status');s.textContent=t;s.style.display='block'}"
        "function up(f){"
        "st('Uploading '+f.name+'...');"
        "var u=location.pathname.replace(/\\/+$/,'')+'/'+encodeURIComponent(f.name);"
        "var x=new XMLHttpRequest();"
        "x.open('PUT',u,true);"
        "x.onload=function(){st('');if(x.status==200)location.reload();else alert('Upload failed: '+x.status)};"
        "x.send(f);"
        "}"
        "function drop(e){e.preventDefault();st('');"
        "for(var i=0;i<e.dataTransfer.files.length;i++)up(e.dataTransfer.files[i]);}"
        "function pick(e){if(e.target.files.length)up(e.target.files[0]);e.target.value=''}"
        "function del(p){if(!confirm('Delete '+p.split('/').pop()+'?'))return;"
        "var x=new XMLHttpRequest();"
        "x.open('DELETE',p,true);"
        "x.onload=function(){if(x.status==200)location.reload();else alert('Delete failed')};"
        "x.send()}"
        "</script>"
        "</head>"
        "<body ondragover='event.preventDefault()' ondrop='drop(event)'>"
        "<h1>LittleFS</h1>"
        "<div class='bread'><a href='/'>/</a>");

    /* Breadcrumb path (relative to root) */
    {
        const char *p = uri_path;
        while (*p) {
            /* Skip leading '/' */
            if (*p == '/') { p++; continue; }
            /* Find next '/' */
            const char *slash = strchr(p, '/');
            if (slash) {
                CHUNK("%s/", p);
                p = slash + 1;
            } else {
                CHUNK("%s", p);
                break;
            }
        }
    }

    chunk(req, "</div><table><thead><tr><th>Name</th><th>Size</th><th></th></tr></thead><tbody>");

    /* Parent directory link */
    if (strlen(uri_path) > 1) {
        char parent[512];
        snprintf(parent, sizeof(parent), "%s", uri_path);
        size_t plen = strlen(parent);
        if (plen > 1 && parent[plen - 1] == '/') parent[--plen] = '\0';
        char *slash = strrchr(parent, '/');
        if (slash) {
            if (slash == parent)
                CHUNK("<tr><td><a href='/'>..</a></td><td class='s'></td><td></td></tr>");
            else {
                *slash = '\0';
                CHUNK("<tr><td><a href='%s/'>..</a></td><td class='s'></td><td></td></tr>", parent);
            }
        }
    }

    DIR *dir = opendir(fs_path);
    if (!dir) {
        CHUNK("<tr><td colspan='3'>opendir: %s</td></tr>", strerror(errno));
    } else {
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            if (e->d_name[0] == '.') continue;
            char full[512];
            snprintf(full, sizeof(full), "%s/%s", fs_path, e->d_name);
            struct stat st;
            if (stat(full, &st) != 0) continue;

            char link[512];
            if (S_ISDIR(st.st_mode)) {
                snprintf(link, sizeof(link), "%s/%s/", uri_path, e->d_name);
                CHUNK("<tr><td><a href='%s'>%s/</a></td><td class='s'></td><td></td></tr>",
                      link, e->d_name);
            } else {
                snprintf(link, sizeof(link), "%s/%s", uri_path, e->d_name);
                CHUNK("<tr><td><a href='%s'>%s</a></td>"
                      "<td class='s'>%d B</td>"
                      "<td><span class='del' onclick='del(\"%s\")' title='Delete'>&#x2715;</span></td></tr>",
                      link, e->d_name, (int)st.st_size, link);
            }
        }
        closedir(dir);
    }

    chunk(req,
        "</tbody></table>"
        "<div class='up-area' id='dropzone'"
        " ondragover='this.classList.add(\"dragover\");event.preventDefault()'"
        " ondragleave='this.classList.remove(\"dragover\")'>"
        "<label>Upload file <input type='file' onchange='pick(event)'></label>"
        " &middot; or drag files here"
        "</div>"
        "<div id='status'></div>"
        "</body></html>");

    httpd_resp_send_chunk(req, NULL, 0); /* end of chunks */
}

/* ------------------------------------------------------------------ */
/*  Handlers                                                           */
/* ------------------------------------------------------------------ */

static esp_err_t handle_get(httpd_req_t *req)
{
    char path[512];
    snprintf(path, sizeof(path), "%s%s", FS_BASE, req->uri);

    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/') path[len - 1] = '\0';

    struct stat st;
    if (stat(path, &st) != 0) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    if (S_ISDIR(st.st_mode)) {
        char uribuf[512];
        snprintf(uribuf, sizeof(uribuf), "%s", req->uri);
        size_t ulen = strlen(uribuf);
        if (ulen > 0 && uribuf[ulen - 1] == '/')
            uribuf[--ulen] = '\0';
        send_listing(req, path, uribuf);
        return ESP_OK;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    httpd_resp_set_type(req, mime_type(path));
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    char chunk_buf[512];
    ssize_t n;
    while ((n = read(fd, chunk_buf, sizeof(chunk_buf))) > 0) {
        if (httpd_resp_send_chunk(req, chunk_buf, n) != ESP_OK) break;
    }
    httpd_resp_send_chunk(req, NULL, 0);
    close(fd);
    return ESP_OK;
}

static esp_err_t handle_put(httpd_req_t *req)
{
    char path[512];
    snprintf(path, sizeof(path), "%s%s", FS_BASE, req->uri);

    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/') path[len - 1] = '\0';

    {
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
            httpd_resp_send_404(req);
            return ESP_OK;
        }
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        ESP_LOGE(TAG, "open(%s) write: %s", path, strerror(errno));
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    char chunk_buf[512];
    int remaining = req->content_len;
    while (remaining > 0) {
        int sz = remaining < (int)sizeof(chunk_buf) ? remaining : (int)sizeof(chunk_buf);
        int recvd = httpd_req_recv(req, chunk_buf, sz);
        if (recvd <= 0) {
            if (recvd == HTTPD_SOCK_ERR_TIMEOUT) continue;
            close(fd); unlink(path);
            httpd_resp_send_500(req);
            return ESP_OK;
        }
        if (write(fd, chunk_buf, recvd) != recvd) {
            close(fd); unlink(path);
            httpd_resp_send_500(req);
            return ESP_OK;
        }
        remaining -= recvd;
    }

    close(fd);
    ESP_LOGI(TAG, "uploaded %s (%d B)", path, req->content_len);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t handle_delete(httpd_req_t *req)
{
    char path[512];
    snprintf(path, sizeof(path), "%s%s", FS_BASE, req->uri);

    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/') path[len - 1] = '\0';

    struct stat st;
    if (stat(path, &st) != 0) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }
    if (S_ISDIR(st.st_mode)) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    if (unlink(path) != 0) {
        ESP_LOGE(TAG, "unlink(%s): %s", path, strerror(errno));
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "deleted %s", path);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

esp_err_t http_file_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    cfg.stack_size      = 8192;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn    = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = handle_get };
    httpd_uri_t wild = { .uri = "/*", .method = HTTP_GET, .handler = handle_get };
    httpd_uri_t upld = { .uri = "/*", .method = HTTP_PUT, .handler = handle_put };
    httpd_uri_t del  = { .uri = "/*", .method = HTTP_DELETE, .handler = handle_delete };

    esp_err_t r;
    r = httpd_register_uri_handler(server, &root);
    if (r != ESP_OK) { ESP_LOGE(TAG, "reg root: %s", esp_err_to_name(r)); return r; }
    r = httpd_register_uri_handler(server, &wild);
    if (r != ESP_OK) { ESP_LOGE(TAG, "reg wild: %s", esp_err_to_name(r)); return r; }
    r = httpd_register_uri_handler(server, &upld);
    if (r != ESP_OK) { ESP_LOGE(TAG, "reg put: %s", esp_err_to_name(r)); return r; }
    r = httpd_register_uri_handler(server, &del);
    if (r != ESP_OK) { ESP_LOGE(TAG, "reg del: %s", esp_err_to_name(r)); return r; }

    ESP_LOGI(TAG, "HTTP file server running on port %d", cfg.server_port);
    return ESP_OK;
}
