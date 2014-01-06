#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
  ngx_flag_t enabled;
  ngx_str_t server;
  ngx_str_t zone;
} ngx_http_smockron_conf_t;

static void *ngx_http_smockron_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_smockron_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);

static ngx_command_t ngx_http_smockron_commands[] = {
  {
    ngx_string("smockron_enabled"),
    NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
    ngx_conf_set_flag_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_smockron_conf_t, enabled),
    NULL
  },
  {
    ngx_string("smockron_server"),
    NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_smockron_conf_t, server),
    NULL
  },
  {
    ngx_string("smockron_zone"),
    NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_smockron_conf_t, zone),
    NULL
  },
  ngx_null_command
};

static ngx_http_module_t ngx_http_smockron_module_ctx = {
  NULL,                          /* preconfiguration */
  NULL,                          /* postconfiguration */

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
  NULL,                          /* init process */
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
  conf->server = NGX_CONF_UNSET;
  conf->zone = NGX_CONF_UNSET;
  return conf;
}

static char *ngx_http_smockron_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child) {
  ngx_http_smockron_conf_t *prev = parent;
  ngx_http_smockron_conf_t *conf = child;

  ngx_merge_conf_flag_value(conf->enabled, prev->enabled, 0);
  ngx_merge_conf_str_value(conf->server, prev->server, ngx_string("tcp://localhost:10004"));
  ngx_merge_conf_str_value(conf->zone, prev->zone, ngx_string("default"));

  return NGX_CONF_OK;
}


