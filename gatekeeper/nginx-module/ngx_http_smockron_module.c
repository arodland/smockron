#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <zmq.h>
#include <assert.h>
#include <inttypes.h>
#include <setjmp.h>
#include "uthash/src/uthash.h"

#define DELAY_KEY_LEN 256

typedef struct {
  ngx_flag_t enabled;
  ngx_str_t master;
  ngx_int_t master_idx;
  ngx_str_t domain;
  ngx_http_complex_value_t identifier;
  ngx_http_complex_value_t log_info;
  ngx_msec_t max_delay;
  ngx_int_t status_code;
} ngx_http_smockron_conf_t;

typedef struct {
  size_t shm_size;
} ngx_http_smockron_main_conf_t;

typedef struct {
  ngx_str_t accounting_server;
  void *accounting_socket;
  ngx_str_t control_server;
  void *control_socket;
  ngx_array_t *domains;
} ngx_http_smockron_master_t;

typedef struct {
  char key[DELAY_KEY_LEN];
  uint64_t next_allowed;
  UT_hash_handle hh;
} ngx_http_smockron_delay_t;

static void *ngx_http_smockron_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_smockron_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static char *ngx_http_smockron_set_cv(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void *ngx_http_smockron_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_smockron_init_main_conf(ngx_conf_t *cf, void *conf);
static ngx_int_t ngx_http_smockron_preinit(ngx_conf_t *cf);
static ngx_int_t ngx_http_smockron_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_smockron_initproc(ngx_cycle_t *cycle);
static ngx_int_t ngx_http_smockron_shm_init(ngx_shm_zone_t *zone, void *data);
static void ngx_http_smockron_delay(ngx_http_request_t *r);
static void ngx_http_smockron_control_read(ngx_event_t *ev);
static void ngx_http_smockron_hash_cleanup_handler(ngx_event_t *ev);

static void *zmq_context;

static ngx_pool_t *ngx_http_smockron_master_pool;
static ngx_array_t *ngx_http_smockron_master_array;

static ngx_shm_zone_t *ngx_http_smockron_delay_zone;
static ngx_http_smockron_delay_t **ngx_http_smockron_delay_hash = NULL;

static ngx_event_t hash_cleanup_event;

#define ngx_http_smockron_delay_pool ((ngx_slab_pool_t *)ngx_http_smockron_delay_zone->shm.addr)

#undef uthash_malloc
#undef uthash_free
#undef uthash_fatal
#define uthash_malloc(sz) ngx_slab_alloc_locked(ngx_http_smockron_delay_pool, sz)
#define uthash_free(ptr,sz) ngx_slab_free_locked(ngx_http_smockron_delay_pool, ptr)
#define uthash_fatal(msg) longjmp(bailout, 1);

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
    ngx_string("smockron_master"),
    NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_smockron_conf_t, master),
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
    ngx_http_smockron_set_cv,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_smockron_conf_t, identifier),
    NULL
  },
  {
    ngx_string("smockron_log_info"),
    NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_http_smockron_set_cv,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_smockron_conf_t, log_info),
    NULL
  },
  {
    ngx_string("smockron_max_delay"),
    NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_msec_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_smockron_conf_t, max_delay),
    NULL
  },
  {
    ngx_string("smockron_status_code"),
    NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_num_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_smockron_conf_t, status_code),
    NULL
  },
  {
    ngx_string("smockron_shm_size"),
    NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_size_slot,
    NGX_HTTP_MAIN_CONF_OFFSET,
    offsetof(ngx_http_smockron_main_conf_t, shm_size),
    NULL
  },
  ngx_null_command
};

static ngx_http_module_t ngx_http_smockron_module_ctx = {
  ngx_http_smockron_preinit,     /* preconfiguration */
  ngx_http_smockron_init,        /* postconfiguration */

  ngx_http_smockron_create_main_conf, /* create main configuration */
  ngx_http_smockron_init_main_conf,   /* init main configuration */

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
  conf->max_delay = NGX_CONF_UNSET_MSEC;
  conf->status_code = NGX_CONF_UNSET;
  conf->master_idx = NGX_CONF_UNSET;

  return conf;
}

