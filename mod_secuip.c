/* 
**  mod_secuip.c -- Apache secuip module
**  [Autogenerated via ``apxs -n secuip -g'']
**
**    made by Sungjae Jang (sjang@sk.com)
**    IP Block for T cloud Web
**
**  To play with this sample module first compile it into a
**  DSO file and install it into Apache's modules directory 
**  by running:
**
**    $ apxs -c -i mod_secuip.c
**
**  Then activate it in Apache's httpd.conf file for instance
**  for the URL /API_PATH in as follows:
**
**    #   httpd.conf
**    LoadFile modules/libhiredis.so
**    LoadModule secuip_module modules/mod_secuip.so
**    RedisIP REDIS_IP_ADDRESS(x.x.x.x)
**    RedisPort REDIS_PORT(ex 6379)
**    RedisPassword "REDIS_PASSWORD"
**    <Location /API_APTH>
**        SecuipEnabled on
**        SecuipDurationSecond 30
**        SecuipMaxCallCount 4
**        SecuipBlockSecond 60
**    </Location>
**
**
*/ 


#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "ap_config.h"
#include "apr_queue.h"
#include "apr_strings.h"


#include "http_log.h"

/***************************/
#include "http_request.h"
/***************************/ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hiredis/hiredis.h"

#include <sys/types.h>
#include <unistd.h>


module AP_MODULE_DECLARE_DATA secuip_module;

/*
 *  *  ==============================================================================
 *   *   Our configuration prototype and declaration:
 *    *    ==============================================================================
 *     *     */
typedef struct {
    int enabled;      /* Enable or disable our module */
    int duration;         /* checking duration */
    int max_call_count;         /* request count for apicall */
    int block_time;         /* blocking time when exceed call count for duration */
    int block_response_code;
} secuip_dir_config;

typedef struct {
	int redis_queue_enabled; /* redis client queue */
	char redis_ip[128];  /* redis ip address */ 
	int redis_port; /* redis port */
	char redis_password[128]; /* redis password */
	int redis_init_count; /* redis client init count */
        char allow_ip_list[2048]; /* allow ip list (a.b.c.d,w.x.y.z) comma delimter */
    apr_pool_t *pool;
#if APR_HAS_THREADS
    apr_thread_mutex_t *mutex;
#endif
//    apr_queue_t *redis_context_queue;
} secuip_svr_config;

//static secuip_dir_config config;

static apr_queue_t *redis_context_queue;

static void *create_dir_conf(apr_pool_t *pool, char *context);
static void *merge_dir_conf(apr_pool_t *pool, void *BASE, void *ADD);
static void *create_svr_conf(apr_pool_t *p, server_rec *s);
static void *merge_svr_conf(apr_pool_t *p, void *basev, void *overridesv);
static int secuip_checker(request_rec *r);

static apr_table_t *allow_ip_table;

static void *create_dir_conf(apr_pool_t *pool, char *context)
{
    secuip_dir_config *config;
    
        config = apr_pcalloc(pool, sizeof(secuip_dir_config));
    if(config) {
        /* Set some default values */
        config->enabled = 0;
        config->duration = -1;
        config->max_call_count = -1;
        config->block_time = -1;
        config->block_response_code = 400; // BAD_REQUEST
    }

    return config;
}

void *merge_dir_conf(apr_pool_t *pool, void *BASE, void *ADD)
{
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
    secuip_dir_config    *base = (secuip_dir_config *) BASE;
    secuip_dir_config    *add = (secuip_dir_config *) ADD;
    secuip_dir_config    *dir_conf = (secuip_dir_config *) create_dir_conf(pool, "Merged configuration");
    /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

    if (add->enabled != 1) {
        //dir_conf->enabled = base->enabled;
        dir_conf->enabled = 1;
        dir_conf->duration = base->duration;
        dir_conf->max_call_count = base->max_call_count;
        dir_conf->block_time = base->block_time;
        dir_conf->block_response_code = base->block_response_code;
        return dir_conf;
    }

    //dir_conf->enabled = add->enabled;
    dir_conf->enabled = 1;
    dir_conf->duration = add->duration;
    dir_conf->max_call_count = add->max_call_count;
    dir_conf->block_time = add->block_time;
    dir_conf->block_response_code = add->block_response_code;

    return dir_conf;
}

