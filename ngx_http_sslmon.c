/* Put a license-something here */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>

/* ************************ */
/* Types/structs definition */
/* ************************ */

typedef double ngx_avg;		/* avarage */
typedef long unsigned ngx_cnt;	/* counter */

typedef struct {
	ngx_cnt counter;		/* requests counter */
	ngx_cnt rt_sum;			/* req time sum */
	ngx_cnt ut_sum;			/* upstream time sum */
	ngx_cnt slow_requests;		/* requests slower than 200ms */
	ngx_cnt reused_sessions;	/* reused sessions */
} ngx_http_sslmon_stats_t;

typedef struct {
	ngx_uint_t	update_period;
	ngx_uint_t	slow_request_time;
	int fd;
	ngx_http_sslmon_stats_t * stats;
} ngx_http_sslmon_main_conf_t;

/* ****************************** */
/* Function prototype definitions */
/* ****************************** */
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

static void
ngx_http_sslmon_set_timer( ngx_http_sslmon_main_conf_t *c, ngx_log_t *l );

static void
ngx_http_sslmon_write_report( ngx_event_t *ev );

/* *********** */
/* Nginx timer */
/* *********** */

static ngx_event_t ngx_http_sslmon_timer;

/* ************************ */
/* Nginx module definitions */
/* ************************ */

/* How often the statistic file has to be written */
/* default: every 300 seconds */
#define SSLMON_DEFAULT_UPDATE_PERIOD 300

/* A reuqest is considered slow if it takes more than 200ms */
#define SSLMON_DEFAULT_SLOW_REQUEST_TIME 200
static ngx_command_t ngx_http_sslmon_commands[] = {

        { ngx_string("sslmon_update_period"),
          NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
          ngx_conf_set_msec_slot,
          NGX_HTTP_MAIN_CONF_OFFSET,
	  offsetof(ngx_http_sslmon_main_conf_t, update_period),
	  NULL },

        { ngx_string("sslmon_slow_request_time"),
          NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
          ngx_conf_set_num_slot,
          NGX_HTTP_MAIN_CONF_OFFSET,
	  offsetof(ngx_http_sslmon_main_conf_t, slow_request_time),
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
	conf->update_period = NGX_CONF_UNSET_UINT;
	conf->slow_request_time = NGX_CONF_UNSET_UINT;
	conf->fd = NGX_CONF_UNSET;

	ngx_http_sslmon_stats_t *stats;
	stats = ngx_pcalloc(cf->pool, sizeof(ngx_http_sslmon_stats_t));
	if( stats == NULL ) {
		ngx_log_error(NGX_LOG_ERR, cf->log, 0,
			"sslmon_create_main_conf: not able to allocate stats");
		return NULL;
	} else {
		ngx_log_error(NGX_LOG_DEBUG, cf->log, 0,
			"sslmon_create_main_conf: stats store at %p", conf);
	}
	conf->stats = stats;
	return conf;
}

static void
ngx_http_sslmon_reset_stats( ngx_http_sslmon_stats_t * s )
{
	/* resetting counters */
	s->counter = s->slow_requests = s->reused_sessions = 0;
	s->rt_sum = s->ut_sum = 0;
}


static char *
ngx_http_sslmon_merge_main_conf(ngx_conf_t *cf, void *c)
{
	ngx_http_sslmon_main_conf_t * conf = c;
	ngx_http_sslmon_stats_t * stats = conf->stats;
	if( conf->update_period == NGX_CONF_UNSET_UINT ) {
		conf->update_period = SSLMON_DEFAULT_UPDATE_PERIOD;
	}
	if( conf->slow_request_time == NGX_CONF_UNSET_UINT ) {
		conf->slow_request_time = SSLMON_DEFAULT_SLOW_REQUEST_TIME;
	}
	ngx_log_error(NGX_LOG_NOTICE, cf->log, 0,
		"sslmon_merge_main_conf: update_period %l s, slow_request %l ms",
		conf->update_period/1000, conf->slow_request_time);
	ngx_http_sslmon_reset_stats( stats );
	return NGX_CONF_OK;
}

static void
ngx_http_sslmon_set_timer( ngx_http_sslmon_main_conf_t *c, ngx_log_t *log )
{
	ngx_http_sslmon_timer.handler = ngx_http_sslmon_write_report;
	ngx_http_sslmon_timer.log = log;
	ngx_http_sslmon_timer.data = c;
	ngx_add_timer( &ngx_http_sslmon_timer, c->update_period );
}

#define SSLMON_FILENAME_BASE "/var/log/nginx/sslmon/sslmon"
#define SSLMON_FILENAME_MAXSIZE 512
static ngx_int_t
ngx_http_sslmon_init_process( ngx_cycle_t * cx )
{
	ngx_http_sslmon_main_conf_t * conf =
		ngx_http_cycle_get_module_main_conf( cx, ngx_http_sslmon_module );
	if( conf == NULL ) { /* paranoid check */
		ngx_log_error(NGX_LOG_ERR, cx->log, 0,
			"sslmon_init_process: not conf found");
		return NGX_OK;
	}

/* create the log filename appending the pid */
	ngx_pid_t pid = ngx_getpid();
	ngx_str_t filename;
	filename.data = ngx_pcalloc(cx->pool, SSLMON_FILENAME_MAXSIZE);
	if( filename.data == NULL ) {
		ngx_log_error(NGX_LOG_ERR, cx->log, 0,
			"sslmon_init_process:: not able to allocate");
		return NGX_ERROR;
	}
	filename.len = 0;
	filename.len = snprintf( (char *)filename.data, SSLMON_FILENAME_MAXSIZE,
		"%s-%d.log", SSLMON_FILENAME_BASE,pid );
	ngx_log_error(NGX_LOG_NOTICE, cx->log, 0,
		"sslmon_init_process: sslmon log file %s", filename.data );
/* opening the log file */
	conf->fd = ngx_open_file( filename.data, NGX_FILE_WRONLY,
		NGX_FILE_TRUNCATE | NGX_FILE_NONBLOCK,
		S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH );

	if ( conf->fd == -1 ) {
		ngx_log_error(NGX_LOG_ERR, cx->log, 0,
			"sslmon_init_process: file not open (%d)", errno);
		conf->fd = NGX_CONF_UNSET;
		return NGX_ERROR;
	}
/* preparing the timer */
	ngx_http_sslmon_set_timer( conf, cx->log );
	ngx_log_error(NGX_LOG_DEBUG, cx->log, 0,
		"sslmon_init_process: starting process %d - updating conf %p", pid, conf );
	return NGX_OK;
}

static ngx_http_variable_value_t *
ngx_http_sslmon_getvar( ngx_http_request_t *r, const char * cstr )
{
	ngx_str_t var_name = ngx_string(cstr);
	ngx_uint_t key;
	ngx_http_variable_value_t * vv;
	key = ngx_hash_key( var_name.data, var_name.len );
	vv = ngx_http_get_variable( r, &var_name, key );
	return vv;
}

static ngx_int_t
ngx_http_sslmon_msec_getvar( ngx_http_request_t *r, const char * cstr )
{
	ngx_http_variable_value_t * vv;
	vv = ngx_http_sslmon_getvar( r, cstr );
	if( vv == NULL ) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
			"sslmon_handler: var %s is null", cstr);
		return 0;
	}
	if( vv->not_found ) {
		ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
			"sslmon_handler: no %s found", cstr);
		return 0;
	}
	int sec, msec;
	sec = msec = 0;
	sscanf( (char *)vv->data, "%d.%d", &sec, &msec );
	msec += 1000*sec;
	ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
		"sslmon_handler: %s %d", cstr, msec );
	return msec;
}

