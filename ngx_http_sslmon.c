/* Put a license-something here */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>

static ngx_int_t
ngx_http_sslmon_init( ngx_conf_t *cf );

static void *
ngx_http_sslmon_create_main_conf( ngx_conf_t * cf );

static char *
ngx_http_sslmon_merge_main_conf(ngx_conf_t *cf, void *conf);

static ngx_int_t
ngx_http_sslmon_init_process( ngx_cycle_t * cx );

static ngx_int_t
ngx_http_sslmon_exit_process( ngx_cycle_t * cx );

typedef struct {
	ngx_str_t filename;
	int fd;
	unsigned long counter;
} ngx_http_sslmon_main_conf_t;

typedef struct {
	unsigned long counter;
} ngx_http_sslmon_stats_t;

static ngx_command_t ngx_http_sslmon_commands[] = {

        { ngx_string("sslmon_output_file"),
          NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
          ngx_conf_set_str_slot,
          NGX_HTTP_LOC_CONF_OFFSET,
	  offsetof(ngx_http_sslmon_main_conf_t, filename),
	  NULL },

	ngx_null_command

};

static ngx_http_module_t  ngx_http_sslmon_module_ctx = {
	NULL,					/* preconfiguration */
	ngx_http_sslmon_init,			/* postconfiguration */

	ngx_http_sslmon_create_main_conf,	/* create main configuration */
	ngx_http_sslmon_merge_main_conf,	/* init main configuration */

	NULL,					/* create server configuration */
	NULL,					/* merge server configuration */

	NULL,					/* create location configration */
	NULL,					/* merge location configration */
};

ngx_module_t ngx_http_sslmon_module = {
	NGX_MODULE_V1,
	&ngx_http_sslmon_module_ctx,	/* module context */
	ngx_http_sslmon_commands,	/* module directives */
	NGX_HTTP_MODULE,		/* module type */
	NULL,				/* init master */
	NULL,				/* init module */
	ngx_http_sslmon_init_process,	/* init process */
	NULL,				/* init thread */
	NULL,				/* exit thread */
	NULL,				/* exit process */
	NULL,				/* exit master */
	NGX_MODULE_V1_PADDING
};

static void *
ngx_http_sslmon_create_main_conf(ngx_conf_t *cf)
{
	ngx_http_sslmon_main_conf_t *conf;
	conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_sslmon_main_conf_t));
	if( conf == NULL ) {
		ngx_log_error(NGX_LOG_ERR, cf->log, 0,
			"sslmon_create_main_conf: not able to allocate");
		return NULL;
	} else {
		ngx_log_error(NGX_LOG_DEBUG, cf->log, 0,
			"sslmon_create_main_conf: conf store at %p", conf);
	}
	conf->fd = NGX_CONF_UNSET;
	conf->counter = 0;
	return conf;
}

static char *
ngx_http_sslmon_merge_main_conf(ngx_conf_t *cf, void *conf)
{
	/* 
	ngx_log_error(NGX_LOG_NOTICE, cf->log, 0,
		"sslmon_merge_main_conf: starting");
	*/
	return NGX_CONF_OK;
}

#define SSLMON_FILENAME_BASE "/var/log/nginx/sslmon/sslmon"
#define SSLMON_STATISTIC_FILENAME "/var/log/nginx/sslmon/sslmon.log"
#define SSLMON_FILENAME_MAXSIZE 512
static ngx_int_t
ngx_http_sslmon_init_process( ngx_cycle_t * cx )
{
	ngx_http_sslmon_main_conf_t * conf =
		ngx_http_cycle_get_module_main_conf( cx, ngx_http_sslmon_module );
	if( conf == NULL ) {
		ngx_log_error(NGX_LOG_ERR, cx->log, 0,
			"sslmon_init_process: file not open (%d)", errno);
		return NGX_OK;
	}
	ngx_pid_t pid = ngx_getpid();
	ngx_str_t filename;
	filename.data = ngx_pcalloc(cx->pool, SSLMON_FILENAME_MAXSIZE);
	if( filename.data == NULL ) {
		ngx_log_error(NGX_LOG_ERR, cx->log, 0,
			"sslmon_init_process:: not able to allocate");
		return NGX_ERROR;
	}
	filename.len = 0;
	filename.len = ngx_snprintf( filename.data, SSLMON_FILENAME_MAXSIZE,
		"%s-%d.log", SSLMON_FILENAME_BASE,pid );
	ngx_log_error(NGX_LOG_NOTICE, cx->log, 0,
		"sslmon_init_process: sslmon log file %s", filename.data );
	conf->fd = ngx_open_file( filename.data, NGX_FILE_WRONLY,
		NGX_FILE_TRUNCATE | NGX_FILE_NONBLOCK,
		S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH );
	/*
	conf->fd = open( SSLMON_STATISTIC_FILENAME, O_WRONLY | O_CREAT | O_TRUNC );
	*/
	if ( conf->fd == -1 ) {
		ngx_log_error(NGX_LOG_ERR, cx->log, 0,
			"sslmon_init_process: file not open (%d)", errno);
		conf->fd = NGX_CONF_UNSET;
	}
	ngx_log_error(NGX_LOG_DEBUG, cx->log, 0,
		"sslmon_init_process: starting process %d - updating conf %p", pid, conf );
	return NGX_OK;
}

static ngx_int_t
ngx_http_sslmon_handler( ngx_http_request_t *r )
{
	ngx_http_sslmon_main_conf_t * conf;
	conf = ngx_http_get_module_main_conf( r, ngx_http_sslmon_module );
	conf->counter++;
	if( conf->fd != NGX_CONF_UNSET ) {
		off_t seek_rc;
		seek_rc = lseek( conf->fd, 0, SEEK_SET );
		dprintf( conf->fd, "%lu\n", conf->counter );
		ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
			"sslmon_handler: Updating counter %d from pid %d",
			conf->counter, ngx_getpid());
	} else {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
			"sslmon_handler: fd not set - conf %p", conf);
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
