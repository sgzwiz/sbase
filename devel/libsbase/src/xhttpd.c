#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <dirent.h>
#include <sys/resource.h>
#include <sbase.h>
#include "iniparser.h"
#include "http.h"
#include "mime.h"
#include "trie.h"
#include "stime.h"
#define HTTP_RESP_OK            "HTTP/1.1 200 OK"
#define HTTP_BAD_REQUEST        "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"
#define HTTP_NOT_FOUND          "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n" 
#define HTTP_NOT_MODIFIED       "HTTP/1.1 304 Not Modified\r\nContent-Length: 0\r\n\r\n"
#define HTTP_NO_CONTENT         "HTTP/1.1 206 No Content\r\nContent-Length: 0\r\n\r\n"
#define LL(x) ((long long)x)
#define UL(x) ((unsigned long int)x)
static SBASE *sbase = NULL;
static SERVICE *service = NULL;
static dictionary *dict = NULL;
static char *httpd_home = "/tmp/xhttpd/html";
static char *http_indexes[HTTP_INDEX_MAX];
static int http_indexes_view = 0;
static int nindexes = 0;
static char *http_default_charset = "UTF-8";
static int httpd_compress = 1;
static char *httpd_compress_cachedir = "/tmp/xhttpd/cache";
static HTTP_VHOST httpd_vhosts[HTTP_VHOST_MAX];
static int nvhosts = 0;
static void *namemap = NULL;

int xhttpd_mkdir(char *path, int mode)
{
    char *p = NULL, fullpath[HTTP_PATH_MAX];
    int ret = 0, level = -1;
    struct stat st;

    if(path)
    {
        strcpy(fullpath, path);
        p = fullpath;
        while(*p != '\0')
        {
            if(*p == '/' )
            {
                level++;
                while(*p != '\0' && *p == '/' && *(p+1) == '/')++p;
                if(level > 0)
                {
                    *p = '\0';
                    memset(&st, 0, sizeof(struct stat));
                    ret = stat(fullpath, &st);
                    if(ret == 0 && !S_ISDIR(st.st_mode)) return -1;
                    if(ret != 0 && mkdir(fullpath, mode) != 0) return -1;
                    *p = '/';
                }
            }
            ++p;
        }
        return 0;
    }
    return -1;
}


/* xhttpd packet reader */
int xhttpd_packet_reader(CONN *conn, CB_DATA *buffer)
{
    return buffer->ndata;
}