const char *secuip_set_enabled(cmd_parms *cmd, void *cfg, const char *arg)
{
    secuip_dir_config *config = (secuip_dir_config *)cfg;

    if (config) {
        if(!strcasecmp(arg, "on")) {
	    	config->enabled = 1;
	    }
        else {
    		config->enabled = 0;
        }
    }
    return NULL;
}

const char *secuip_set_duration_second(cmd_parms *cmd, void *cfg, const char *arg)
{
    secuip_dir_config *config = (secuip_dir_config *)cfg;
    if (config) {
        config->duration = atoi(arg);
    }
    return NULL;
}

const char *secuip_set_max_call_count(cmd_parms *cmd, void *cfg, const char *arg)
{
    secuip_dir_config *config = (secuip_dir_config *)cfg;

    if (config) {
        config->max_call_count = atoi(arg);
    }
    return NULL;
}

const char *secuip_set_block_second(cmd_parms *cmd, void *cfg, const char *arg)
{
    secuip_dir_config *config = (secuip_dir_config *)cfg;

    if (config) {
        config->block_time = atoi(arg);
    }
    return NULL;
}

const char *secuip_set_block_response_code(cmd_parms *cmd, void *cfg, const char *arg)
{
    secuip_dir_config *config = (secuip_dir_config *)cfg;

    if (config) {
        config->block_response_code = atoi(arg);
    }
    return NULL;
}


// svr conf
static void *create_svr_conf(apr_pool_t *pool, server_rec *s)
{
    secuip_svr_config *svr_config = apr_pcalloc(pool, sizeof(secuip_svr_config));

    if(svr_config) {
        /* Set some default values */
        svr_config->redis_queue_enabled = 0;
        memset(svr_config->redis_ip, 0, 128);
        svr_config->redis_port = 0;
        memset(svr_config->redis_password, 0, 128);
        svr_config->redis_init_count = 0;
        memset(svr_config->allow_ip_list, 0, 2048);
    }

    return svr_config;
}

void *merge_svr_conf(apr_pool_t *p, void *basev, void *overridesv)
{
    secuip_svr_config *base = (secuip_svr_config *) basev;
    secuip_svr_config *add = (secuip_svr_config *) overridesv;
    secuip_svr_config *svr_config = apr_pcalloc(p, sizeof(secuip_svr_config));

    svr_config->redis_queue_enabled = (add->redis_queue_enabled == 0) ? base->redis_queue_enabled : add->redis_queue_enabled;
    memset(svr_config->redis_ip, 0, 128);
    strcpy(svr_config->redis_ip, strlen(add->redis_ip) ? add->redis_ip : base->redis_ip);
    svr_config->redis_port = (add->redis_port == 0) ? base->redis_port : add->redis_port;
    memset(svr_config->redis_password, 0, 128);
    strcpy(svr_config->redis_password, strlen(add->redis_password) ? add->redis_password : base->redis_password);
    svr_config->redis_init_count = (add->redis_init_count == 0) ? base->redis_init_count : add->redis_init_count;
    memset(svr_config->allow_ip_list, 0, 2048);
    strcpy(svr_config->allow_ip_list, strlen(add->allow_ip_list) ? add->allow_ip_list : base->allow_ip_list);

    return svr_config;
}

const char *secuip_set_redis_ip(cmd_parms *cmd, void *cfg, const char *arg)
{

    secuip_svr_config *svr_config = (secuip_svr_config *)ap_get_module_config(cmd->server->module_config, &secuip_module);

    if (svr_config) {
        strcpy(svr_config->redis_ip, arg);
    }
    return NULL;
}

const char *secuip_set_redis_port(cmd_parms *cmd, void *cfg, const char *arg)
{
    apr_status_t rv;

    secuip_svr_config *svr_config = (secuip_svr_config *)ap_get_module_config(cmd->server->module_config, &secuip_module);

    if (svr_config) {
        svr_config->redis_port = atoi(arg);
    }
    return NULL;
}

