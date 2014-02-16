#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <zmq.h>
#include <assert.h>
#include <inttypes.h>

typedef struct {
  ngx_flag_t enabled;
  ngx_str_t server;
  ngx_str_t domain;
  ngx_http_complex_value_t identifier;
} ngx_http_smockron_conf_t;

static void *ngx_http_smockron_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_smockron_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static char *ngx_http_smockron_identifier(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_smockron_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_smockron_initproc(ngx_cycle_t *cycle);
void ngx_http_smockron_control_read(ngx_event_t *ev);

static void *zmq_context;
static void *accounting_socket;
static void *control_socket;
static ngx_connection_t *control_connection;

static ngx_command_t ngx_http_smockron_commands[] = {
  {
    ngx_string("smockron"),
    NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_flag_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_smockron_conf_t, enabled),
    NULL
  },
  {
    ngx_string("smockron_server"),
    NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_smockron_conf_t, server),
    NULL
  },
  {
    ngx_string("smockron_domain"),
    NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_smockron_conf_t, domain),
    NULL
  },
  {
    ngx_string("smockron_identifier"),
    NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_http_smockron_identifier,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_smockron_conf_t, identifier),
    NULL
  },
  ngx_null_command
};

static ngx_http_module_t ngx_http_smockron_module_ctx = {
  NULL,                          /* preconfiguration */
  ngx_http_smockron_init,        /* postconfiguration */

  NULL,                          /* create main configuration */
  NULL,                          /* init main configuration */

  NULL,                          /* create server configuration */
  NULL,                          /* merge server configuration */

  ngx_http_smockron_create_loc_conf, /* create location configuration */
  ngx_http_smockron_merge_loc_conf   /* merge location configuration */
};

ngx_module_t ngx_http_smockron_module = {
  NGX_MODULE_V1,
  &ngx_http_smockron_module_ctx,
  ngx_http_smockron_commands,
  NGX_HTTP_MODULE,
  NULL,                          /* init master */
  NULL,                          /* init module */
  ngx_http_smockron_initproc,    /* init process */
  NULL,                          /* init thread */
  NULL,                          /* exit thread */
  NULL,                          /* exit process */
  NULL,                          /* exit master */
  NGX_MODULE_V1_PADDING
};

static void *ngx_http_smockron_create_loc_conf(ngx_conf_t *cf) {
  ngx_http_smockron_conf_t *conf;
  conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_smockron_conf_t));
  if (conf == NULL) {
    return NGX_CONF_ERROR;
  }
  conf->enabled = NGX_CONF_UNSET;

  ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "create_loc_conf");

  return conf;
}

static char *ngx_http_smockron_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child) {
  ngx_http_smockron_conf_t *prev = parent;
  ngx_http_smockron_conf_t *conf = child;

  ngx_conf_merge_value(conf->enabled, prev->enabled, 0);
  ngx_conf_merge_str_value(conf->server, prev->server, "tcp://localhost:10004");
  ngx_conf_merge_str_value(conf->domain, prev->domain, "default");
  if (conf->identifier.value.data == NULL) {
    if (prev->identifier.value.data == NULL) {
      ngx_str_t value = ngx_string("$remote_addr");
      ngx_http_compile_complex_value_t ccv;
      ccv.cf = cf;
      ccv.value = &value;
      ccv.complex_value = &conf->identifier;
      if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
      }
    } else {
      conf->identifier = prev->identifier;
    }
  }

  ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "merge_loc_conf");

  return NGX_CONF_OK;
}

static char *ngx_http_smockron_identifier(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
  ngx_http_smockron_conf_t *smcf = conf;

  ngx_str_t *value;
  ngx_http_compile_complex_value_t ccv;

  value = cf->args->elts;

  if (smcf->identifier.value.data) {
    return "is duplicate";
  }

  ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

  ccv.cf = cf;
  ccv.value = &value[1];
  ccv.complex_value = &smcf->identifier;

  if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
    return NGX_CONF_ERROR;
  }

  return NGX_CONF_OK;
}

static inline uint64_t get_request_time(ngx_http_request_t *r) {
  return r->start_sec * 1000 + r->start_msec;
}

static inline uint64_t get_ident_next_allowed_request(ngx_str_t ident) {
  return 0;
}