/* packet handler */
int xhttpd_packet_handler(CONN *conn, CB_DATA *packet)
{
    char buf[HTTP_BUF_SIZE], zfile[HTTP_PATH_MAX], zoldfile[HTTP_PATH_MAX], 
         file[HTTP_PATH_MAX], link[HTTP_PATH_MAX], *host = NULL, *mime = NULL, 
         *home = NULL, *pp = NULL, *p = NULL, *end = NULL, *root = NULL, 
         *s = NULL, *outfile = NULL;
    int i = 0, n = 0, found = 0, nmime = 0, is_need_compress = 0;
    off_t from = 0, to = 0, len = 0;
    struct dirent *ent = NULL;
    HTTP_REQ http_req = {0};
    struct stat st = {0};
    DIR *dirp = NULL;
    void *dp = NULL;

    if(conn && packet)
    {
        p = packet->data;
        end = packet->data + packet->ndata;
        //fprintf(stdout, "%s", p);
        if(http_request_parse(p, end, &http_req) == -1) goto err;
        if(http_req.reqid == HTTP_GET)
        {
            if((n = http_req.headers[HEAD_REQ_HOST]) > 0)
            {
                p = http_req.hlines + n;
                if(strncasecmp(p, "www.", 4) == 0) p += 4;
                host = p;
                while(*p != ':' && *p != '\0')++p;
                n = p - host;
                TRIETAB_GET(namemap, host, n, dp);
                if((i = ((long)dp - 1)) >= 0) home = httpd_vhosts[i].home;  
            }
            if(home == NULL) home = httpd_home;
            if(home == NULL) goto err;
            p = file;
            p += sprintf(p, "%s", home);
            root = p;
            if(http_req.path[0] != '/')
                p += sprintf(p, "/%s", http_req.path);
            else
                p += sprintf(p, "%s", http_req.path);
            if((n = (p - file)) > 0 && lstat(file, &st) == 0)
            {
                if(S_ISDIR(st.st_mode))
                {
                    i = 0;
                    found = 0;
                    if(p > file && *(p-1) != '/') *p++ = '/';
                    while(i < nindexes && http_indexes[i])
                    {
                        pp = p;
                        pp += sprintf(pp, "%s", http_indexes[i]);
                        if(access(file, F_OK) == 0)
                        {
                            found = 1;
                            p = pp;
                            break;
                        }
                        ++i;
                    }
                    if(found == 0 && http_indexes_view && (*p = '\0') >= 0 
                            && (dirp = opendir(file)))
                    {
                        end = --p;
                        if((p = pp = (char *)calloc(1, HTTP_INDEXES_MAX)))
                        {
                            p += sprintf(p, "<html><head><title>Indexes Of %s</title>"
                                    "<head><body><h1 align=center>xhttpd</h1>", root);
                            if(*end == '/') --end;
                            while(end > root && *end != '/')--end;
                            if(end == root)
                                p += sprintf(p, "<a href='/' >/</a><br>");
                            else if(end > root)
                            {
                                *end = '\0';
                                p += sprintf(p, "<a href='%s/' >..</a><br>", root);
                            }
                            p += sprintf(p, "<hr noshade><table><tr align=left>"
                                    "<th width=500>Name</th><th width=200>Size</th>"
                                    "<th>Last-Modified</th></tr>");
                            end = p;
                            while((ent = readdir(dirp)) != NULL)
                            {
                                if(ent->d_name[0] != '.' && ent->d_reclen > 0)
                                {
                                    p += sprintf(p, "<tr>");
                                    if(ent->d_type == DT_DIR)
                                    {
                                        p += sprintf(p, "<td><a href='%s/' >%s/</a></td>", 
                                                ent->d_name, ent->d_name);
                                    }
                                    else
                                    {
                                        p += sprintf(p, "<td><a href='%s' >%s</a></td>", 
                                                ent->d_name, ent->d_name);
                                    }
                                    sprintf(buf, "%s/%s", file, ent->d_name);
                                    if(lstat(buf, &st) == 0)
                                    {
                                        if(st.st_size >= (off_t)HTTP_BYTE_G)
                                            p += sprintf(p, "<td>%.1fG</td>", 
                                                    (double)st.st_size/(double) HTTP_BYTE_G);
                                        else if(st.st_size >= (off_t)HTTP_BYTE_M)
                                            p += sprintf(p, "<td>%lldM</td>", 
                                                    st.st_size/(off_t)HTTP_BYTE_M);
                                        else if(st.st_size >= (off_t)HTTP_BYTE_K)
                                            p += sprintf(p, "<td>%lldK</td>", 
                                                    st.st_size/(off_t)HTTP_BYTE_K);
                                        else 
                                            p += sprintf(p, "<td>%lldB</td>", st.st_size);

                                    }
                                    p += sprintf(p, "<td>");
                                    p += GMTstrdate(st.st_mtime, p);
                                    p += sprintf(p, "</td>");
                                    p += sprintf(p, "</tr>");
                                }
                            }
                            p += sprintf(p, "</table>");
                            if(end != p) p += sprintf(p, "<hr noshade>");
                            p += sprintf(p, "<em></body></html>");
                            len = (off_t)(p - pp);
                            p = buf;
                            p += sprintf(p, "HTTP/1.1 200 OK\r\nContent-Length:%lld\r\n"
                                    "Content-Type: text/html;charset=%s\r\n",
                                    (long long int)(len), http_default_charset);
                            if((n = http_req.headers[HEAD_GEN_CONNECTION]) > 0)
                                p += sprintf(p, "Connection: %s\r\n", http_req.hlines + n);
                            p += sprintf(p, "Server: xhttpd\r\n\r\n");
                            conn->push_chunk(conn, buf, (p - buf));
                            conn->push_chunk(conn, pp, len);
                            free(pp);
                            pp = NULL;
                        }
                        closedir(dirp);
                        return 0;
                    }
                }
                mime = pp = p ;
                while(mime > file && *mime != '.')--mime;
                if(*mime == '.') nmime = p - ++mime;
                if(st.st_size == 0)
                {
                    return conn->push_chunk(conn, HTTP_NO_CONTENT, 
                            strlen(HTTP_NO_CONTENT));
                }
                else if((n = http_req.headers[HEAD_REQ_IF_MODIFIED_SINCE]) > 0
                        && str2time(http_req.hlines + n) == st.st_mtime)
                {
                    return conn->push_chunk(conn, HTTP_NOT_MODIFIED, 
                            strlen(HTTP_NOT_MODIFIED));
                }
                else
                {
                    if((n = http_req.headers[HEAD_REQ_RANGE]) > 0)
                    {
                        p = http_req.hlines + n;
                        while(*p == 0x20 || *p == '\t')++p;
                        if(strncasecmp(p, "bytes=", 6) == 0) p += 6;
                        while(*p == 0x20)++p;
                        if(*p == '-')
                        {
                            ++p;
                            while(*p == 0x20)++p;
                            if(*p >= '0' && *p <= '9') to = (off_t)atoll(p) + 1;
                        }
                        else if(*p >= '0' && *p <= '9')
                        {
                            from = (off_t) atoll(p++);
                            while(*p != '-')++p;
                            ++p;
                            while(*p == 0x20)++p;
                            if(*p >= '0' && *p <= '9') to = (off_t)atoll(p) + 1;
                        }
                    }
                    if(to == 0) to = st.st_size;
                    len = to - from;
                    p = buf;
                    if(from > 0)
                        p += sprintf(p, "HTTP/1.1 206 Partial Content\r\nAccept-Ranges: bytes\r\n"
                                "Content-Range: bytes %lld-%lld/%lld\r\n", 
                                LL(from), LL(to - 1), LL(st.st_size));
                    else
                        p += sprintf(p, "HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\n");
                    //"Content-Length:%lld\r\n", LL(len));
                    //fprintf(stdout, "%s::%d mime:%s[%d]\n", __FILE__, __LINE__, mime, nmime);
                    if(mime && nmime > 0)
                    {
                        TRIETAB_GET(namemap, mime, nmime, dp);
                        if((i = ((long)dp - 1)) >= 0)
                        {
                            p += sprintf(p, "Content-Type: %s;charset=%s\r\n",
                                    http_mime_types[i].s, http_default_charset);
                            if((n = http_req.headers[HEAD_REQ_ACCEPT_ENCODING]) > 0 
                                    && strstr(http_mime_types[i].s, "text"))
                            {
                                p = http_req.hlines + n;
#ifdef HAVE_ZLIB
                                if(strstr(p, "gzip")) is_need_compress |= HTTP_ENCODING_GZIP;	
                                if(strstr(p, "deflate")) is_need_compress |= HTTP_ENCODING_DEFLATE;
                                if(strstr(p, "compress")) is_need_compress |= HTTP_ENCODING_COMPRESS;
#endif
#ifdef HAVE_BZIP2
                                if(strstr(p, "bzip2")) is_need_compress |= HTTP_ENCODING_BZIP2;
#endif
                            }
                        }
                    }
                    if(httpd_compress && is_need_compress > 0)
                    {
                        sprintf(zfile, "%s%s.%lld.%lld.%lu", httpd_compress_cachedir, 
                            root, LL(from), LL(to), UL(st.st_mtime));
                        if(access(zfile, F_OK) == 0)
                        {
                            lstat(zfile, &st);
                            outfile = zfile;
                            from = 0;
                            len = st.st_size;
                        }
                        else
                        {
                            if(access(link, F_OK))
                            {
                                xhttpd_mkdir(link, 0755);
                            }
                            else 
                            {
                                if(readlink(link, zoldfile, HTTP_PATH_MAX) > 0)
                                    unlink(zoldfile);
                            }
#ifdef HAVE_ZLIB
                            if(is_need_compress & HTTP_ENCODING_DEFLATE)
                            {

                            }
                            else if(is_need_compress & HTTP_ENCODING_GZIP)
                            {

                            }
                            else if(is_need_compress & HTTP_ENCODING_COMPRESS)
                            {
                            }
#endif		
#ifdef HAVE_BZIP2
                            if(is_need_compress & HTTP_ENCODING_BZIP2)
                            {
                            }
#endif
                        }
                    }
                    else outfile = file;
                    if((n = http_req.headers[HEAD_GEN_CONNECTION]) > 0)
                        p += sprintf(p, "Connection: %s\r\n", http_req.hlines + n);
                    p += sprintf(p, "Last-Modified:");
                    p += GMTstrdate(st.st_mtime, p);
                    p += sprintf(p, "%s", "\r\n");//date end
                    p += sprintf(p, "Server: xhttpd\r\n\r\n");
                    conn->push_chunk(conn, buf, (p - buf));
                    //fprintf(stdout, "%s::%d root:%s\n", __FILE__, __LINE__, root);
                    return conn->push_file(conn, outfile, from, len);
                }
            }
        }
err:
        return conn->push_chunk(conn, HTTP_NOT_FOUND, strlen(HTTP_NOT_FOUND));
    }
    return -1;
}