const char *secuip_set_redis_password(cmd_parms *cmd, void *cfg, const char *arg)
{
    secuip_svr_config *svr_config = (secuip_svr_config *)ap_get_module_config(cmd->server->module_config, &secuip_module);

    if (svr_config) {
        strcpy(svr_config->redis_password,arg);
    }
    return NULL;
}

const char *secuip_set_redis_init_count(cmd_parms *cmd, void *cfg, const char *arg)
{
    secuip_svr_config *svr_config = (secuip_svr_config *)ap_get_module_config(cmd->server->module_config, &secuip_module);

    if (svr_config) {
        svr_config->redis_init_count = atoi(arg);
    }
    return NULL;
}


const char *secuip_set_redis_queue_enabled(cmd_parms *cmd, void *cfg, const char *arg)
{
    secuip_svr_config *svr_config = (secuip_svr_config *)ap_get_module_config(cmd->server->module_config, &secuip_module);

    if (svr_config) {
        if(!strcasecmp(arg, "on")) {
	    	svr_config->redis_queue_enabled = 1;
	    }
        else {
    		svr_config->redis_queue_enabled = 0;
    	}
    }
    return NULL;
}

const char *secuip_set_allow_ip_list(cmd_parms *cmd, void *cfg, const char *arg)
{
    secuip_svr_config *svr_config = (secuip_svr_config *)ap_get_module_config(cmd->server->module_config, &secuip_module);

    if (svr_config) {
        strcpy(svr_config->allow_ip_list,arg);
    }
    return NULL;
}

//////////////// Application functions ///////////////////////////


void static free_redis_ctx(redisContext *ctx, server_rec *svr)
{
    redisFree(ctx);
}

static redisContext *init_redisclient(server_rec *s, const char *redis_ip, int redis_port, const char *redis_password)
{

    redisContext *ctx;
    redisReply *reply;
    struct timeval timeout = { 1, 500000 }; // 1.5 seconds

    ctx = redisConnectWithTimeout(redis_ip, redis_port, timeout);
    if (ctx == NULL || ctx->err) {

        if (ctx) {
            free_redis_ctx(ctx, s);
        } 
    	ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "redis connection fail.");
        return NULL;
    }

    // AUTH 
    reply = redisCommand(ctx,"AUTH %s", redis_password);
    if (reply == NULL) {
        // wrong redis password
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"wrong redis password");
        free_redis_ctx(ctx, s);
        return NULL;
    }
    // DEBUG
    //ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"redis reply: AUTH, type[%d], str[%s]", reply->type, reply->str);
    freeReplyObject(reply);

    return ctx;
}

enum {
    REDIS_ERROR = -1,
    TRYPOP_REDIS_CONTEXT,
    TRYPUSH_REDIS_CONTEXT
};

static int manage_redis_context_queue(redisContext **ctx, int management_mode, request_rec *r)
{

    apr_status_t rv; 
    int redis_queue_enabled = 0;

    secuip_svr_config *svr_config = (secuip_svr_config *) ap_get_module_config(r->server->module_config, &secuip_module);

    // check the redis connection pool on/off setting
    redis_queue_enabled = svr_config->redis_queue_enabled;

    if (management_mode == TRYPOP_REDIS_CONTEXT) {
        // not using queue
        if (!redis_queue_enabled) {
            *ctx = init_redisclient(r->server, svr_config->redis_ip, svr_config->redis_port, svr_config->redis_password);
            if (*ctx == NULL) // redis error
            {
                ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "trying to redis connection error.[%d]");
                return -1; // reponse: DECLINED
            }
            return 0;
        }

        // pop redis context
        rv = apr_queue_trypop(redis_context_queue, (void **)ctx); 
        if (rv != APR_SUCCESS) {
            // queue empty
            if (rv == APR_EAGAIN) {
                ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "redis connection pool is empty. skipping to check request count.");

                *ctx = init_redisclient(r->server, svr_config->redis_ip, svr_config->redis_port, svr_config->redis_password);
                if (*ctx == NULL) { // redis error 
                    ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "trying to redis connection error.[%d]");
                    return -1; // reponse: DECLINED
                }
            }
            else {
                ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "queue error(trypop)");
                return -1; // reponse: DECLINED
            }
        }
    }
    else if (management_mode == TRYPUSH_REDIS_CONTEXT) {
        if (!redis_queue_enabled) {
            free_redis_ctx(*ctx, r->server);
            return 0; // success
        }

        // push redis context for reuse
        rv = apr_queue_trypush(redis_context_queue, *ctx);
        if (rv != APR_SUCCESS) {
            // anyway, free context
            free_redis_ctx(*ctx, r->server);

            if (rv == APR_EAGAIN) { // queue full 
                ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "redis connection pool is full.(if this occures, there is a error of queue management.");
            }
            ap_log_error(APLOG_MARK, APLOG_ERR, rv, r->server,"Failed to push queue in block module");
            return 0; //success
        }
        // log (current queue size)
        //ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "redis context pushed[%d].", apr_queue_size(redis_context_queue));
    }

    return 0;
}

