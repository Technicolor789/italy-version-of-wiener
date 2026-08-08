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

## Notes

- On Altervista's **free WordPress plan**, `.htaccess` access can be
  limited. If `.htaccess` changes are ignored, use a plugin such as
  **WP Add Mime Types** and ask Altervista support to confirm that
  `.pb` MIME mapping is allowed on your domain.
- If you move the `.pb` file outside `wp-content/uploads/`, make sure
  the file permissions are world-readable (`644`) and the path matches
  exactly.