static ngx_int_t ngx_http_smockron_master_set_control_server(ngx_http_smockron_master_t *master) {
  char *port;
  int portnum;

  master->control_server.data = ngx_pcalloc(ngx_http_smockron_master_pool, master->accounting_server.len + 2);
  if (master->control_server.data == NULL) {
    return NGX_ERROR;
  }

  ngx_memcpy(master->control_server.data, master->accounting_server.data, master->accounting_server.len);

  port = strrchr((const char *)master->control_server.data, ':');
  if (port == NULL) {
    return NGX_ERROR;
  }
  port ++;
  master->control_server.len = port - (char *)master->control_server.data;

  portnum = atoi(port);
  portnum ++;

  master->control_server.len += sprintf(port, "%d", portnum);

  return NGX_OK;
}

static char *ngx_http_smockron_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child) {
  ngx_http_smockron_conf_t *prev = parent;
  ngx_http_smockron_conf_t *conf = child;
  ngx_str_t *domain;
  unsigned int i;
  int domain_found = 0;

  ngx_conf_merge_value(conf->enabled, prev->enabled, 0);
  ngx_conf_merge_str_value(conf->master, prev->master, "tcp://localhost:10004");

  ngx_http_smockron_master_t *master = ngx_http_smockron_master_array->elts;
  for (i = 0 ; i < ngx_http_smockron_master_array->nelts ; i++) {
    if (ngx_strcmp(master[i].accounting_server.data, conf->master.data) == 0) {
      conf->master_idx = i;
      master = &master[i];
      break;
    }
  }
  if (conf->master_idx == NGX_CONF_UNSET) {
    master = ngx_array_push(ngx_http_smockron_master_array);
    if (master == NULL) {
      return NGX_CONF_ERROR;
    }
    master->accounting_server = conf->master;
    master->domains = ngx_array_create(ngx_http_smockron_master_pool, 1, sizeof(ngx_str_t));
    if (master->domains == NULL) {
      return NGX_CONF_ERROR;
    }
    if (ngx_http_smockron_master_set_control_server(master) != NGX_OK) {
      return NGX_CONF_ERROR;
    }
    conf->master_idx = ngx_http_smockron_master_array->nelts - 1;
  }

  ngx_conf_merge_str_value(conf->domain, prev->domain, "default");

  domain = master->domains->elts;
  for (i = 0 ; i < master->domains->nelts ; i++) {
    if (ngx_strcmp(domain->data, conf->domain.data) == 0) {
      domain_found = 1;
      break;
    }
  }

  if (!domain_found) {
    domain = ngx_array_push(master->domains);
    if (domain == NULL) {
      return NGX_CONF_ERROR;
    }
    *domain = conf->domain;
  }

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
  if (conf->log_info.value.data == NULL) {
    conf->log_info = prev->log_info; /* default NULL */
  }
  ngx_conf_merge_msec_value(conf->max_delay, prev->max_delay, 5000);
  ngx_conf_merge_value(conf->status_code, prev->status_code, 503);

  return NGX_CONF_OK;
}

static char *ngx_http_smockron_set_cv(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
  ngx_http_complex_value_t *field = (ngx_http_complex_value_t *)((char *)conf + cmd->offset);

  ngx_str_t *value;
  ngx_http_compile_complex_value_t ccv;

  value = cf->args->elts;

  if (field->value.data) {
    return "is duplicate";
  }

  ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

  ccv.cf = cf;
  ccv.value = &value[1];
  ccv.complex_value = field;

  if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
    return NGX_CONF_ERROR;
  }

  return NGX_CONF_OK;
}

static void *ngx_http_smockron_create_main_conf(ngx_conf_t *cf) {
  ngx_http_smockron_main_conf_t *conf;
  conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_smockron_main_conf_t));
  if (conf == NULL) {
    return NGX_CONF_ERROR;
  }

  conf->shm_size = NGX_CONF_UNSET_SIZE;
  return conf;
}

static char *ngx_http_smockron_init_main_conf(ngx_conf_t *cf, void *conf) {
  ngx_http_smockron_main_conf_t *smcf = conf;

  if (smcf->shm_size == NGX_CONF_UNSET_SIZE) {
    smcf->shm_size = 4 * 1024 * 1024;
  }
  return NGX_CONF_OK;
}

static inline uint64_t get_request_time(ngx_http_request_t *r) {
  return r->start_sec * 1000 + r->start_msec;
}