static redisReply *send_redis_command(server_rec *s, redisContext **ctx, const char *redis_command, const char *redis_ip, int redis_port, const char *redis_password)
{
    redisReply *reply; 
    reply = redisCommand(*ctx, redis_command);
    if (reply == NULL) {
        free_redis_ctx(*ctx, s); // free current current ctx

        // try to recover redis connection
        *ctx = init_redisclient(s, redis_ip, redis_port, redis_password);
        if (*ctx == NULL) {
            ap_log_error(APLOG_MARK, APLOG_CRIT, 0, s, "recovery redis connection error.");
            return NULL;
        }
        reply = redisCommand(*ctx, redis_command);
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "redis command error[%s]. trying to recover redis connection and retry the command -> OK!", redis_command);
    }
    // DEBUG
    //ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"redis reply: command[%s], type[%d], str[%s]", redis_command, reply->type, reply->str);
    return reply;
}

static void set_allow_ip_table(apr_table_t *allow_ip_table, char *ip_list, apr_pool_t *pchild)
{
    char *p;
    p = strtok(ip_list, ",");   
    while (p) {
        apr_table_set(allow_ip_table, p, "ALLOW");
        p = strtok(NULL, ",");
    }
    return;
}

static void setup_redisclient_child_init(apr_pool_t *pchild, server_rec *s)
{
    apr_status_t rv; 
    redisContext *ctx;
    redisReply *reply;
    int i;

    ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pchild, "redis client initializaion.(apache child. PID:[%d])", getpid());

    /*********************/
    secuip_svr_config *svr_config = (secuip_svr_config *)ap_get_module_config(s->module_config, &secuip_module);
    
    //TODO: per virtualServer(maybe next version)
    //secuip_svr_config *svr_config = (secuip_svr_config *)ap_get_module_config(s->next->module_config, &secuip_module);
    /*********************/

    rv = apr_pool_create(&svr_config->pool, pchild);
    if (rv != APR_SUCCESS) {
        ap_log_perror(APLOG_MARK, APLOG_CRIT, rv, pchild, "Failed to create subpool for secuip");
        return;
    }

#if APR_HAS_THREADS
    rv = apr_thread_mutex_create(&svr_config->mutex, APR_THREAD_MUTEX_DEFAULT, svr_config->pool);
    if (rv != APR_SUCCESS) {
        ap_log_perror(APLOG_MARK, APLOG_CRIT, rv, svr_config->pool,"[0]Failed to create mutex for sucuip");
        return; 
    }
#endif

