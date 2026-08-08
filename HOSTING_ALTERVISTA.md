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

### 4. GTFS-RT proxy plugin (recommended on Altervista)

Altervista's free WordPress often blocks direct PHP execution in
`wp-content/`, which causes 500 errors. The reliable method is to use
the included WordPress plugin.

1. Create the folder `wp-content/plugins/gtfs-rt-proxy/`
2. Upload `wp-content/plugins/gtfs-rt-proxy/gtfs-rt-proxy.php`
3. In WordPress admin, go to **Plugins** and activate **GTFS-RT Proxy**
4. After activation, the proxy URLs are:
   ```
   https://autolineeamicizia.altervista.org/gtfs-rt-proxy/toscana
   https://autolineeamicizia.altervista.org/gtfs-rt-proxy/atac_roma
   https://autolineeamicizia.altervista.org/gtfs-rt-proxy/actv_veneto
   https://autolineeamicizia.altervista.org/gtfs-rt-proxy/busitalia_veneto
   https://autolineeamicizia.altervista.org/gtfs-rt-proxy/busitalia_tram
   ```

### 5. Test with a simple HTML page

Upload `wp-content/gtfs-rt-viewer.html` to your WordPress installation
and open it in a browser:

```
https://autolineeamicizia.altervista.org/wp-content/gtfs-rt-viewer.html
```

This page:
- fetches the GTFS-RT feed through the proxy
- decodes the protobuf in the browser
- shows a table of trip updates

This is the fastest way to verify that your WordPress/Altervista setup
can fetch and serve real-time data before flashing the ESP32.

## ESP32 usage

Point the ESP32 to your proxy URLs instead of the direct `.pb` links:

```cpp
#define URL_BASE \
  "https://autolineeamicizia.altervista.org/gtfs-rt-proxy/"
```

Then enter the feed name in the WiFiManager `RBL/Stop ID` field.

## Notes

- On Altervista's **free WordPress plan**, `.htaccess` access can be
  limited. If `.htaccess` changes are ignored, use the proxy script as
  the reliable fallback.
- For Milano, wait for the API key and add it either to the proxy script
  as a new preset or pass it via query string if the feed supports it.
- If you move the `.pb` file outside `wp-content/uploads/`, make sure
  the file permissions are world-readable (`644`) and the path matches
  exactly.