static inline uint64_t get_ident_next_allowed_request(ngx_str_t domain, ngx_str_t ident, ngx_log_t *log) {
  char key[DELAY_KEY_LEN];
  uint64_t ret;

  if (domain.len + ident.len + 1 > DELAY_KEY_LEN) {
    ngx_log_error(NGX_LOG_ERR, log, 0, "domain len %d + ident len %d > key size %d",
        domain.len + 1, ident.len, DELAY_KEY_LEN);
    return 0;
  }
      
  strncpy(key, (char *)domain.data, domain.len);
  key[domain.len] = ';';
  strncpy(key + domain.len + 1, (char *)ident.data, ident.len);
  key[domain.len + ident.len + 1] = '\0';

  ngx_http_smockron_delay_t *delay = NULL;
  ngx_shmtx_lock(&ngx_http_smockron_delay_pool->mutex);
  HASH_FIND_STR(*ngx_http_smockron_delay_hash, key, delay);
  ret = delay ? delay->next_allowed : 0;
  ngx_shmtx_unlock(&ngx_http_smockron_delay_pool->mutex);
  return ret;
}

static ngx_int_t ngx_http_smockron_handler(ngx_http_request_t *r) {
  ngx_http_smockron_conf_t *smockron_config;
  ngx_str_t ident;
  ngx_str_t log_info;

  if (r->internal || ngx_http_get_module_ctx(r->main, ngx_http_smockron_module) != NULL)
    return NGX_DECLINED;

  smockron_config = ngx_http_get_module_loc_conf(r, ngx_http_smockron_module);

  if (!smockron_config->enabled)
    return NGX_DECLINED;

  ngx_http_set_ctx(r->main, (void *)1, ngx_http_smockron_module);

  if (ngx_http_complex_value(r, &smockron_config->identifier, &ident) != NGX_OK) {
    return NGX_ERROR;
  }

  if (ngx_http_complex_value(r, &smockron_config->log_info, &log_info) != NGX_OK) {
    return NGX_ERROR;
  }

  char receive_time[32], delay_time[32];
  int receive_time_len, delay_time_len = 0;
  uint64_t request_time = get_request_time(r);
  uint64_t next_allowed_time = get_ident_next_allowed_request(smockron_config->domain, ident, r->connection->log);
  ngx_http_smockron_master_t *master = ngx_http_smockron_master_array->elts;
  master += smockron_config->master_idx;

  ngx_str_t status;
  ngx_int_t rc;

  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
      "smockron ident \"%*s\"=\"%*s\" rcv TS %l allowed TS %l",
      smockron_config->identifier.value.len, smockron_config->identifier.value.data, ident.len, ident.data,
      request_time, next_allowed_time);

  receive_time_len = snprintf(receive_time, 32, "%" PRId64, request_time);

  if (request_time >= next_allowed_time) {
    ngx_str_set(&status, "ACCEPTED");
    rc = NGX_DECLINED;
  } else if (request_time >= next_allowed_time - smockron_config->max_delay) {
    ngx_str_set(&status, "DELAYED");
    delay_time_len = snprintf(delay_time, 32, "%" PRId64, next_allowed_time);
    if (ngx_handle_read_event(r->connection->read, 0) != NGX_OK) {
      rc = smockron_config->status_code;
    } else {
      rc = NGX_AGAIN;
      r->read_event_handler = ngx_http_test_reading;
      r->write_event_handler = ngx_http_smockron_delay;
      ngx_add_timer(r->connection->write, next_allowed_time - request_time);
    }
  } else {
    ngx_str_set(&status, "REJECTED");
    rc = NGX_HTTP_SERVICE_UNAVAILABLE;
  }

  zmq_send(master->accounting_socket, smockron_config->domain.data, smockron_config->domain.len + 1, ZMQ_SNDMORE);
  zmq_send(master->accounting_socket, status.data, status.len, ZMQ_SNDMORE);
  zmq_send(master->accounting_socket, ident.data, ident.len, ZMQ_SNDMORE);
  zmq_send(master->accounting_socket, receive_time, receive_time_len, ZMQ_SNDMORE);
  zmq_send(master->accounting_socket, delay_time, delay_time_len, ZMQ_SNDMORE);
  zmq_send(master->accounting_socket, log_info.data, log_info.len, 0);

  return rc;
}

/* Lifted from ngx_http_limit_req module */
static void ngx_http_smockron_delay(ngx_http_request_t *r) {
  ngx_event_t *wev = r->connection->write;

  if (!wev->timedout) {
    if (ngx_handle_write_event(wev, 0) != NGX_OK) {
      ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    }
    return;
  }

  wev->timedout = 0;

  if (ngx_handle_read_event(r->connection->read, 0) != NGX_OK) {
    ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    return;
  }

  r->read_event_handler = ngx_http_block_reading;
  r->write_event_handler = ngx_http_core_run_phases;

  ngx_http_core_run_phases(r);
}

