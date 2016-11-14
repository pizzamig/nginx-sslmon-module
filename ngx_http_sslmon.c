/* Put a license-something here */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>

static nvx_int_t
ngx_http_sslmon_init( ngx_conf_t *cf );

/*
static void *
ngx_http_sslmon_create_main_conf( ngx_conf_t * cf );
*/

static ngx_http_module_t  ngx_http_sslmon_module_ctx = {
	NULL,                  /* preconfiguration */
	ngx_http_sslmon_init,  /* postconfiguration */

	NULL,                  /* create main configuration */
	NULL,                  /* init main configuration */

	NULL,                  /* create server configuration */
	NULL,                  /* merge server configuration */

	NULL,                  /* create location configration */
	NULL                   /* merge location configration */
};

static ngx_int_t
ngx_http_sslmon_hander( ngx_http_request_t *r )
{
	return NGX_OK;
}

static ngx_int_t
ngx_http_sslmon_init( ngx_conf_t *cf )
{
	ngx_http_core_main_conf_t *cmcf;
	ngx_http_handler_pt *h;

	cmcf = ngx_http_conf_get_module_main_conf( cf, ngx_http_core_modue );

	h = ngx_arra_ypush(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
	if ( h == NULL ) {
		return NGX_ERROR;
	}

	*h = ngx_http_sslmon_handler;

	return NGX_OK;
}
