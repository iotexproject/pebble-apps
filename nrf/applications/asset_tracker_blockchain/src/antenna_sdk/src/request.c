#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#if(NOT_PLATFORM_NRF9160)
#include <curl/curl.h>
#endif
#include "debug.h"
#include "config.h"
#include "request.h"
#include "iotex_emb.h"
#if(IS_PLATFORM_NRF9160)
#include <zephyr.h>
#include <net/socket.h>
#include <modem/bsdlib.h>
#include <net/tls_credentials.h>
#include <modem/lte_lc.h>
#include <modem/at_cmd.h>
#include <modem/at_notif.h>
#include <modem/modem_key_mgmt.h>
#endif

#if defined(CONFIG_BSD_LIBRARY)
#include <modem/bsdlib.h>
#include <bsd.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#endif /* CONFIG_BSD_LIBRARY */

//#define  DEBUG_HTTP   

#ifdef DEBUG_HTTP
#define  HTTP_DEBUG_OUTPUT(...)  printk(__VA_ARGS__)
#else
#define  HTTP_DEBUG_OUTPUT(...)  
#endif




typedef struct {
    uint32_t req;
    const char *paths[3];
    const char *args_fmt;
} iotex_st_request_conf;


typedef struct {
    char *data;
    size_t len;
} iotex_st_response_data;


static const iotex_st_request_conf __g_req_configs[] = {
    {
        REQ_GET_ACCOUNT,
        {"accounts", NULL},
        "%s",
    },

    {
        REQ_GET_CHAINMETA,
        {"chainmeta", NULL}
    },

    {
        REQ_GET_ACTIONS_BY_ADDR,
        {"actions", "addr", NULL},
        "%s?start=%u&count=%u",
    },

    {
        REQ_GET_ACTIONS_BY_HASH,
        {"actions", "hash", NULL},
        "%s",
    },

    {
        REQ_READ_CONTRACT_BY_ADDR,
        {"contract", "addr", NULL},
        "%s?method=%s&data=%s",
    },

    {
        REQ_GET_TRANSFERS_BY_BLOCK,
        {"transfers", "block", NULL},
        "%s",
    },

    {
        REQ_GET_MEMBER_VALIDATORS,
        {"staking", "validators", NULL}
    },

    {
        REQ_GET_MEMBER_DELEGATIONS,
        {"staking", "delegations", NULL}
    },

    {
        REQ_SEND_SIGNED_ACTION_BYTES,
        {"actionbytes", NULL},
        "%s"
    },

    {
        REQ_TAIL_NONE, {NULL}
    }
};

#if(IS_PLATFORM_NRF9160)

typedef struct {
    char host[100];
    int  port;
    char path[1024];
}Host_Path;

#define POST_TEMPLATE "POST %s HTTP/1.1\r\n"\
                      "Host: %s\r\n"\
                      "Connection: close\r\n"\
                      "Content-type: application/x-www-form-urlencoded\r\n"\
                      "Content-length: %d\r\n\r\n"

#define GET_TEMPLATE "GET /%s HTTP/1.1\r\n"\
                      "Host: %s:%d\r\n"\
                      "Connection: close\r\n\r\n"                                       

#define HTTPS_PORT   8192

#define HTTP_HDR_END "\r\n\r\n"

//#define RECV_BUF_SIZE 2048
#define TLS_SEC_TAG 42

#define MAX_MTU_SIZE     1000
#define RECV_BUF_SIZE    IOTEX_EBM_MAX_RES_LEN
#define SEND_BUF_SIZE    MAX_MTU_SIZE

/* Certificate for `google.com` */
static const char cert_ca[] = {
    #include "./iotex-io.pem"   
};
#endif

/*
 * @brief: free iotex_st_response_data
 */
static void _free_response_data(iotex_st_response_data *res) {

    if (!res || !res->data) {
        return;
    }

    free(res->data);
    res->len = 0;
    res->data = NULL;
}


/*
 * @brief: curl receive data callback, copy received data to iotex_st_response_data
 */
