/* Put a license-something here */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <nginx.h>

/* ************************ */
/* Types/structs definition */
/* ************************ */

typedef long unsigned ngx_cnt;	/* counter */

typedef struct {
	char * name;
	ngx_cnt counter;
} ngx_http_sslmon_ciphers_cnt_t;

typedef struct {
	ngx_cnt counter;		/* requests counter */
	ngx_cnt rt_sum;			/* req time sum */
	ngx_cnt ut_sum;			/* upstream time sum */
	ngx_cnt slow_requests;		/* requests slower than 200ms */
	ngx_cnt reused_sessions;	/* reused sessions */
	ngx_array_t* ciphers_cnt;	/* ciphers counter */
} ngx_http_sslmon_stats_t;

typedef struct {
	ngx_uint_t			update_period;
	ngx_uint_t			slow_request_time;
	int 				fd;
	ngx_http_sslmon_stats_t * 	stats;
	ngx_str_t			filename;
} ngx_http_sslmon_main_conf_t;

/* ****************************** */
/* Function prototype definitions */
/* ****************************** */

void
ngx_http_sslmon_increment_ssl_handshake_errors();

static ngx_int_t
ngx_http_sslmon_init( ngx_conf_t *cf );

static void *
ngx_http_sslmon_create_main_conf( ngx_conf_t * cf );

static char *
ngx_http_sslmon_merge_main_conf(ngx_conf_t *cf, void *conf);

static ngx_int_t
ngx_http_sslmon_init_process( ngx_cycle_t * cx );

static void
ngx_http_sslmon_exit_process( ngx_cycle_t * cx );

static void
ngx_http_sslmon_set_timer( ngx_http_sslmon_main_conf_t *c, ngx_log_t *l );

static void
ngx_http_sslmon_timer_handler( ngx_event_t *ev );

static void
ngx_http_sslmon_write_report( ngx_http_sslmon_main_conf_t *conf, ngx_log_t *l );

/* ********************** */
/* Global variables timer */
/* ********************** */

ngx_cnt ngx_http_sslmon_ssl_handshake_errors;	/* SSL handshake errors */
ngx_event_t ngx_http_sslmon_timer;		/* write report timer */

/* ************************ */
/* Nginx module definitions */
/* ************************ */

/* How often the statistic file has to be written */
/* default: every 300 seconds */
#define SSLMON_DEFAULT_UPDATE_PERIOD 300*1000

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
	ngx_http_sslmon_exit_process,	/* exit process */
	NULL,				/* exit master */
	NGX_MODULE_V1_PADDING
};

static void
ngx_http_sslmon_add_cipher(ngx_array_t * cnt_array, const char * cipher_name)
{
	ngx_http_sslmon_ciphers_cnt_t cc, * ccp;
	cc.name = (char *)cipher_name;
	cc.counter = 1;
	ccp = ngx_array_push(cnt_array);
	if( ccp == NULL ) {
		/* disaster */
		return;
	}
	*ccp = cc;
}

static int
ngx_http_sslmon_find_and_incr_cipher( ngx_array_t * cnt_array, const char * cipher_name)
{
	ngx_uint_t i = 0;
	ngx_http_sslmon_ciphers_cnt_t * ciphers = cnt_array->elts;
	size_t len = strlen(cipher_name);
	for( i=0; i < cnt_array->nelts ; i++) {
		size_t len2 = strlen( ciphers[i].name );
		if( strncmp( cipher_name, ciphers[i].name, ngx_max(len,len2) ) == 0 ) {
			ciphers[i].counter++;
			return 1; /* true */
		}
	}
	return 0; /* false */
}

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
	stats->ciphers_cnt =
		ngx_array_create( cf->pool, 15, sizeof( ngx_http_sslmon_ciphers_cnt_t ));
	return conf;
}

void
ngx_http_sslmon_increment_ssl_handshake_errors()
{
	ngx_http_sslmon_ssl_handshake_errors++;
}

