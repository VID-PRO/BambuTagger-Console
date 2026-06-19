#include "http_thumb_client.h"
#include <WiFi.h>
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────
void HttpThumbClient::begin(const char *ip) {
    strncpy(_ip, ip, sizeof(_ip) - 1);
    _ip[sizeof(_ip) - 1] = '\0';
    log_i("HttpThumb: printer IP set to %s", _ip);
}

// ─────────────────────────────────────────────────────────────
size_t HttpThumbClient::downloadThumbnail(uint8_t *out_buf, size_t max_bytes) {
    if (WiFi.status() != WL_CONNECTED) {
        log_w("HttpThumb: WiFi not connected");
        return 0;
    }
    if (!out_buf || max_bytes == 0) {
        log_w("HttpThumb: invalid buffer");
        return 0;
    }

    // Build URL
    char url[80];
    snprintf(url, sizeof(url), "http://%s/i1/get_preview.png", _ip);
    log_i("HttpThumb: GET %s", url);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(HTTP_THUMB_TIMEOUT_MS);
    // Some Bambu FW versions require this header
    http.addHeader("User-Agent", "BambuTagger/1.0");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        log_w("HttpThumb: HTTP %d", code);
        http.end();
        return 0;
    }

    int content_len = http.getSize();   // -1 if chunked / unknown
    log_i("HttpThumb: content-length = %d", content_len);

    // Bail early if server reports a size we can't fit
    if (content_len > 0 && (size_t)content_len > max_bytes) {
        log_w("HttpThumb: image too large (%d > %u)", content_len, max_bytes);
        http.end();
        return 0;
    }

    // Stream body into buffer
    WiFiClient *stream   = http.getStreamPtr();
    size_t      received = 0;
    uint32_t    t        = millis();

    while ((http.connected() || stream->available()) &&
           received < max_bytes) {

        // Honour global timeout even if connection stays open
        if (millis() - t > (uint32_t)HTTP_THUMB_TIMEOUT_MS) {
            log_w("HttpThumb: stream timeout after %u bytes", received);
            break;
        }

        size_t avail = stream->available();
        if (avail > 0) {
            size_t to_read = min(avail, max_bytes - received);
            int r = stream->readBytes(&out_buf[received], to_read);
            if (r > 0) {
                received += (size_t)r;
                t = millis();   // reset idle timer
            }
        } else {
            // Stop as soon as we have all expected bytes (avoids blocking on
            // Keep-Alive connections that never close naturally)
            if (content_len > 0 && received >= (size_t)content_len) break;
            delay(1);
        }
    }

    http.end();

    if (received == 0) {
        log_w("HttpThumb: no bytes received");
    } else {
        log_i("HttpThumb: downloaded %u bytes", received);
    }
    return received;
}