/* data handler */
int xhttpd_data_handler(CONN *conn, CB_DATA *packet, CB_DATA *cache, CB_DATA *chunk)
{

}

/* OOB handler */
int xhttpd_oob_handler(CONN *conn, CB_DATA *oob)
{
    if(conn && conn->push_chunk)
    {
        conn->push_chunk((CONN *)conn, ((CB_DATA *)oob)->data, oob->ndata);
        return oob->ndata;
    }
    return -1;
}

/* signal */
static void xhttpd_stop(int sig)
{
    switch (sig) 
    {
        case SIGINT:
        case SIGTERM:
            fprintf(stderr, "xhttpd server is interrupted by user.\n");
            if(sbase)sbase->stop(sbase);
            break;
        default:
            break;
    }
}

/* Initialize from ini file */
int sbase_initialize(SBASE *sbase, char *conf)
{
	char *logfile = NULL, *s = NULL, *p = NULL, *cacert_file = NULL, *privkey_file = NULL;
	int n = 0, i = 0, ret = -1;
    void *dp = NULL;

	if((dict = iniparser_new(conf)) == NULL)
	{
		fprintf(stderr, "Initializing conf:%s failed, %s\n", conf, strerror(errno));
		_exit(-1);
	}
	/* SBASE */
	sbase->nchilds = iniparser_getint(dict, "SBASE:nchilds", 0);
	sbase->connections_limit = iniparser_getint(dict, "SBASE:connections_limit", SB_CONN_MAX);
	sbase->usec_sleep = iniparser_getint(dict, "SBASE:usec_sleep", SB_USEC_SLEEP);
	sbase->set_log(sbase, iniparser_getstr(dict, "SBASE:logfile"));
	sbase->set_evlog(sbase, iniparser_getstr(dict, "SBASE:evlogfile"));
	/* XHTTPD */
	if((service = service_init()) == NULL)
	{
		fprintf(stderr, "Initialize service failed, %s", strerror(errno));
		_exit(-1);
	}
	service->family = iniparser_getint(dict, "XHTTPD:inet_family", AF_INET);
	service->sock_type = iniparser_getint(dict, "XHTTPD:socket_type", SOCK_STREAM);
	service->ip = iniparser_getstr(dict, "XHTTPD:service_ip");
	service->port = iniparser_getint(dict, "XHTTPD:service_port", 80);
	service->working_mode = iniparser_getint(dict, "XHTTPD:working_mode", WORKING_PROC);
	service->service_type = iniparser_getint(dict, "XHTTPD:service_type", C_SERVICE);
	service->service_name = iniparser_getstr(dict, "XHTTPD:service_name");
	service->nprocthreads = iniparser_getint(dict, "XHTTPD:nprocthreads", 1);
	service->ndaemons = iniparser_getint(dict, "XHTTPD:ndaemons", 0);
    service->session.packet_type=iniparser_getint(dict, "XHTTPD:packet_type",PACKET_DELIMITER);
    if((service->session.packet_delimiter = iniparser_getstr(dict, "XHTTPD:packet_delimiter")))
    {
        p = s = service->session.packet_delimiter;
        while(*p != 0 )
        {
            if(*p == '\\' && *(p+1) == 'n')
            {
                *s++ = '\n';
                p += 2;
            }
            else if (*p == '\\' && *(p+1) == 'r')
            {
                *s++ = '\r';
                p += 2;
            }
            else
                *s++ = *p++;
        }
        *s++ = 0;
        service->session.packet_delimiter_length = strlen(service->session.packet_delimiter);
    }
	service->session.buffer_size = iniparser_getint(dict, "XHTTPD:buffer_size", SB_BUF_SIZE);
	service->session.packet_reader = &xhttpd_packet_reader;
	service->session.packet_handler = &xhttpd_packet_handler;
	service->session.data_handler = &xhttpd_data_handler;
    service->session.oob_handler = &xhttpd_oob_handler;
    cacert_file = iniparser_getstr(dict, "XHTTPD:cacert_file");
    privkey_file = iniparser_getstr(dict, "XHTTPD:privkey_file");
    if(cacert_file && privkey_file && iniparser_getint(dict, "XHTTPD:is_use_SSL", 0))
    {
        service->is_use_SSL = 1;
        service->cacert_file = cacert_file;
        service->privkey_file = privkey_file;
    }
    //httpd home
    if((p = iniparser_getstr(dict, "XHTTPD:httpd_home")))
        httpd_home = p;
    http_indexes_view = iniparser_getint(dict, "XHTTPD:http_indexes_view", 1);
    if((p = iniparser_getstr(dict, "XHTTPD:httpd_index")))
    {
        memset(http_indexes, 0, sizeof(char *) * HTTP_INDEX_MAX);
        nindexes = 0;
        while(nindexes < HTTP_INDEX_MAX && *p != '\0')
        {
            while(*p == 0x20 || *p == '\t' || *p == ',' || *p == ';')++p;
            if(*p != '\0') 
            {
                http_indexes[nindexes] = p;
                while(*p != '\0' && *p != 0x20 && *p != '\t' 
                        && *p != ',' && *p != ';')++p;
                *p++ = '\0';
                //fprintf(stdout, "%s::%d %d[%s]\n", __FILE__, __LINE__, nindexes, http_indexes[nindexes]);
                ++nindexes;
            }else break;
        }
    }
    if((httpd_compress = iniparser_getint(dict, "XHTTPD:httpd_compress", 0)))
    {
	    if((p =  iniparser_getstr(dict, "XHTTPD:httpd_compress_cachedir")))
		    httpd_compress_cachedir =  p;
	    if(access(httpd_compress_cachedir, F_OK) && mkdir(httpd_compress_cachedir, 0755)) 
	    {
		    fprintf(stderr, "create compress cache dir %s failed, %s\n", 
			httpd_compress_cachedir, strerror(errno));
		    return -1;
	    }
    }
    //name map
    TRIETAB_INIT(namemap);
    if(namemap)
    {
        for(i = 0; i < HTTP_MIME_NUM; i++)
        {
            dp = (void *)((long)(i + 1));
            p = http_mime_types[i].e;
            n = http_mime_types[i].elen;
            TRIETAB_ADD(namemap, p, n, dp);
        }
        if((p = iniparser_getstr(dict, "XHTTPD:httpd_vhosts")))
        {
            memset(httpd_vhosts, 0, sizeof(HTTP_VHOST) * HTTP_VHOST_MAX);
            nvhosts = 0;
            while(nvhosts < HTTP_VHOST_MAX && *p != '\0')
            {
                while(*p != '[') ++p;
                ++p;
                while(*p == 0x20 || *p == '\t' || *p == ',' || *p == ';')++p;
                httpd_vhosts[nvhosts].name = p;
                while(*p != ':' && *p != 0x20 && *p != '\t') ++p;
                *p = '\0';
                if((n = (p - httpd_vhosts[nvhosts].name)) > 0)
                {
                    dp = (void *)((long)(nvhosts + 1));
                    TRIETAB_ADD(namemap, httpd_vhosts[nvhosts].name, n, dp);
                }
                ++p;
                while(*p == 0x20 || *p == '\t' || *p == ',' || *p == ';')++p;
                httpd_vhosts[nvhosts].home = p;
                while(*p != ']' && *p != 0x20 && *p != '\t') ++p;
                *p++ = '\0';
                ++nvhosts;
            }
        }
    }
    if((p = iniparser_getstr(dict, "XHTTPD:logfile")))
    {
        service->set_log(service, p);
    }
	/* server */
	//fprintf(stdout, "Parsing for server...\n");
	return sbase->add_service(sbase, service);
    /*
    if(service->sock_type == SOCK_DGRAM 
            && (p = iniparser_getstr(dict, "XHTTPD:multicast")) && ret == 0)
    {
        ret = service->add_multicast(service, p);
    }
    return ret;
    */
}