static ngx_int_t
ngx_http_sslmon_handler( ngx_http_request_t *r )
{
	ngx_http_sslmon_main_conf_t * conf;
	ngx_http_sslmon_stats_t * stats;
	unsigned int rt = 0; /* response time */
	unsigned int ut = NGX_CONF_UNSET; /* upstream time */
	unsigned int nrt = 0; /* nginx/net response time */
	unsigned long epoch = 0; /* request epoch */

	conf = ngx_http_get_module_main_conf( r, ngx_http_sslmon_module );
	stats = conf->stats;

	rt = ngx_http_sslmon_msec_getvar( r, "request_time" );
	ut = ngx_http_sslmon_msec_getvar( r, "upstream_request_time" );
	nrt = rt - ut;
	if( rt > conf->slow_request_time ) {
		stats->slow_requests++;
	}
	stats->counter++;
	stats->rt_sum += rt;
	stats->ut_sum += ut;

	ngx_http_variable_value_t * vv;
	vv = ngx_http_sslmon_getvar( r, "ssl_session_reused" );
	if( vv == NULL ) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
			"sslmon_handler: var ssl_session_reused is null", conf);
	} else {
		if ( vv->not_found ) {
			ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
				"sslmon_handler: no ssl_session not reused");
		} else {
			stats->reused_sessions++;
			ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
				"sslmon_handler: ssl_session reused");
		}
	}
	return NGX_OK;
}

static void
ngx_http_sslmon_write_report( ngx_event_t *ev )
{
	ngx_http_sslmon_main_conf_t * conf;
	ngx_http_sslmon_stats_t * stats;

	conf = ev->data;
	stats = conf->stats;
	ngx_http_sslmon_set_timer( conf, ev->log );
	if( conf->fd != NGX_CONF_UNSET ) {
		ngx_log_error(NGX_LOG_NOTICE, ev->log, 0,
			"sslmon_write_report: Updating report (counting %d) of pid %d",
			stats->counter, ngx_getpid());
		off_t seek_rc;
		seek_rc = lseek( conf->fd, 0, SEEK_SET );
		dprintf( conf->fd, "epoch=%lu\n", ngx_time() );
		dprintf( conf->fd, "counter=%lu\n", stats->counter );
		dprintf( conf->fd, "slow_requests=%lu\n", stats->slow_requests );
		dprintf( conf->fd, "reused_sessions=%lu\n", stats->reused_sessions );
		dprintf( conf->fd, "avg_rt=%lf\n",
			stats->rt_sum/(double)(stats->counter) );
		dprintf( conf->fd, "avg_ut=%lf\n",
			stats->ut_sum/(double)(stats->counter) );
		dprintf( conf->fd, "avg_net_rt=%lf\n",
			 (stats->rt_sum-stats->ut_sum)/(double)(stats->counter) );
	} else {
		ngx_log_error(NGX_LOG_ERR, ev->log, 0,
			"sslmon_handler: fd not set - conf %p", conf);
	}
	/* reset all counters */
	ngx_http_sslmon_reset_stats( stats );
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
