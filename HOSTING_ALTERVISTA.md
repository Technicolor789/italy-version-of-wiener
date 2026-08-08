# Using this site on Altervista WordPress

This project can be served from a WordPress site hosted on Altervista,
for example `https://autolineeamicizia.altervista.org`.

## Problem

WordPress and many shared hosts do not serve `.pb` files with a useful
Content-Type by default. On Altervista WordPress, requests for `.pb`
files can also be routed through `index.php`, which returns HTML
instead of the raw binary file.

## Fix

Apply the following changes on your Altervista WordPress site.

### 1. Allow `.pb` uploads

Install one of these plugins and enable the `.pb` extension:

- **File Upload Types** (recommended)  
  `Settings -> File Upload Types -> Add custom file types`  
  Description: `Protobuf`  
  MIME type: `application/x-protobuf`  
  Extension: `.pb`

- or **WP Add Mime Types**  
  Add:  
  `application/x-protobuf = .pb`

### 2. Serve `.pb` with correct MIME type

If Altervista allows `.htaccess` in your WordPress root, create or edit
`.htaccess` and add this **before** the WordPress rewrite block:

```apache
<IfModule mod_mime.c>
    AddType application/x-protobuf .pb
</IfModule>

<IfModule mod_rewrite.c>
    RewriteEngine On
    RewriteRule \.pb$ - [L]
</IfModule>
```

The `RewriteRule` is important: it tells Apache not to send `.pb`
requests through WordPress's `index.php`.

### 3. Verify

After saving, test with:

```bash
curl -I https://autolineeamicizia.altervista.org/wp-content/uploads/example.pb
```

Expected response headers:

```
HTTP/1.1 200 OK
Content-Type: application/x-protobuf
```

### 4. Alternative: GTFS-RT proxy script

If Altervista still blocks direct `.pb` serving, use the included
`wp-content/gtfs-rt-proxy.php` script.

Preset feeds:
- `toscana`
- `atac_roma`
- `actv_veneto`
- `busitalia_veneto`
- `busitalia_tram`

Examples:
```bash
curl -I "https://autolineeamicizia.altervista.org/wp-content/gtfs-rt-proxy.php?feed=toscana"
curl -I "https://autolineeamicizia.altervista.org/wp-content/gtfs-rt-proxy.php?feed=atac_roma"
curl "https://autolineeamicizia.altervista.org/wp-content/gtfs-rt-proxy.php?feed=busitalia_veneto" -o busitalia.pb
```

For custom feeds:
```bash
curl "https://autolineeamicizia.altervista.org/wp-content/gtfs-rt-proxy.php?url=https%3A%2F%2Fexample.com%2Ffeed.pb" -o custom.pb
```

## ESP32 usage

Point the ESP32 to your proxy URLs instead of the direct `.pb` links:

```cpp
#define URL_BASE \
  "https://autolineeamicizia.altervista.org/wp-content/gtfs-rt-proxy.php?feed="
```

Then pass the feed name as the RBL/stop parameter in the WiFiManager config.

## Notes

- On Altervista's **free WordPress plan**, `.htaccess` access can be
  limited. If `.htaccess` changes are ignored, use the proxy script as
  the reliable fallback.
- For Milano, wait for the API key and add it either to the proxy script
  as a new preset or pass it via query string if the feed supports it.
- If you move the `.pb` file outside `wp-content/uploads/`, make sure
  the file permissions are world-readable (`644`) and the path matches
  exactly.