static size_t _curl_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {

    char *new_ptr = NULL;
    size_t new_len = size * nmemb;
    iotex_st_response_data *res = userdata;

    /* First time */
    if (res->data == NULL) {

        res->data = 0;

        if (!(res->data = malloc(new_len)))  {
            __ERROR_MSG__("malloc");
            return 0;
        }
    }
    else {

        if (!(new_ptr = realloc(res->data, res->len + new_len))) {
            __ERROR_MSG__("realloc");
            _free_response_data(res);
            return 0;
        }

        res->data = new_ptr;
    }

    /* Append new data to response data */
    memcpy(res->data + res->len, ptr, new_len);
    res->len += new_len;
    return new_len;
}


/*
 * @brief: compose https request url
 * #url: buffer to save composed url
 * #url_max_size: #url buffer max size(bytes)
 * #req: IotexHttpRequests request
 * $return: successed return composed url, failed return NULL
 *
 * TODO:
 * 1. find a way check va_args number
 */
char *req_compose_url(char *url, size_t url_max_size, iotex_em_request req, ...) {

    assert(url != NULL);

    int i;
    va_list ap;
    size_t path_len;
    char *url_tail = NULL;
    //iotex_st_config config = get_config();
    const iotex_st_request_conf *conf = NULL;

    /* Get request config */
    for (i = 0; __g_req_configs[i].paths[0] != NULL; i++) {
        if (__g_req_configs[i].req == req) {
            conf = __g_req_configs + i;
            break;
        }
    }

    if (!conf || !conf->paths[0]) {
        __WARN_MSG__("unknown request");
        return NULL;
    }

    /* Copy base url and version */
    memset(url, 0, url_max_size);
    snprintf(url, url_max_size, IOTEX_EMB_BASE_URL, 1);

    /* Compose request url, without args */
    for (i = 0, url_tail = url + strlen(url); conf->paths[i]; i++) {

        path_len = strlen(conf->paths[i]);

        if ((url_tail - url + path_len + 1) < url_max_size) {

            memcpy(url_tail, conf->paths[i], path_len);
            url_tail += path_len;
            *url_tail = '/';
            url_tail++;
        }
        else {

            __WARN_MSG__("url buffer too short!");
            return NULL;
        }
    }

    /* No request args */
    if (!conf->args_fmt)  {
        --url_tail;
        *url_tail = 0;
        return url;
    }

    /* Append post args to url */
    va_start(ap, req);
    vsnprintf(url_tail, url_max_size - (url_tail - url), conf->args_fmt, ap);
    va_end(ap);

    return url;
}

#if(IS_PLATFORM_NRF9160)

void parseURL(char *url, Host_Path *parse)
{
    HTTP_DEBUG_OUTPUT("url:%s\n",url);
    sscanf(url, "http://%99[^:]:%99d/%999[^\n]", parse->host,&parse->port, parse->path);
}


#define  PEM_WRITE_ADDRESS  MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN

/* Provision certificate to modem */
int http_cert_provision(void)
{
	int err;
	bool exists;
	u8_t unused;

    if(strncmp(IOTEX_EMB_BASE_URL,"https",5))
        return  0;
	err = modem_key_mgmt_exists(TLS_SEC_TAG,
				    PEM_WRITE_ADDRESS,
				    &exists, &unused);
	if (err) {
		printk("Failed to check for certificates err %d\n", err);
		return err;
	}   
	if (exists) {
		/* For the sake of simplicity we delete what is provisioned
		 * with our security tag and reprovision our certificate.
		 */
		err = modem_key_mgmt_delete(TLS_SEC_TAG,
					    PEM_WRITE_ADDRESS);
		if (err) {
			printk("Failed to delete existing certificate, err %d\n",
			       err);
		}
	}

	printk("Provisioning certificate\n");

	/*  Provision certificate to the modem */
	err = modem_key_mgmt_write(TLS_SEC_TAG,
				   PEM_WRITE_ADDRESS,
				   cert_ca, sizeof(cert_ca) - 1);
	if (err) {
		printk("Failed to provision certificate, err %d\n", err);
		return err;
	} 

	return 0;
}
/* Setup TLS options on a given socket */
int tls_setup(int fd)
{
	int err;
	int verify;

	/* Security tag that we have provisioned the certificate with */
	const sec_tag_t tls_sec_tag[] = {
		TLS_SEC_TAG,
	};

	/* Set up TLS peer verification */
	enum {
		NONE = 0,
		OPTIONAL = 1,
		REQUIRED = 2,
	};

	verify = REQUIRED;

	err = setsockopt(fd, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
	if (err) {
		printk("Failed to setup peer verification, err %d\n", errno);
		return err;
	}

	/* Associate the socket with the security tag
	 * we have provisioned the certificate with.
	 */
	err = setsockopt(fd, SOL_TLS, TLS_SEC_TAG_LIST, tls_sec_tag,
			 sizeof(tls_sec_tag));
	if (err) {
		printk("Failed to setup TLS sec tag, err %d\n", errno);
		return err;
	}

	return 0;
}

int blocking_recv(int fd, u8_t *buf, u32_t size, u32_t flags)
{
    int err;
	do {
		err = recv(fd, buf, size, flags);
	} while (err < 0 && (errno == EAGAIN));
	return err;
}

int blocking_send(int fd, u8_t *buf, u32_t size, u32_t flags)
{
	int err;

	do {
		err = send(fd, buf, size, flags);
	} while (err < 0 && (errno == EAGAIN));

	return err;
}

int blocking_connect(int fd, struct sockaddr *local_addr, socklen_t len)
{
	int err;

	do {
		err = connect(fd, local_addr, len);        
	} while (err < 0 && errno == EAGAIN);

	return err;
}

#endif
/*
 * @brief: send a request to server and get response data
 * #url: request url, it should be composed with url and data
 * #response: store request response data
 * #response_max_size: #response buffer max len
 * #is_post: set this indicate it's a post request
 * $return: successed return 0, failed return negative error code
 *
 * TODO:
 * 1. add meaningful error code
 * 2. add zero copy version ? (don't forget release response)
 * 3. add two-way authentication support
 */
static int req_basic_request(const char *request, char *response, size_t response_max_size, uint32_t is_post) {
	int err,i;
	int fd,send_data_len,num_bytes;
	struct addrinfo *res;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
    Host_Path *pHost;
    char *send_buf, *recv_buf = response,*pStr,*pStr1,*pStr2;
    struct timeval timeout = {5,0}; 
    int tot_num_bytes;
    pHost = malloc(sizeof(Host_Path));
    send_buf = malloc(SEND_BUF_SIZE);    
    if((pHost == NULL)||(send_buf == NULL))
    {
        printk("malloc error file :%s, line:%d \n",__FILE__, __LINE__);
        return -1;
    }     
    parseURL(request, pHost);
    net_mutex_lock();
	err = getaddrinfo(pHost->host, NULL, &hints, &res);
	if (err) {
		printk("getaddrinfo() failed, err %d\n", errno); 
        net_mutex_unlock();
		goto clean_up;
	}
	((struct sockaddr_in *)res->ai_addr)->sin_port = htons(pHost->port);  
    
    if(!strncmp(IOTEX_EMB_BASE_URL,"https",5)) {
        fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);
        if (fd == -1) {
            printk("Failed to open socket!\n");    
            net_mutex_unlock();                   
            goto clean_up;
        }
        /* Setup TLS socket options */
        err = tls_setup(fd);
        if (err) {     
            net_mutex_unlock();                  
            goto clean_up;
        }
    }
    else {
        fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd == -1) {
            printk("Failed to open socket!%d\n", errno);  
            net_mutex_unlock();                     
            goto clean_up;
        }
    }
    setsockopt(fd, NRF_SOL_SOCKET,NRF_SO_SNDTIMEO, (char *)&timeout,sizeof(struct timeval));
    setsockopt(fd, NRF_SOL_SOCKET,NRF_SO_RCVTIMEO, (char *)&timeout,sizeof(struct timeval));

	HTTP_DEBUG_OUTPUT("Connecting to %s\n", pHost->host);  
    err = blocking_connect(fd, (struct sockaddr *)res->ai_addr,
			       sizeof(struct sockaddr_in));
    if(err){
        net_mutex_unlock();              
        goto clean_up;
    }        

    HTTP_DEBUG_OUTPUT("connect ok \n");
    if (is_post) {
        send_data_len = snprintf(send_buf,
                             MAX_MTU_SIZE,
			     			 POST_TEMPLATE, pHost->path,
		             		 pHost->host,0);
    }
    else {
        
         send_data_len = snprintf(send_buf,
                             MAX_MTU_SIZE,
			     			 GET_TEMPLATE, pHost->path,
		             		 pHost->host,pHost->port);  
                                 
    }
    HTTP_DEBUG_OUTPUT("\n\rsend_data_len:%d,Send HTTP  request.:\n", send_data_len);
    HTTP_DEBUG_OUTPUT("%s \n",send_buf); 
    tot_num_bytes = 0;
    do {
	    num_bytes = blocking_send(fd, send_buf+tot_num_bytes, send_data_len-tot_num_bytes, 0);		
		if (num_bytes < 0) {
			printk("ret: %d, errno: %s\n", num_bytes, strerror(errno));
		}
        tot_num_bytes += num_bytes;
    } while (num_bytes < 0);    
    HTTP_DEBUG_OUTPUT("Start recv\n");
    tot_num_bytes = 0;
	do {
		num_bytes =blocking_recv(fd, recv_buf+tot_num_bytes, RECV_BUF_SIZE-tot_num_bytes, 0);		
		if (num_bytes <= 0) {
			break;
		}  
        tot_num_bytes += num_bytes;
	} while (num_bytes > 0);
    net_mutex_unlock();     
    HTTP_DEBUG_OUTPUT("taotal:%d\n", tot_num_bytes);
    if(tot_num_bytes < RECV_BUF_SIZE)
        recv_buf[tot_num_bytes] = 0;
    recv_buf[response_max_size-1] = 0;
    HTTP_DEBUG_OUTPUT("%s\n", recv_buf);
    if(strstr(recv_buf, "Transfer-Encoding: chunked") != NULL)
    {
        pStr = strstr(recv_buf,"\r\n\r\n");
        if(pStr)
        {
            pStr += 4;            
            tot_num_bytes = 0;
            num_bytes = strtol(pStr, NULL, 16);
            pStr1 = strstr(pStr,"{\"");
            pStr = pStr1;
            tot_num_bytes += num_bytes;            
            while(num_bytes)
            {
                pStr1 += 2;
                pStr1 += num_bytes;
                num_bytes = strtol(pStr1, NULL, 16);
                //printk("num_bytes2:%d\n", num_bytes);
                if(num_bytes)
                {
                    tot_num_bytes += num_bytes;                    
                    pStr2 = strstr(pStr1,"\r\n");
                    pStr2 += 2;
                    memcpy(pStr1, pStr2, num_bytes);
                    pStr1 = pStr2;
                }
            }
            pStr[tot_num_bytes] = 0;
            HTTP_DEBUG_OUTPUT("lenth: %d\n", tot_num_bytes);
            HTTP_DEBUG_OUTPUT("pStr:  %s\n", pStr);            
        }      
    }
    else
    {
        pStr = strstr(recv_buf, "{\"");            
    }      
    if (!is_post && pStr) {
        // get data        
        i = 0;
        while(pStr[i])
        {
            recv_buf[i] = pStr[i];
            i++;            
        } 
        recv_buf[i] = 0;
    }
     HTTP_DEBUG_OUTPUT("response:  \n %s\n", recv_buf); 
clean_up:
	freeaddrinfo(res);
	(void)close(fd);
    free(pHost);
    free(send_buf);   
    return 0;
}

int req_get_request(const char *request, char *response, size_t response_max_size) {

    return req_basic_request(request, response, response_max_size, 0);
}

int req_post_request(const char *request, char *response, size_t response_max_size) {

    return req_basic_request(request, response, response_max_size, 1);
}