static void
ngx_http_sslmon_reset_stats( ngx_http_sslmon_stats_t * s )
{
	/* resetting counters */
	s->counter = s->slow_requests = s->reused_sessions = 0;
	s->rt_sum = s->ut_sum = 0;
	ngx_array_init( s->ciphers_cnt, s->ciphers_cnt->pool, 25,
		sizeof( ngx_http_sslmon_ciphers_cnt_t ));
	ngx_http_sslmon_ssl_handshake_errors = 0;
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
	ngx_http_sslmon_timer.handler = ngx_http_sslmon_timer_handler;
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
	conf->filename.data = ngx_pcalloc(cx->pool, SSLMON_FILENAME_MAXSIZE);
	if( conf->filename.data == NULL ) {
		ngx_log_error(NGX_LOG_ERR, cx->log, 0,
			"sslmon_init_process:: not able to allocate");
		return NGX_ERROR;
	}
	conf->filename.len = 0;
	conf->filename.len = snprintf( (char *)conf->filename.data, SSLMON_FILENAME_MAXSIZE,
		"%s-%d.log", SSLMON_FILENAME_BASE,pid );
	ngx_log_error(NGX_LOG_NOTICE, cx->log, 0,
		"sslmon_init_process: sslmon log file %s", conf->filename.data );
/* opening the log file */
	conf->fd = ngx_open_file( conf->filename.data, NGX_FILE_WRONLY,
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

static void
ngx_http_sslmon_exit_process( ngx_cycle_t * cx )
{
	ngx_http_sslmon_main_conf_t * conf =
		ngx_http_cycle_get_module_main_conf( cx, ngx_http_sslmon_module );
	(void)close( conf->fd );
	(void)unlink( (char *)conf->filename.data );
}

static ngx_http_variable_value_t *
ngx_http_sslmon_getvar( ngx_http_request_t *r, const char * cstr )
{
	ngx_str_t var_name = ngx_string(cstr);
	var_name.len=strlen(cstr);
	ngx_uint_t key;
	ngx_http_variable_value_t * vv;
	key = ngx_hash_key( var_name.data, var_name.len );
	vv = ngx_http_get_variable( r, &var_name, key );
	return vv;
}

static ngx_int_t
ngx_http_sslmon_msec_getvar( ngx_http_request_t *r, const char * cstr )
{
	char tmpstr[10];
	bzero(tmpstr, 10);
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
	strncpy( tmpstr, (char *)vv->data, vv->len );
	sscanf( tmpstr, "%d.%d", &sec, &msec );
	msec += 1000*sec;
	ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
		"sslmon_handler: %s %d", cstr, msec );
	return msec;
}

static ngx_int_t
ngx_http_sslmon_handler( ngx_http_request_t *r )
{
	ngx_http_sslmon_main_conf_t * conf;
	ngx_http_sslmon_stats_t * stats;
	unsigned int rt = 0; /* response time */
	unsigned int new_rt = 0; /* response time */
	unsigned int ut = 0; /* upstream time */
	unsigned int stable_ut = 0; /* response time */
	unsigned int nrt = 0; /* nginx/net response time */
	unsigned long epoch = 0; /* request epoch */
	ngx_ssl_connection_t * ssl_connection;

	conf = ngx_http_get_module_main_conf( r, ngx_http_sslmon_module );
	stats = conf->stats;
	ssl_connection = r->connection->ssl;

	/* variable access is deprecated */
	/* rt = ngx_http_sslmon_msec_getvar( r, "request_time" ); */
	stable_ut = ngx_http_sslmon_msec_getvar( r, "upstream_response_time" );
	/* get the response time */
	rt = (ngx_cached_time->sec - r->start_sec ) * 1000 + ngx_cached_time->msec - r->start_msec;
	ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
		"sslmon_handler: response time %d ms", rt);

	/* get the upstream time */
	if ( r->upstream_states == NULL || r->upstream_states->nelts == 0 ) {
		ut = 0;
	} else {
		ngx_uint_t i=0;
		ngx_msec_int_t ms;
		ngx_http_upstream_state_t *state;
		state = r->upstream_states->elts;
		for( ;; ) {
			if( state[i].status ) {
				ms = state[i].response_time;
				if ( ms > 600000 ) {
					/* if the response time is bigger than 10 minutes */
					/* ms is holding an epoch in millisecond */
					 ngx_log_error(NGX_LOG_NOTICE, r->connection->log, 0,
						"sslmon_handler: state %d has huge ut", i);
					/* probably for a not yet completed request */
					ms = 0;
				}
			}
			ms = ngx_max(ms, 0);
			ut += ms;
			i++;
			if( i == r->upstream_states->nelts ) {
				break;
			}
			if (state[i].peer) {
				;
			} else {
				i++;
				if( i == r->upstream_states->nelts ) {
					break;
				}
				continue;
			}
		}
	}
	if ( ut != stable_ut ) {
		ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
			"sslmon_handler: upstream time %d ms, but ut = %d", stable_ut, ut);
	}
	ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
		"sslmon_handler: upstream time %d ms", ut);
	nrt = rt - stable_ut;
	if( rt > conf->slow_request_time ) {
		stats->slow_requests++;
	}
	stats->counter++;
	stats->rt_sum += rt;
	stats->ut_sum += stable_ut;

	/* direct access to ssl information, avoiding variable parsing */
	if (ssl_connection && ssl_connection->connection ) {
		/* resued session counter */
		if( SSL_session_reused(ssl_connection->connection)) {
			stats->reused_sessions++;
			ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
				"sslmon_handler: ssl_session reused");
		}
		/* ciphers statistic */
		const char * cipher_name = SSL_get_cipher_name(ssl_connection->connection);
		if ( ngx_http_sslmon_find_and_incr_cipher( stats->ciphers_cnt , cipher_name) ) {
			ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
				"sslmon_handler: incrementing the counter %s", cipher_name);
		} else {
			ngx_http_sslmon_add_cipher( stats->ciphers_cnt, cipher_name );
			ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
				"sslmon_handler: adding a counter for the cipher %s", cipher_name);
		}
	}
	return NGX_OK;
}