//    rv = apr_queue_create(&svr_config->redis_context_queue, svr_config->redis_init_count, svr_config->pool);
    rv = apr_queue_create(&redis_context_queue, svr_config->redis_init_count, svr_config->pool);
    if (rv != APR_SUCCESS) {
        ap_log_perror(APLOG_MARK, APLOG_CRIT, rv, svr_config->pool,"[1]Failed to create queue for secuip");
        return; 
    }

    // not using redis connection pool
    if (svr_config->redis_queue_enabled != 1) {
        ap_log_perror(APLOG_MARK, APLOG_ERR, 0, pchild, "svr_config->redis_queue_enabled value[%d].(apache child. PID:[%d], init count[%d])", svr_config->redis_queue_enabled, getpid(), svr_config->redis_init_count);
        return;
    }

    for (i = 0; i < svr_config->redis_init_count; i++) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,"init redis for secuip:[%d], PID:[%d]", i, getpid());
        ctx = init_redisclient(s, svr_config->redis_ip, svr_config->redis_port, svr_config->redis_password);
        if ( ctx == NULL) {
            ap_log_error(APLOG_MARK, APLOG_CRIT, 0, s, "init redisclient error.");
            return;
        }

        // reg. cleanup
        //apr_pool_cleanup_register(svr_config->pool, ctx, redisFree, apr_pool_cleanup_null) ;

        // add ctx to queue.
        rv = apr_queue_trypush(redis_context_queue, ctx);
        if (rv != APR_SUCCESS) {
            // queue full
            //free
            free_redis_ctx(ctx, s);
            if (rv == APR_EAGAIN) {
            //redisCommand(ctx, "GET trypush_queue_full(%X)", ctx);
	            ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "redis connection pool is full.(if this occures, there is a error of queue management.");
            }
            //redisCommand(ctx, "GET trypush_error(%X)", ctx);
            ap_log_perror(APLOG_MARK, APLOG_ERR, rv, svr_config->pool, "[2]Failed to push queue for secuip.");
            return; 
        }
        // log (current queue size)
            //redisCommand(ctx, "GET trypush_success(%X)(pid%d)(size%d)", ctx, getpid(), apr_queue_size(redis_context_queue));
        //ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "redis context pushed[%d].", apr_queue_size(redis_context_queue));
    }

    //redisFree(ctx); // not necessary in here.


    // create apr table for allow_ip_list
    allow_ip_table = apr_table_make(pchild, 32); // init space 32
    set_allow_ip_table(allow_ip_table, svr_config->allow_ip_list, pchild);
    return;
}



static int secuip_checker(request_rec *r)
{

/***************************
Ref. http://redis.io/commands/INCR

FUNCTION LIMIT_API_CALL(ip):
current = GET(ip)
IF current != NULL AND current > 10 THEN
    ERROR "too many requests per second"
ELSE
    value = INCR(ip)
    IF value == 1 THEN
        EXPIRE(value,1)
    END
    PERFORM_API_CALL()
END

************************/

	int current_count = 0;
	int total_count = 0;
    redisContext *ctx = NULL;
    void *my_redis_ctx;
    redisReply *reply;
	int is_first_req = 0;
    apr_status_t rv;
    char redis_command[1024];
    int redis_queue_enabled = 0;

    // HTTP response code when blocked.
    int block_response_code = 400; // BAD_REQUEST(default)


    secuip_dir_config *config = (secuip_dir_config *) ap_get_module_config(r->per_dir_config, &secuip_module);
    secuip_svr_config *svr_config = (secuip_svr_config *) ap_get_module_config(r->server->module_config, &secuip_module);

    // not using secuip
	if (config->enabled != 1) {
	    ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "secuip conf not null and not applied, enabled:[%d] [%s]", config->enabled, r->uri);
		// not applied
		return DECLINED;
	}

//    const char *null = "NULL";

//    ap_rprintf(r, "uri: [%s], r->server->defn_name: [%s], r->server->server_hostname: [%s]\n", r->uri, (r->server->defn_name == NULL ? null:r->server->defn_name), \
            (r->server->server_hostname == NULL ? null : r->server->server_hostname) \
            );

    /*
    ap_rprintf(r, "svr conf redis Queue enabled: [%d]\n", svr_config->redis_queue_enabled);
    ap_rprintf(r, "svr conf redis ip: [%s]\n", svr_config->redis_ip);
    ap_rprintf(r, "svr conf redis init count: [%d]\n", svr_config->redis_init_count);

    ap_rprintf(r, "dir conf secu enabled : [%d]\n", config->enabled);
    ap_rprintf(r, "dir conf duration: [%d]\n", config->duration);
    */

#ifdef HTTPD22
    char *client_ip_address = r->connection->remote_ip;
#else
    char *client_ip_address = r->connection->client_ip;
