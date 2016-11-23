--- src/event/ngx_event_openssl.c.orig	2016-11-23 13:46:35.524339000 +0100
+++ src/event/ngx_event_openssl.c	2016-11-23 13:53:46.309927000 +0100
@@ -17,6 +17,10 @@
     ngx_uint_t  engine;   /* unsigned  engine:1; */
 } ngx_openssl_conf_t;
 
+#if (NGX_HTTP_SSLMON)
+extern void
+ngx_http_sslmon_increment_ssl_handshake_errors();
+#endif
 
 static int ngx_ssl_password_callback(char *buf, int size, int rwflag,
     void *userdata);
@@ -1214,10 +1218,18 @@
         c->write->handler = ngx_ssl_handshake_handler;
 
         if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
+#if (NGX_HTTP_SSLMON)
+            /* counting handshake errors */
+            ngx_http_sslmon_increment_ssl_handshake_errors();
+#endif
             return NGX_ERROR;
         }
 
         if (ngx_handle_write_event(c->write, 0) != NGX_OK) {
+#if (NGX_HTTP_SSLMON)
+            /* counting handshake errors */
+            ngx_http_sslmon_increment_ssl_handshake_errors();
+#endif
             return NGX_ERROR;
         }
 
@@ -1230,10 +1242,18 @@
         c->write->handler = ngx_ssl_handshake_handler;
 
         if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
+#if (NGX_HTTP_SSLMON)
+            /* counting handshake errors */
+            ngx_http_sslmon_increment_ssl_handshake_errors();
+#endif
             return NGX_ERROR;
         }
 
         if (ngx_handle_write_event(c->write, 0) != NGX_OK) {
+#if (NGX_HTTP_SSLMON)
+            /* counting handshake errors */
+            ngx_http_sslmon_increment_ssl_handshake_errors();
+#endif
             return NGX_ERROR;
         }
 
@@ -1249,6 +1269,10 @@
     if (sslerr == SSL_ERROR_ZERO_RETURN || ERR_peek_error() == 0) {
         ngx_connection_error(c, err,
                              "peer closed connection in SSL handshake");
+#if (NGX_HTTP_SSLMON)
+        /* counting handshake errors */
+        ngx_http_sslmon_increment_ssl_handshake_errors();
+#endif
 
         return NGX_ERROR;
     }
@@ -1256,6 +1280,10 @@
     c->read->error = 1;
 
     ngx_ssl_connection_error(c, sslerr, err, "SSL_do_handshake() failed");
+#if (NGX_HTTP_SSLMON)
+    /* counting handshake errors */
+    ngx_http_sslmon_increment_ssl_handshake_errors();
+#endif
 
     return NGX_ERROR;
 }
