#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef _MEMB_H_
#define _MEMB_H_
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _MEMB
{
    char *data;
    int  ndata;
    int block_size;
    int size;
    int left;
    char *end;
    void *logger;
}MEMB;
#define MB_BLOCK_SIZE  131072
#define MB(ptr)        ((MEMB *)ptr)
#define MB_SIZE(ptr)   (MB(ptr)->size)
#define MB_BSIZE(ptr)  (MB(ptr)->block_size)
#define MB_LEFT(ptr)   (MB(ptr)->left)
#define MB_DATA(ptr)   (MB(ptr)->data)
#define MB_NDATA(ptr)  (MB(ptr)->ndata)
#define MB_END(ptr)    (MB(ptr)->end)
/* mem buffer initialize */
void *mmb_init(int block_size);
/* set logger */
void mmb_set_logger(void *mmb, void *logger);
/* mem buffer set block size */
void mmb_set_block_size(void *mmb, int block_size);
/* mem buffer receiving  */
int mmb_recv(void *mmb, int fd, int flag);
/* mem buffer reading  */
int mmb_read(void *mmb, int fd);
/* mem buffer reading with SSL */
int mmb_read_SSL(void *mmb, void *ssl);
/* push mem buffer */
int mmb_push(void *mmb, char *data, int ndata);
/* delete mem buffer */
int mmb_del(void *mmb, int ndata);
/* reset mem buffer */
void mmb_reset(void *mmb);
/* clean mem buffer */
void mmb_clean(void *mmb);
#ifdef __cplusplus
 }
#endif
#endif