#endif

    /* check allow ip */
    const char *allow_ip = apr_table_get(allow_ip_table, client_ip_address);
    if (allow_ip != NULL) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "%s is allowed", client_ip_address);
        return DECLINED;
    }

    // HTTP response code when blocked.
    block_response_code = config->block_response_code;

    // redis connection pool on/off setting
    redis_queue_enabled = svr_config->redis_queue_enabled;

    if (manage_redis_context_queue((redisContext **)&my_redis_ctx, TRYPOP_REDIS_CONTEXT, r) < 0) {
        return DECLINED;
    }

    const char *null = "NULL";
    // Secuip's key string is composed by ClientIP_ServerHostname_URI.
    char *key_string = apr_pstrcat(r->pool, client_ip_address, "_", (r->server->server_hostname == NULL ? null : r->server->server_hostname), "_", r->uri, NULL);

    ctx = (redisContext *)my_redis_ctx;

    memset(redis_command, 0, sizeof(redis_command));
    snprintf(redis_command, sizeof(redis_command), "GET %s", key_string);
	reply = send_redis_command(r->server, &ctx, redis_command, svr_config->redis_ip, svr_config->redis_port, svr_config->redis_password);
	if (reply == NULL) {
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "redis error(GET)");
        if (!redis_queue_enabled) {
    	    free_redis_ctx(ctx, r->server);
        }
		return DECLINED;
	}


	if (reply->str != NULL) {
	    current_count = atoi(reply->str);
	    freeReplyObject(reply);

		// INCR count (for total req. count)
        memset(redis_command, 0, sizeof(redis_command));
        snprintf(redis_command, sizeof(redis_command), "INCR %s", key_string);
		reply = send_redis_command(r->server, &ctx, redis_command, svr_config->redis_ip, svr_config->redis_port, svr_config->redis_password);
		if (reply == NULL) {
			// redis error
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "redis error(INCR1)");
            if (!redis_queue_enabled) {
                free_redis_ctx(ctx, r->server);
            }
			return block_response_code;
		}
		total_count = reply->integer;
		freeReplyObject(reply);


		if (current_count >= config->max_call_count) {
			if (current_count == config->max_call_count) { // set block time here only 
				//////////// set blocking time
                memset(redis_command, 0, sizeof(redis_command));
                snprintf(redis_command, sizeof(redis_command), "EXPIRE %s %d", key_string, config->block_time);
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "EXPIRE COMMAND(current_count == max_call_count) [%s]", redis_command);
				reply = send_redis_command(r->server, &ctx, redis_command, svr_config->redis_ip, svr_config->redis_port, svr_config->redis_password);
				//ap_rprintf(r, "INCR: [%d]<BR>\n", reply->integer);
				if (reply == NULL) {
					// redis error
					ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "redis error(EXPIRE1)");
                    if (!redis_queue_enabled) {
                        free_redis_ctx(ctx, r->server);
                    }
					return block_response_code;
				}
                                ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "REPLY(%s): type[%d], integer[%d], len[%d], str[%s], elements[%d]", redis_command, reply->type, reply->integer, reply->len, (reply->str == NULL ? "NULL" : reply->str));
				ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "Blocking [%s] [block time:%d]", key_string, config->block_time);
				freeReplyObject(reply);
				/////////////////////////////////////////
			}
			
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "Blocking [%s] [total req. count:%d]", key_string, total_count);

            if (manage_redis_context_queue(&ctx, TRYPUSH_REDIS_CONTEXT, r) < 0) {
                return DECLINED;
            }

			return block_response_code;
		}
        else {
		    ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "Passing [%s] [current count:%d]", key_string, total_count);

            if (manage_redis_context_queue(&ctx, TRYPUSH_REDIS_CONTEXT, r) < 0) {
                return DECLINED;
            }

        }
	}
	else { // the first req.
	    freeReplyObject(reply);
		is_first_req = 1;

		// when new request
		// INCR count 
        memset(redis_command, 0, sizeof(redis_command));
        snprintf(redis_command, sizeof(redis_command), "INCR %s", key_string, r->uri);
		reply = send_redis_command(r->server, &ctx, redis_command, svr_config->redis_ip, svr_config->redis_port, svr_config->redis_password);
		if (reply == NULL) {
			// redis error
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "redis error(INCR2)");
            if (!redis_queue_enabled) {
                free_redis_ctx(ctx, r->server);
            }
			return DECLINED;
		}
        freeReplyObject(reply);

		//////////// set duration time
        memset(redis_command, 0, sizeof(redis_command));
        snprintf(redis_command, sizeof(redis_command), "EXPIRE %s %d", key_string, config->duration);
	ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "EXPIRE COMMAND(the first req.) [%s]", redis_command);
		reply = send_redis_command(r->server, &ctx, redis_command, svr_config->redis_ip, svr_config->redis_port, svr_config->redis_password);
		//ap_rprintf(r, "INCR: [%d]<BR>\n", reply->integer);
		if (reply == NULL) {
			// redis error
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "redis error(EXPIRE2)");
            if (!redis_queue_enabled) {
                free_redis_ctx(ctx, r->server);
            }
			return DECLINED;
		}
                ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "REPLY(%s): type[%d], integer[%d], len[%d], str[%s], elements[%d]", redis_command, reply->type, reply->integer, reply->len, (reply->str == NULL ? "NULL" : reply->str));
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server, "The FIRST request(within [%dsec])(Passing count:1) [%s] [duration time:%d]", config->duration, key_string, config->duration);
        freeReplyObject(reply);
		/////////////////////////////////////////
        if (manage_redis_context_queue(&ctx, TRYPUSH_REDIS_CONTEXT, r) < 0) {
            return DECLINED;
        }
	}


    //redisFree(ctx); // comment: not necessary here.

	return DECLINED;
}

