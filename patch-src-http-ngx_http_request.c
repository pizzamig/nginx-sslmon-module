11a12,15
> #if (NGX_HTTP_SSLMON)
> extern void
> ngx_http_sslmon_increment_ssl_handshake_errors();
> #endif
746a751,754
> #if (NGX_HTTP_SSLMON)
>     /* counting cipher errors */
>     ngx_http_sslmon_increment_ssl_handshake_errors();
> #endif
