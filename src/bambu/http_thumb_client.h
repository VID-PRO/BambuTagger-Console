#pragma once
/**
 * http_thumb_client.h
 *
 * Fetches the active-print preview thumbnail from the Bambu Lab
 * printer's built-in HTTP server.
 *
 * Endpoint: GET http://{printer_ip}/i1/get_preview.png
 *
 * The printer always serves the thumbnail for the currently running
 * (or last run) job as a plain PNG — no FTPS, no ZIP, no inflation.
 * Port 80 is used (standard HTTP, no TLS required on LAN).
 */

#include <HTTPClient.h>

static const int HTTP_THUMB_TIMEOUT_MS = 10000;

class HttpThumbClient {
public:
    /**
     * @param ip  Printer LAN IP address string, e.g. "192.168.1.42"
     */
    void begin(const char *ip);

    /**
     * Download the current thumbnail PNG into out_buf.
     * @return  Number of bytes written, or 0 on failure.
     */
    size_t downloadThumbnail(uint8_t *out_buf, size_t max_bytes);

private:
    char _ip[40] = {};
};