static void
ngx_http_sslmon_write_report( ngx_http_sslmon_main_conf_t *conf, ngx_log_t *l )
{
	ngx_http_sslmon_stats_t * stats;
	stats = conf->stats;
	if( conf->fd != NGX_CONF_UNSET ) {
		int rc;
		ngx_log_error(NGX_LOG_DEBUG, l, 0,
			"sslmon_write_report: Updating report (counting %d) of pid %d",
			stats->counter, ngx_getpid());
		rc = ftruncate( conf->fd, 0 );
		if( rc != 0 ) {
			ngx_log_error(NGX_LOG_WARN, l, 0,
				"sslmon_write_report: ftruncate didn't work - %d", errno );
		}
		lseek( conf->fd, 0, SEEK_SET );
		dprintf( conf->fd, "update_period=%lu\n", conf->update_period/1000);
		dprintf( conf->fd, "slow_request_time=%lu\n", conf->slow_request_time);
		dprintf( conf->fd, "epoch=%lu\n", ngx_time() );
		dprintf( conf->fd, "counter=%lu\n", stats->counter );
		dprintf( conf->fd, "slow_requests=%lu\n", stats->slow_requests );
		dprintf( conf->fd, "reused_sessions=%lu\n", stats->reused_sessions );
		dprintf( conf->fd, "ssl_errors=%lu\n", ngx_http_sslmon_ssl_handshake_errors );
		if( stats->counter != 0 ) {
			dprintf( conf->fd, "avg_rt=%lf\n",
				stats->rt_sum/(double)(stats->counter) );
			dprintf( conf->fd, "avg_ut=%lf\n",
				stats->ut_sum/(double)(stats->counter) );
			dprintf( conf->fd, "avg_net_rt=%lf\n",
				(stats->rt_sum-stats->ut_sum)/(double)(stats->counter) );
		} else {
			dprintf( conf->fd, "avg_rt=0.0\n");
			dprintf( conf->fd, "avg_ut=0.0\n");
			dprintf( conf->fd, "avg_net_rt=0.0\n");
		}
		ngx_uint_t i = 0;
		ngx_http_sslmon_ciphers_cnt_t * ciphers = stats->ciphers_cnt->elts;
		for( i=0; i<stats->ciphers_cnt->nelts; i++ ) {
			if( ciphers[i].counter != 0 ) {
				dprintf( conf->fd, "cipher.%s=%d\n",
					ciphers[i].name, ciphers[i].counter );
				ciphers[i].counter = 0;
			}
		}
	} else {
		ngx_log_error(NGX_LOG_ERR, l, 0,
			"sslmon_handler: fd not set - conf %p", conf);
	}
	/* reset all counters */
	ngx_http_sslmon_reset_stats( stats );
}

static void
ngx_http_sslmon_timer_handler( ngx_event_t *ev )
{
	ngx_http_sslmon_main_conf_t * conf;
	conf = ev->data;
	if ( ngx_exiting == 1 ) {
		ngx_log_error(NGX_LOG_NOTICE, ev->log, 0,
			"sslmon_timer_handler: quitting -> no timer anymore", conf);
	} else {
		ngx_http_sslmon_set_timer( conf, ev->log );
		ngx_log_error(NGX_LOG_DEBUG, ev->log, 0,
			"sslmon_timer_handler: re-setting timer", conf);
	}
	ngx_http_sslmon_write_report( conf, ev->log );
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
