#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
  ngx_flag_t enabled;
  ngx_str_t server;
  ngx_str_t domain;
  ngx_str_t identifier_varname;
  ngx_int_t identifier_idx;
} ngx_http_smockron_conf_t;

static void *ngx_http_smockron_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_smockron_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_http_smockron_init(ngx_conf_t *cf);

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
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_http_smockron_conf_t, identifier_varname),
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
  conf->identifier_idx = NGX_CONF_UNSET;
  conf->enabled = NGX_CONF_UNSET;

  ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "create_loc_conf");

  return conf;
}

static char *parse_variable_name(ngx_conf_t *cf, ngx_str_t str, ngx_int_t *dest) {
  ngx_int_t idx = NGX_ERROR;
  ngx_str_t varname;

  if (str.data[0] == '$') {
    varname.data = str.data + 1;
    varname.len = str.len - 1;
    idx = ngx_http_get_variable_index(cf, &varname);
  }
  if (idx == NGX_ERROR) {
    return NGX_CONF_ERROR;
  } else {
    *dest = idx;
    return NGX_CONF_OK;
  }
}

static char *ngx_http_smockron_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child) {
  ngx_http_smockron_conf_t *prev = parent;
  ngx_http_smockron_conf_t *conf = child;

  ngx_conf_merge_value(conf->enabled, prev->enabled, 0);
  ngx_conf_merge_str_value(conf->server, prev->server, "tcp://localhost:10004");
  ngx_conf_merge_str_value(conf->domain, prev->domain, "default");
  ngx_conf_merge_str_value(conf->identifier_varname, prev->identifier_varname, "$remote_addr");

  if (parse_variable_name(cf, conf->identifier_varname, &(conf->identifier_idx)) == NGX_CONF_ERROR) {
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
        "Invalid identifier varname \"%s\"", conf->identifier_varname.data);
    return NGX_CONF_ERROR;
  }
  
  ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "merge_loc_conf");

  return NGX_CONF_OK;
}

static ngx_int_t ngx_http_smockron_handler(ngx_http_request_t *r) {
  ngx_http_smockron_conf_t *smockron_config;
  ngx_http_variable_value_t *ident;

  if (ngx_http_get_module_ctx(r->main, ngx_http_smockron_module) != NULL)
    return NGX_DECLINED;

  smockron_config = ngx_http_get_module_loc_conf(r, ngx_http_smockron_module);

  if (!smockron_config->enabled)
    return NGX_DECLINED;

  ngx_http_set_ctx(r->main, (void *)1, ngx_http_smockron_module);

  ident = ngx_http_get_indexed_variable(r, smockron_config->identifier_idx);
  if (ident == NULL || ident->not_found) {
    ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0,
        "Variable not found: \"%s\"", smockron_config->identifier_varname.data);
  } else {
    ngx_log_error(NGX_LOG_EMERG, r->connection->log, 0,
        "Var \"%s\"=\"%s\"", smockron_config->identifier_varname.data, ident->data);
  }

  return NGX_DECLINED;
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
