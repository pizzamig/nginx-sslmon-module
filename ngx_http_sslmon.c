/* Put a license-something here */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>

static ngx_int_t
ngx_http_sslmon_init( ngx_conf_t *cf );

static void *
ngx_http_sslmon_create_loc_conf( ngx_conf_t * cf );

static char *
ngx_http_sslmon_merge_loc_conf(ngx_conf_t *cf, void *prev, void *conf);

typedef struct {
	ngx_str_t filename;
	int fd;
	unsigned long counter;
} ngx_http_sslmon_loc_conf_t;

typedef struct {
	unsigned long counter;
} ngx_http_sslmon_stats_t;

static ngx_command_t ngx_http_sslmon_commands[] = {

        { ngx_string("sslmon_output_file"),
          NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
          ngx_conf_set_str_slot,
          NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_sslmon_loc_conf_t, filename),
	  NULL },

	ngx_null_command

};

static ngx_http_module_t  ngx_http_sslmon_module_ctx = {
	NULL,					/* preconfiguration */
	ngx_http_sslmon_init,			/* postconfiguration */

	NULL,					/* create main configuration */
	NULL,					/* init main configuration */

	NULL,					/* create server configuration */
	NULL,					/* merge server configuration */

	ngx_http_sslmon_create_loc_conf,	/* create location configration */
	ngx_http_sslmon_merge_loc_conf,		/* merge location configration */
};

ngx_module_t ngx_http_sslmon_module = {
	NGX_MODULE_V1,
	&ngx_http_sslmon_module_ctx,	/* module context */
	ngx_http_sslmon_commands,	/* module directives */
	NGX_HTTP_MODULE,		/* module type */
	NULL,				/* init master */
	NULL,				/* init module */
	NULL,				/* init process */
	NULL,				/* init thread */
	NULL,				/* exit thread */
	NULL,				/* exit process */
	NULL,				/* exit master */
	NGX_MODULE_V1_PADDING
};

#define SSLMON_STATISTIC_FILENAME "/var/log/nginx/sslmon.log"
static void *
ngx_http_sslmon_create_loc_conf(ngx_conf_t *cf)
{
	ngx_http_sslmon_loc_conf_t *conf;
	conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_sslmon_loc_conf_t));
	if( conf == NULL ) {
		return NULL;
	}
	conf->fd = NGX_CONF_UNSET;
	conf->counter = 0;
	return conf;
}

static char *
ngx_http_sslmon_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
	ngx_http_sslmon_loc_conf_t *prev = parent;
	ngx_http_sslmon_loc_conf_t *conf = child;

	if ( prev->fd == NGX_CONF_UNSET ) {
		conf->fd = open( SSLMON_STATISTIC_FILENAME, O_WRONLY | O_CREAT | O_TRUNC );
		if ( conf->fd == -1 ) {
			conf->fd = NGX_CONF_UNSET;
		}
	} else {
		conf->fd = prev->fd;
	}
	conf->counter = prev->counter;
	return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_sslmon_handler( ngx_http_request_t *r )
{
	ngx_http_sslmon_loc_conf_t * conf;
	conf = ngx_http_get_module_loc_conf( r, ngx_http_sslmon_module );
	conf->counter++;
	if( conf->fd != NGX_CONF_UNSET ) {
		off_t seek_rc;
		seek_rc = lseek( conf->fd, 0, SEEK_SET );
		dprintf( conf->fd, "%lu\n", conf->counter );
	} else {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
			"sslmon_handkler: fd not set");
	}
	return NGX_OK;
}

static ngx_int_t
ngx_http_sslmon_init( ngx_conf_t *cf )
{
	ngx_http_core_main_conf_t *cmcf;
	ngx_http_handler_pt *h;

	cmcf = ngx_http_conf_get_module_main_conf( cf, ngx_http_core_module );

	h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
	if ( h == NULL ) {
		return NGX_ERROR;
	}

	*h = ngx_http_sslmon_handler;

	return NGX_OK;
}