int main(int argc, char **argv)
{
    pid_t pid;
    char *conf = NULL, *p = NULL, ch = 0;
    int is_daemon = 0;

    /* get configure file */
    while((ch = getopt(argc, argv, "c:d")) != -1)
    {
        if(ch == 'c') conf = optarg;
        else if(ch == 'd') is_daemon = 1;
    }
    if(conf == NULL)
    {
        fprintf(stderr, "Usage:%s -d -c config_file\n", argv[0]);
        _exit(-1);
    }
    /* locale */
    setlocale(LC_ALL, "C");
    /* signal */
    signal(SIGTERM, &xhttpd_stop);
    signal(SIGINT,  &xhttpd_stop);
    signal(SIGHUP,  &xhttpd_stop);
    signal(SIGPIPE, SIG_IGN);
    //daemon
    if(is_daemon)
    {
        pid = fork();
        switch (pid) {
            case -1:
                perror("fork()");
                exit(EXIT_FAILURE);
                break;
            case 0: //child
                if(setsid() == -1)
                    exit(EXIT_FAILURE);
                break;
            default://parent
                _exit(EXIT_SUCCESS);
                break;
        }
    }
    /*setrlimiter("RLIMIT_NOFILE", RLIMIT_NOFILE, 65536)*/
    if((sbase = sbase_init()) == NULL)
    {
        exit(EXIT_FAILURE);
        return -1;
    }
    fprintf(stdout, "Initializing from configure file:%s\n", conf);
    /* Initialize sbase */
    if(sbase_initialize(sbase, conf) != 0 )
    {
        fprintf(stderr, "Initialize from configure file failed\n");
        exit(EXIT_FAILURE);
        return -1;
    }
    fprintf(stdout, "Initialized successed\n");
    if(service->sock_type == SOCK_DGRAM 
            && (p = iniparser_getstr(dict, "XHTTPD:multicast")))
    {
        if(service->add_multicast(service, p) != 0)
        {
            fprintf(stderr, "add multicast:%s failed, %s", p, strerror(errno));
            exit(EXIT_FAILURE);
            return -1;
        }
        p = "224.1.1.168";
        if(service->add_multicast(service, p) != 0)
        {
            fprintf(stderr, "add multicast:%s failed, %s", p, strerror(errno));
            exit(EXIT_FAILURE);
            return -1;
        }

    }
    sbase->running(sbase, 0);
    //sbase->running(sbase, 3600);
    //sbase->running(sbase, 90000000);sbase->stop(sbase);
    sbase->clean(&sbase);
    if(namemap) TRIETAB_CLEAN(namemap);
    if(dict)iniparser_free(dict);
}