static ngx_int_t ngx_http_smockron_init(ngx_conf_t *cf) {
  ngx_http_handler_pt *h;
  ngx_http_core_main_conf_t *cmcf;
  ngx_http_smockron_main_conf_t *smcf;
  ngx_str_t shm_key = ngx_string("smockron_delay");

  cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
  h = ngx_array_push(&cmcf->phases[NGX_HTTP_PREACCESS_PHASE].handlers);
  if (h == NULL) {
    return NGX_ERROR;
  }

  *h = ngx_http_smockron_handler;

  smcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_smockron_module);

  ngx_http_smockron_delay_zone = ngx_shared_memory_add(cf, &shm_key, smcf->shm_size, &ngx_http_smockron_module);

  ngx_http_smockron_delay_zone->init = ngx_http_smockron_shm_init;

  return NGX_OK;
}

static ngx_int_t ngx_http_smockron_shm_init(ngx_shm_zone_t *zone, void *data) {

  if (data) {
    zone->data = data;
    return NGX_OK;
  }

  ngx_http_smockron_delay_hash = ngx_slab_alloc(ngx_http_smockron_delay_pool, sizeof(ngx_http_smockron_delay_hash));
  if (ngx_http_smockron_delay_hash == NULL) {
    return NGX_ERROR;
  }
  *ngx_http_smockron_delay_hash = NULL;

  zone->data = (void *)1;

  return NGX_OK;
}

static ngx_int_t ngx_http_smockron_preinit(ngx_conf_t *cf) {
  ngx_http_smockron_master_pool = ngx_create_pool(NGX_DEFAULT_POOL_SIZE, cf->log);
  if (ngx_http_smockron_master_pool == NULL) {
    return NGX_ERROR;
  }

  ngx_http_smockron_master_array = ngx_array_create(ngx_http_smockron_master_pool, 1, sizeof(ngx_http_smockron_master_t));
  if (ngx_http_smockron_master_array == NULL) {
    return NGX_ERROR;
  }

  return NGX_OK;
}

static ngx_int_t ngx_http_smockron_initproc(ngx_cycle_t *cycle) {
  zmq_context = zmq_ctx_new();
  int controlfd;
  size_t fdsize = sizeof(int);

  ngx_http_smockron_master_t *master = ngx_http_smockron_master_array->elts;
  unsigned int i,j;
  ngx_str_t *domain;

  for (i = 0 ; i < ngx_http_smockron_master_array->nelts ; i++) {
    master[i].accounting_socket = zmq_socket(zmq_context, ZMQ_PUB);
    if (zmq_connect(master[i].accounting_socket, (const char *)master[i].accounting_server.data) != 0) {
      ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "Failed to connect accounting socket %*s: %s",
          master[i].accounting_server.len, master[i].accounting_server.data, strerror(errno));
      return NGX_ERROR;
    }

    if (ngx_process_slot == 0) {
      master[i].control_socket = zmq_socket(zmq_context, ZMQ_SUB);
      if (zmq_connect(master[i].control_socket, (const char *)master[i].control_server.data) != 0) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "Failed to connect control socket %*s: %s",
            master[i].control_server.len, master[i].control_server.data, strerror(errno));
        return NGX_ERROR;
      }

      domain = master[i].domains->elts;
      for (j = 0 ; j < master[i].domains->nelts ; j++) {
        zmq_setsockopt(master[i].control_socket, ZMQ_SUBSCRIBE, domain[j].data, domain[j].len);
      }

      zmq_getsockopt(master[i].control_socket, ZMQ_FD, &controlfd, &fdsize);
      ngx_connection_t *control_connection = ngx_get_connection(controlfd, cycle->log);
      control_connection->read->handler = ngx_http_smockron_control_read;
      control_connection->read->log = cycle->log;
      control_connection->data = master[i].control_socket;
      ngx_add_event(control_connection->read, NGX_READ_EVENT, 0);
    }
  }

  if (ngx_process_slot == 0) {
    hash_cleanup_event.handler = ngx_http_smockron_hash_cleanup_handler;
    hash_cleanup_event.log = cycle->log;
    ngx_add_timer(&hash_cleanup_event, 1000);
  }

  return NGX_OK;
}

