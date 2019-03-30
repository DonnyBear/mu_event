#include <unistd.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "connection.h"
#include "event_loop.h"
#include "logger.h"
#include "event.h"
#include "buffer.h"

#define mu_malloc malloc
#define mu_free   free

static void handle_close(connection* conn);

static void event_readable_callback(int fd, event* ev, void* arg)
{
    connection* conn = (connection*)arg;
    int size = 0;
    if (ioctl(fd, FIONREAD, &size) < 0)
		debug_sys("file: %s, line: %d", __FILE__, __LINE__);	/* exit */

    char* buf = (char*)mu_malloc(size);
    int closing = 0;
    do  {
        ssize_t	real_n = read(fd, buf, size);
        if (real_n == 0)  { 
            closing = 1; 
            handle_close(conn);
            break;
        }
        else if (real_n < 0)  {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            debug_sys("file: %s, line: %d", __FILE__, __LINE__);
        }

        push_buffer(conn->buf_socket_read, buf, real_n);
        size -= real_n;
        mu_free(buf);
    } 
    while(size > 0);
    if (closing == 0)  {
        conn->message_callback(conn);
    }
}

static void handle_close(connection* conn)
{
    connection_free(conn);    //如不关闭一直会触发
    printf("closed!!! \n");
}

static void event_writable_callback(int fd, event* ev, void* arg)
{
    connection* conn = (connection*)arg;

    int size = 0;
    char* msg = readall(conn->buf_socket_write, &size);
    if (size > 0)  {
        int n = send(conn->fd, msg, size, 0);
    }
    int left = get_buffer_size(conn->buf_socket_write);
    if (left == 0)  {        //缓存区数据已全部发送，则关闭发送消息
        event_disable_writing(conn->conn_event);
    }
    //printf("write buf is %d !!! \n", size);
}

connection* connection_create(event_loop* loop, int connfd, message_callback_pt msg_cb)
{
    connection* conn = (connection* )mu_malloc(sizeof(connection));
    if (conn == NULL)  {
        debug_ret("file: %s, line: %d", __FILE__, __LINE__);
        return NULL;
    }

    conn->fd = connfd;
    conn->message_callback = msg_cb;

    event* ev = (event*)event_create(connfd,  EPOLLIN | EPOLLPRI, event_readable_callback, 
                            conn, event_writable_callback, conn);
    if (ev == NULL)  {
        debug_ret("file: %s, line: %d", __FILE__, __LINE__);
        mu_free(conn);
        return NULL;
    }

    conn->conn_event = ev;
    event_add_io(loop->epoll_fd, ev);
    conn->buf_socket_read  = socket_buffer_new();
    conn->buf_socket_write = socket_buffer_new();

    return conn;    
}

void connection_free(connection* conn)
{
    event_free(conn->conn_event);
    socket_buffer_free(conn->buf_socket_read);
    socket_buffer_free(conn->buf_socket_write);
    mu_free(conn);
}


void connection_send(connection *conn, char *buf, size_t len)
{
    //printf("size in write is %d\n", conn->buf_socket_write->size);
    if (conn->buf_socket_write->size == 0)  {     //缓冲区为空直接发送
        send(conn->fd, buf, len, 0);
    }
    else  {
        push_buffer(conn->buf_socket_write, buf, len);
        event_enable_writing(conn->conn_event);
    }
}