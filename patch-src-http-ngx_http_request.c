--- src/http/ngx_http_request.c.orig	2016-11-23 14:01:33.702721000 +0100
+++ src/http/ngx_http_request.c	2016-11-23 14:04:33.688003000 +0100
@@ -9,6 +9,10 @@
 #include <ngx_core.h>
 #include <ngx_http.h>
 
+#if (NGX_HTTP_SSLMON)
+extern void
+ngx_http_sslmon_increment_ssl_handshake_errors();
+#endif
 
 static void ngx_http_wait_request_handler(ngx_event_t *ev);
 static void ngx_http_process_request_line(ngx_event_t *rev);
@@ -744,6 +748,10 @@
         return;
     }
 
+#if (NGX_HTTP_SSLMON)
+        /* counting chipers errors */
+	ngx_http_sslmon_increment_ssl_handshake_errors();
+#endif
     ngx_log_error(NGX_LOG_INFO, c->log, 0, "client closed connection");
     ngx_http_close_connection(c);
 }