static const command_rec secuip_directives[] =
{
    AP_INIT_TAKE1("SecuipRedisQueueEnabled", secuip_set_redis_queue_enabled, NULL, RSRC_CONF, "redis client queue"),
    AP_INIT_TAKE1("SecuipRedisIP", secuip_set_redis_ip, NULL, RSRC_CONF, "redis ip address"),
    AP_INIT_TAKE1("SecuipRedisPort", secuip_set_redis_port, NULL, RSRC_CONF, "redis port"),
    AP_INIT_TAKE1("SecuipRedisPassword", secuip_set_redis_password, NULL, RSRC_CONF, "redis password"),
    AP_INIT_TAKE1("SecuipRedisInitCount", secuip_set_redis_init_count, NULL, RSRC_CONF, "redis client init count"),
    AP_INIT_TAKE1("SecuipAllowIPList", secuip_set_allow_ip_list, NULL, RSRC_CONF, "allow ip"),

    AP_INIT_TAKE1("SecuipEnabled", secuip_set_enabled, NULL, ACCESS_CONF, "Enable or disable mod_secuip"),
    AP_INIT_TAKE1("SecuipDurationSecond", secuip_set_duration_second, NULL, ACCESS_CONF, "time for chcking"),
    AP_INIT_TAKE1("SecuipMaxCallCount", secuip_set_max_call_count, NULL, ACCESS_CONF, "max call count"),
    AP_INIT_TAKE1("SecuipBlockSecond", secuip_set_block_second, NULL, ACCESS_CONF, "time for block"),
    AP_INIT_TAKE1("SecuipBlockResponseCode", secuip_set_block_response_code, NULL, ACCESS_CONF, "HTTP response code when blocked"),

    { NULL }
};

static void secuip_register_hooks(apr_pool_t *p)
{
    static const char * const aszPost[]={ "mod_proxy.c", "mod_jk.c", NULL };
    ap_hook_fixups(secuip_checker, NULL, aszPost, APR_HOOK_REALLY_FIRST);
    ap_hook_child_init(setup_redisclient_child_init, NULL, NULL, APR_HOOK_REALLY_FIRST);
    //ap_hook_post_read_request(secuip_checker, NULL, aszPre, APR_HOOK_REALLY_FIRST);
    //ap_hook_access_checker(secuip_checker, NULL, aszPre, APR_HOOK_MIDDLE);
}


/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA secuip_module = {
    STANDARD20_MODULE_STUFF, 
    create_dir_conf,                  /* create per-dir    config structures */
    merge_dir_conf,                  /* merge  per-dir    config structures */
    create_svr_conf,                /* create per-server config structures */
    merge_svr_conf,                  /* merge  per-server config structures */
    secuip_directives,     /* table of config file commands       */
    secuip_register_hooks  /* register hooks                      */
};