static ngx_int_t ngx_http_smockron_handler(ngx_http_request_t *r) {
  ngx_http_smockron_conf_t *smockron_config;
  ngx_str_t ident;

  if (r->internal || ngx_http_get_module_ctx(r->main, ngx_http_smockron_module) != NULL)
    return NGX_DECLINED;

  smockron_config = ngx_http_get_module_loc_conf(r, ngx_http_smockron_module);

  if (!smockron_config->enabled)
    return NGX_DECLINED;

  ngx_http_set_ctx(r->main, (void *)1, ngx_http_smockron_module);

  if (ngx_http_complex_value(r, &smockron_config->identifier, &ident) != NGX_OK) {
    return NGX_ERROR;
  }

  ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0,
      "Var \"%*s\"=\"%*s\"", smockron_config->identifier.value.len, smockron_config->identifier.value.data, ident.len, ident.data);

  char receive_time[32], delay_time[32];
  int receive_time_len, delay_time_len = 0;
  uint64_t request_time = get_request_time(r);
  uint64_t next_allowed_time = get_ident_next_allowed_request(ident);
  ngx_str_t _ACCEPTED = ngx_string("ACCEPTED"),
            _DELAYED  = ngx_string("DELAYED"),
            _REJECTED = ngx_string("REJECTED");
  ngx_str_t *status;
  ngx_int_t rc;

  receive_time_len = snprintf(receive_time, 32, "%" PRId64, request_time);

  if (request_time >= next_allowed_time) {
    status = &_ACCEPTED;
    rc = NGX_DECLINED;
  } else if (request_time >= next_allowed_time - 5000) {
    status = &_DELAYED;
    delay_time_len = snprintf(delay_time, 32, "%" PRId64, next_allowed_time);
    rc = NGX_DECLINED;
  } else {
    status = &_REJECTED;
    rc = NGX_DECLINED;
  }
  
  zmq_send(accounting_socket, smockron_config->domain.data, smockron_config->domain.len + 1, ZMQ_SNDMORE);
  zmq_send(accounting_socket, status->data, status->len, ZMQ_SNDMORE);
  zmq_send(accounting_socket, ident.data, ident.len, ZMQ_SNDMORE);
  zmq_send(accounting_socket, receive_time, receive_time_len, ZMQ_SNDMORE);
  zmq_send(accounting_socket, delay_time, delay_time_len, ZMQ_SNDMORE);
  zmq_send(accounting_socket, "", 0, 0);

  return rc;
}

static ngx_int_t ngx_http_smockron_init(ngx_conf_t *cf) {
  ngx_http_handler_pt *h;
  ngx_http_core_main_conf_t *cmcf;

  cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
  h = ngx_array_push(&cmcf->phases[NGX_HTTP_PREACCESS_PHASE].handlers);
  if (h == NULL) {
    return NGX_ERROR;
  }

  *h = ngx_http_smockron_handler;

  ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "smockron_init");

  return NGX_OK;
}

static ngx_int_t ngx_http_smockron_initproc(ngx_cycle_t *cycle) {
  zmq_context = zmq_ctx_new();
  accounting_socket = zmq_socket(zmq_context, ZMQ_PUB);
  int controlfd;
  size_t fdsize;

  if (zmq_connect(accounting_socket, "tcp://localhost:10004") != 0) {
    ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "Failed to connect accounting socket");
    return NGX_ERROR;
  }
  control_socket = zmq_socket(zmq_context, ZMQ_SUB);
  if (zmq_connect(control_socket, "tcp://localhost:10005") != 0) {
    ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "Failed to connect control socket");
    return NGX_ERROR;
  }
  zmq_setsockopt(control_socket, ZMQ_SUBSCRIBE, "", 0);
  fdsize = sizeof(int);
  zmq_getsockopt(control_socket, ZMQ_FD, &controlfd, &fdsize);
  control_connection = ngx_get_connection(controlfd, cycle->log);
  control_connection->read->handler = ngx_http_smockron_control_read;
  control_connection->read->log = cycle->log;
  ngx_add_event(control_connection->read, NGX_READ_EVENT, 0);

  ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "initproc");
  return NGX_OK;
}

void ngx_http_smockron_control_read(ngx_event_t *ev) {
  int events;
  size_t events_size = sizeof(events);

  zmq_getsockopt(control_socket, ZMQ_EVENTS, &events, &events_size);

  while (events & ZMQ_POLLIN) {
    int more;
    size_t more_size = sizeof(more);
    char buf[256];

    do {
      bzero(buf, sizeof(buf));
      int rc = zmq_recv(control_socket, buf, sizeof(buf), 0);
      assert(rc != -1);
      rc = zmq_getsockopt(control_socket, ZMQ_RCVMORE, &more, &more_size);
      assert(rc == 0);
      fprintf(stderr, "msg: %*s, more: %d\n", rc, buf, more);
    } while (more);
    events = 0;
    zmq_getsockopt(control_socket, ZMQ_EVENTS, &events, &events_size);
  }
}