static void ngx_http_smockron_control_read(ngx_event_t *ev) {
  int events;
  size_t events_size = sizeof(events);
  void *control_socket = ((ngx_connection_t *)ev->data)->data;

  zmq_getsockopt(control_socket, ZMQ_EVENTS, &events, &events_size);

  while (events & ZMQ_POLLIN) {
    int more;
    size_t more_size = sizeof(more);
    char msg[4][256];
    int i = 0;

    do {
      if (i > 4) {
        ngx_log_error(NGX_LOG_ERR, ev->log, 0, "Control message has unexpected parts, ignoring");
        break;
      }

      bzero(msg[i], sizeof(msg[i]));
      int rc = zmq_recv(control_socket, msg[i], sizeof(msg[i]), 0);
      if (rc == -1) {
        ngx_log_error(NGX_LOG_ERR, ev->log, 0, "%s receiving control message, dropping", strerror(errno));
        goto out;
      }
      if ((unsigned)rc > sizeof(msg[i])) {
        ngx_log_error(NGX_LOG_ERR, ev->log, 0, "Control part length %d > %d, dropping", rc, sizeof(msg[i]));
        goto out;
      }

      rc = zmq_getsockopt(control_socket, ZMQ_RCVMORE, &more, &more_size);
      assert(rc == 0);

      i++;
    } while (more);

    if (ngx_strcmp(msg[1], "DELAY_UNTIL") == 0) {
      size_t domain_len = strlen(msg[0]) + 1 /* separator */,
             ident_len = strlen(msg[2]);
      char key[DELAY_KEY_LEN];
      uint64_t ts = atol(msg[3]);
      int delay_hash_was_null;

      ngx_http_smockron_delay_t *delay;

      if (domain_len + ident_len > DELAY_KEY_LEN) {
        ngx_log_error(NGX_LOG_ERR, ev->log, 0, "domain len %d + ident len %d > key size %d",
            domain_len, ident_len, DELAY_KEY_LEN);
        goto out;
      }
      
      strcpy(key, msg[0]);
      key[domain_len - 1] = ';';
      strcpy(key + domain_len, msg[2]);

      ngx_shmtx_lock(&ngx_http_smockron_delay_pool->mutex);
      delay_hash_was_null = *ngx_http_smockron_delay_hash == NULL;
      HASH_FIND_STR(*ngx_http_smockron_delay_hash, key, delay);
      if (!delay) { /* Newly added */
        jmp_buf bailout;

        delay = ngx_slab_alloc_locked(ngx_http_smockron_delay_pool, sizeof(ngx_http_smockron_delay_t));
        if (delay == NULL) {
          ngx_log_error(NGX_LOG_ERR, ev->log, 0, "Allocating delay failed, increase smockron_shm_size");
          goto out_unlock;
        }
        strcpy(delay->key, key);
        delay->next_allowed = ts;
        if (setjmp(bailout) == 0) {
          HASH_ADD_STR(*ngx_http_smockron_delay_hash, key, delay);
        } else {
          ngx_log_error(NGX_LOG_ERR, ev->log, 0, "HASH_ADD_STR failed, increase smockron_shm_size");
          if (delay_hash_was_null && *ngx_http_smockron_delay_hash) {
            /* Otherwise we end up with a bad hash head that causes a segv on next access */
            ngx_slab_free_locked(ngx_http_smockron_delay_pool, *ngx_http_smockron_delay_hash);
            *ngx_http_smockron_delay_hash = NULL;
          }
          goto out_unlock;
        }
      } else if (delay->next_allowed < ts) {
        delay->next_allowed = ts;
      }
    }

    out_unlock:
    ngx_shmtx_unlock(&ngx_http_smockron_delay_pool->mutex);
    out:
    events = 0;
    zmq_getsockopt(control_socket, ZMQ_EVENTS, &events, &events_size);
  }
}

static void ngx_http_smockron_hash_cleanup_handler(ngx_event_t *ev) {
  ngx_http_smockron_delay_t *delay = NULL, *tmp = NULL;
  int freed = 0;

  ngx_shmtx_lock(&ngx_http_smockron_delay_pool->mutex);
  HASH_ITER(hh, *ngx_http_smockron_delay_hash, delay, tmp) {
    if (delay->next_allowed < ngx_current_msec) {
      HASH_DEL(*ngx_http_smockron_delay_hash, delay);
      ngx_slab_free_locked(ngx_http_smockron_delay_pool, delay);
      freed ++;
      if (freed >= 100)
        break;
    }
  }
  ngx_shmtx_unlock(&ngx_http_smockron_delay_pool->mutex);

  if (freed) {
    ngx_log_error(NGX_LOG_DEBUG, ev->log, 0, "Freed %d", freed);
  }

  ngx_add_timer(&hash_cleanup_event, 1000);
}
