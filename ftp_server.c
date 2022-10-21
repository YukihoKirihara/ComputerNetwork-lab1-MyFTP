// Network
#include<unistd.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netdb.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<ctype.h>
#include<errno.h>
#include<malloc.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/ioctl.h>
#include<stdarg.h>
#include<fcntl.h>
// Process
#include<sys/wait.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<signal.h>
// Thread
#include<pthread.h>
#include<sys/poll.h>    // link thread when compiling "-lthread"
#include<sys/time.h>
#include<sys/epoll.h>

#define MAX_INPUT_LENGTH    256     // the maximal length of an input line
#define MAX_CMD_LENGTH      8       // the maximal length of an command name
#define MAX_ARGC_NUM        8       // the maximal number of arguments in a command line
#define MAGIC_NUMBER_LENGTH 6       // Protocol magic number
#define SHORT_MSG_LENGTH    128     // the maximal length of a short message between client and server
#define LONG_MSG_LENGTH     1024    // the maximal length of a long message between client and server
#define HEADER_LENGTH       12      // the length of myFTP header. sizeof(struct myFTP_Header)

// Arguments for the server
#define MY_BACKLOG          8       // the argument for listen()
#define EPOLL_NUM           16      // the number of epoll events.
#define MAX_CLIENT_NUM      8       // the maximal number of clients the server can serve at the same time
#define IP_ADDR_LENGTH      32      // the maximal length of the IP address string
#define MAX_USER_LENGTH     4       // "user"
#define MAX_PASS_LENGTH     6       // "123123"
#define MAX_LS_LENGTH       2400    // the maximal length of list

// Order the command names to print usage instructions.
#define OPEN_ORD        1
#define AUTH_ORD        2
#define LIST_ORD        4
#define GET_ORD         8
#define PUT_ORD         16
#define QUIT_ORD        32

// m_type value of struct myFTP_Header
#define OPEN_CONN_REQUEST       0xA1
#define OPEN_CONN_REPLY         0xA2
#define AUTH_REQUEST            0xA3
#define AUTH_REPLY              0xA4
#define LIST_REQUEST            0xA5
#define LIST_REPLY              0xA6
#define GET_REQUEST             0xA7
#define GET_REPLY               0xA8
#define FILE_DATA               0xFF
#define PUT_REQUEST             0xA9
#define PUT_REPLY               0xAA
#define QUIT_REQUEST            0xAB
#define QUIT_REPLY              0xAC

// m_status value of struct myFTP_Header
#define STATUS_0        0
#define STATUS_1        1
#define STATUS_UNUSED   2

// Client state
#define IDLE            0       // Not connected to any server.
#define CONNECTED       1       // Connected but not authenticated.
#define AUTHENTICATED   2       // Authenticated and ready to communicate.

const char PROTOCOL_NAME[6] = {'\xe3', 'm', 'y', 'f', 't', 'p'};
const char MY_USERNAME[4] = "user";
const char MY_PASSWORD[6] = "123123";

struct myFTP_Header{
    uint8_t m_protocol[MAGIC_NUMBER_LENGTH];    /* protocol magic number (6 bytes) */
    uint8_t m_type;                             /* type (1 byte) */
    uint8_t m_status;                           /* status (1 byte) */
    uint32_t m_length;                          /* length (4 bytes) in Big endian*/
} __attribute__ ((packed));

struct Client{
    int state;
    char ip[IP_ADDR_LENGTH];
    uint16_t port;
    int fd;
    char username[MAX_USER_LENGTH];
    char password[MAX_PASS_LENGTH];
};

struct Client my_clients[MAX_CLIENT_NUM];
int client_num = 0;

int Register(char * his_ip, uint16_t his_port, int curr_fd);
void Open_Reply(struct myFTP_Header * header, int curr_fd, int his_id);
void Auth_Reply(struct myFTP_Header * header, int curr_fd, int his_id);
void List_Reply(struct myFTP_Header * header, int curr_fd, int his_id);
void Get_Reply(struct myFTP_Header * header, int curr_fd, int his_id);
void Put_Reply(struct myFTP_Header * header, int curr_fd, int his_id);
void File_Recv(struct myFTP_Header * header, int curr_fd, int his_id);
void Quit_Reply(struct myFTP_Header * header, int curr_fd, int his_id);
uint32_t Small2Big_Endian(uint32_t num);
uint32_t Big2Small_Endian(uint32_t num);

uint32_t Small2Big_Endian(uint32_t num) {
    return ((num & 0x000000ff) << 24 ) | ((num & 0x0000ff00) << 8) | ((num & 0x00ff0000) >> 8) | ((num & 0xff000000 ) >> 24);
}
uint32_t Big2Small_Endian(uint32_t num) {
    return ((num & 0x000000ff) << 24 ) | ((num & 0x0000ff00) << 8) | ((num & 0x00ff0000) >> 8) | ((num & 0xff000000 ) >> 24);
}

/*
    Register
    When a client is connected to the server, find whether it has registered. 
    If yes, checks if it is in the state IDLE. 
        If yes, changes the state to CONNECTED. 
        Otherwise, reject the connection with return value -1.
    Otherwise, add it to the client list and do the initialization.
    Return the client_id of this client.
*/
int Register(char * his_ip, uint16_t his_port, int curr_fd) {
    for (int i=0; i<MAX_CLIENT_NUM; i++) 
        //if (my_clients[i].ip != NULL) 
        if (strcmp(his_ip, my_clients[i].ip) == 0 && his_port == my_clients[i].port) {
            if (my_clients[i].state != IDLE) 
                return -1;
            else {
                my_clients[i].state != CONNECTED;
                my_clients[i].fd = curr_fd;
                return i;
            }
        }

    my_clients[client_num].state = CONNECTED;
    for (int i=0; i<strlen(his_ip); i++) 
        my_clients[client_num].ip[i] = his_ip[i];
    my_clients[client_num].port = his_port;
    my_clients[client_num].fd = curr_fd;
    for (int i=0; i<MAX_USER_LENGTH; i++) 
        my_clients[client_num].username[i] = MY_USERNAME[i];
    for (int i=0; i<MAX_PASS_LENGTH; i++) 
        my_clients[client_num].password[i] = MY_PASSWORD[i];
    client_num++;
    return client_num-1;
}

/*
    Open_Reply
    Accept the request with a "OPEN_CONN_REPLY" message.
    No reason to reject, since the actual "connect" is made just now in Register().
*/
void Open_Reply(struct myFTP_Header * header, int curr_fd, int his_id) {
    struct myFTP_Header * open_conn_reply;
    char * buf_send;
    open_conn_reply = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    for (int i=0; i<6; i++) 
        open_conn_reply->m_protocol[i] = PROTOCOL_NAME[i];
    open_conn_reply->m_type = OPEN_CONN_REPLY;
    open_conn_reply->m_status = STATUS_1;
    open_conn_reply->m_length = htonl(HEADER_LENGTH);
    buf_send = (char *) malloc(SHORT_MSG_LENGTH);
    for (int i=0; i<HEADER_LENGTH; i++) 
        buf_send[i] = *((char *)(open_conn_reply)+i);
    size_t ret = 0, send_cnt, send_len = HEADER_LENGTH;
    while (ret < send_len) {
        send_cnt = send(curr_fd, buf_send+ret, send_len-ret, 0);
        if (send_cnt == 0) printf("Socket closed.\n");
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }
    free(open_conn_reply);
    open_conn_reply = NULL;
    free(buf_send);
    buf_send = NULL;
}

/*
    Auth_Reply
    Receive the payload,
    Compare with the username and password of this client,
    Reply with a "AUTH_REPLY" message.
*/
void Auth_Reply(struct myFTP_Header * header, int curr_fd, int his_id) {
    struct myFTP_Header * auth_reply;
    char * buf_recv;
    char * buf_send;
    int pld_len = ntohl(header->m_length)-HEADER_LENGTH;
    buf_recv = (char *) malloc(pld_len*(sizeof(char)));
    size_t ret = 0, recv_cnt;
    while (ret < pld_len) {
        recv_cnt = recv(curr_fd, buf_recv+ret, pld_len-ret, 0);
        if (recv_cnt < 0) {
            printf("Error: Socket receive error.\n");
            exit(1);
        }
        // if (recv_cnt == 0) {
        //     printf("Client %d closed.\n", his_id);
        //     my_clients[his_id].state = IDLE;
        //     close(curr_fd);
        //     epoll_ctl(epfd, EPOLL_CTL_DEL, evts[i].data.fd, NULL);        
        // }
        ret+=recv_cnt;
    }
    int flag=1;
    for (int i=0; i<MAX_USER_LENGTH; i++) 
        if (buf_recv[i] != my_clients[his_id].username[i]) {
            flag=0;
            break;
        }
    if (flag) 
    for (int i=0; i<MAX_PASS_LENGTH; i++) 
        if (buf_recv[i+MAX_USER_LENGTH+1] != my_clients[his_id].password[i]) {
            flag=0;
            break;
        }
    auth_reply = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    for (int i=0; i<6; i++) 
        auth_reply->m_protocol[i] = PROTOCOL_NAME[i];
    auth_reply->m_type = AUTH_REPLY;
    auth_reply->m_status = flag? STATUS_1: STATUS_0;
    auth_reply->m_length = htonl(HEADER_LENGTH);
    buf_send = (char *) malloc(SHORT_MSG_LENGTH);
    for (int i=0; i<HEADER_LENGTH; i++)
        buf_send[i] = *((char *)(auth_reply)+i);
    ret = 0;
    size_t send_cnt, send_len = HEADER_LENGTH;
    while (ret < send_len) {
        send_cnt = send(curr_fd, buf_send+ret, send_len-ret, 0);
        if (send_cnt == 0) printf("Socket closed.\n");
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }
    if (flag) 
        my_clients[his_id].state = AUTHENTICATED;

    free(auth_reply);
    auth_reply = NULL;
    free(buf_recv);
    buf_recv = NULL;
    free(buf_send);
    buf_send = NULL;
    return;
}

/*
    List_Reply
*/
void List_Reply(struct myFTP_Header * header, int curr_fd, int his_id) {
    if (my_clients[his_id].state != AUTHENTICATED) {
        // ?
        return;
    }
    
    struct myFTP_Header * list_reply;
    char * buf_send;
    char file_list[MAX_LS_LENGTH];
    int ls_len;
    FILE * fp = NULL;
    char command[SHORT_MSG_LENGTH];
    int tmp;
    snprintf(command, sizeof(command), "ls\n");
    fp = popen(command, "r");   
    if (fp == NULL) {
        printf("Error: popen ls failure.\n");
        return;
    }
    if ((tmp = fread(file_list, 1, sizeof(file_list), fp)) < 0) {
        printf("Error: Read from I/O stream file pointer failure.\n");
        printf("Error details: %s",strerror(errno));
        return;
    }
    tmp = fclose(fp);
    if (tmp == -1) {
        printf("Error: Close I/O stream file pointer failure.\n");
        printf("Error details: %s",strerror(errno));
    }
    ls_len = strlen(file_list);
    file_list[ls_len-1] = '\n';
    file_list[ls_len++] = '\0';
    printf("ls_len = %d\n",ls_len);
    
    // printf("%c,%c,%c\n",file_list[ls_len-2], file_list[ls_len-1], file_list[ls_len]);

    list_reply = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    for (int i=0; i<6; i++) 
        list_reply->m_protocol[i] = PROTOCOL_NAME[i];
    list_reply->m_type = LIST_REPLY;
    list_reply->m_status = STATUS_1;
    list_reply->m_length = htonl(HEADER_LENGTH+ls_len);
    buf_send = (char *) malloc(LONG_MSG_LENGTH*sizeof(char));
    for (int i=0; i<HEADER_LENGTH; i++)
        buf_send[i] = *((char *)(list_reply)+i);
    size_t ret = 0, send_cnt, send_len = HEADER_LENGTH;
    while (ret < send_len) {
        send_cnt = send(curr_fd, buf_send+ret, send_len-ret, 0);
        if (send_cnt == 0) printf("Socket closed.\n");
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }
    // printf("send_len = %d\n",send_len);
    int cut=0;
    while (ls_len > LONG_MSG_LENGTH) {
        ret = 0;
        send_len = LONG_MSG_LENGTH;
        for (int i=0; i<LONG_MSG_LENGTH; i++)
            buf_send[i] = *((char *)(file_list)+i+cut*LONG_MSG_LENGTH);
        while (ret < send_len) {
            send_cnt = send(curr_fd, buf_send+ret, send_len-ret, 0);
            if (send_cnt == 0) printf("Socket closed.\n");
            if (send_cnt < 0) {
                printf("Error: Socket send error.\n");
                exit(1);
            }
            ret += send_cnt;
        }
        cut++;
        ls_len -= LONG_MSG_LENGTH;
        // printf("send_len = %d\n",send_len);
    }
    // ls_len <= LONG_MSG_LENGTH
    ret = 0;
    send_len = ls_len;
    for (int i=0; i<send_len; i++)
        buf_send[i] = *((char *)(file_list)+i+cut*LONG_MSG_LENGTH);
    while (ret < send_len) {
        send_cnt = send(curr_fd, buf_send+ret, send_len-ret, 0);
        if (send_cnt == 0) printf("Socket closed.\n");
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }
    // printf("send_len = %d\n",send_len);
   
    free(list_reply);
    list_reply = NULL;
    free(buf_send);
    buf_send = NULL;
    return;
}

/*
    Get_Reply
*/
void Get_Reply(struct myFTP_Header * header, int curr_fd, int his_id) {
    if (my_clients[his_id].state != AUTHENTICATED) {
        // ?
        return;
    }

    char * filename;
    int file_name_len = ntohl(header->m_length)-HEADER_LENGTH;
    filename = (char *) malloc(file_name_len);
    size_t ret = 0, recv_cnt;
    while (ret < file_name_len) {
        recv_cnt = recv(curr_fd, filename+ret, file_name_len-ret, 0);
        if (recv_cnt < 0) {
            printf("Error: Socket receive error.\n");
            exit(1);
        }
        ret += recv_cnt;
    }
    // printf("%s\n",filename);
    filename[--file_name_len] = '\0';

    int file_access = access(filename, R_OK);
    FILE * localfile = fopen(filename, "r");
    char * pld_file = NULL;
    int pld_file_len;
    int some_waste;
    if (localfile == NULL) {
        printf("Error: Can not open file %s.\n", filename);
        file_access = -1;
    }
    else {
        fseek(localfile, 0, SEEK_END);
        pld_file_len = ftell(localfile);
        fseek(localfile, 0, SEEK_SET);
        //pld_file = (char *) malloc(pld_file_len*(sizeof(char)));
        pld_file = (char *) malloc(LONG_MSG_LENGTH);
        some_waste = fread(pld_file, 1, 1, localfile);
        // fclose(localfile);
        // localfile = NULL;
        if (some_waste != 1) 
            file_access = -1;
        fseek(localfile, 0, SEEK_SET);
    }
    
    struct myFTP_Header * get_reply;
    char * buf_send;
    size_t send_cnt, send_len;
    get_reply = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    buf_send = (char *) malloc(SHORT_MSG_LENGTH);
    for (int i=0; i<6; i++) 
        get_reply->m_protocol[i] = PROTOCOL_NAME[i];
    get_reply->m_type = GET_REPLY;
    get_reply->m_status = (file_access == -1) ? STATUS_0 : STATUS_1;
    get_reply->m_length = htonl(HEADER_LENGTH);
    send_len = HEADER_LENGTH;
    for (int i=0; i<send_len; i++) 
        buf_send[i] = *((char *)(get_reply)+i);
    ret = 0;
    while (ret < send_len) {
        send_cnt = send(curr_fd, buf_send+ret, send_len-ret, 0);
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }   

    if (file_access == -1) {
        free(filename);
        filename = NULL;
        free(get_reply);
        get_reply = NULL;
        free(buf_send);
        buf_send = NULL;
        if (localfile != NULL)
            fclose(localfile);
        if (pld_file != NULL)
            free(pld_file);
        pld_file = NULL;
        return;
    }

    struct myFTP_Header * file_data;    
    file_data = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    for (int i=0; i<6; i++) 
        file_data->m_protocol[i] = PROTOCOL_NAME[i];
    file_data->m_type = FILE_DATA;
    file_data->m_status = STATUS_UNUSED;
    file_data->m_length = htonl(pld_file_len+HEADER_LENGTH);
    for (int i=0; i<HEADER_LENGTH; i++) 
        buf_send[i] = *((char *)(file_data)+i);
    ret = 0;
    send_len = HEADER_LENGTH;
    while (ret < send_len) {
        send_cnt = send(curr_fd, buf_send+ret, send_len-ret, 0);
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }
    int cut = 0;
    while (pld_file_len > LONG_MSG_LENGTH) {
        ret = 0;
        send_len = LONG_MSG_LENGTH;
        some_waste = fread(pld_file, send_len, 1, localfile);
        while (ret < send_len) {
            // send_cnt = send(curr_fd, pld_file+cut*LONG_MSG_LENGTH+ret, LONG_MSG_LENGTH-ret, 0);
            send_cnt = send(curr_fd, pld_file+ret, send_len-ret, 0);
            if (send_cnt < 0) {
                printf("Error: Socket send error.\n");
                exit(1);
            }
            ret += send_cnt;
        }
        //printf("ret = %d\n", ret);
        cut++;
        pld_file_len -= LONG_MSG_LENGTH;
    }
    send_len = pld_file_len;
    some_waste = fread(pld_file, send_len, 1, localfile);
    ret = 0;
    while (ret < send_len) {
        // send_cnt = send(curr_fd, pld_file+cut*LONG_MSG_LENGTH+ret, send_len-ret, 0);
        send_cnt = send(curr_fd, pld_file+ret, send_len-ret, 0);
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }
    // printf("ret = %d\n", ret);

    fclose(localfile);
    free(filename);
    filename = NULL;
    free(get_reply);
    get_reply = NULL;
    free(buf_send);
    buf_send = NULL;
    if (pld_file != NULL)
        free(pld_file);
    pld_file = NULL;
    free(file_data);
    file_data = NULL;
    return;
}

/*
    Put_Reply
*/
void Put_Reply(struct myFTP_Header * header, int curr_fd, int his_id) {
    if (my_clients[his_id].state != AUTHENTICATED) {
        // ?
        return;
    }

    char * filename;
    int file_name_len = ntohl(header->m_length)-HEADER_LENGTH;
    filename = (char *) malloc(file_name_len);
    size_t ret = 0, recv_cnt;
    while (ret < file_name_len) {
        recv_cnt = recv(curr_fd, filename+ret, file_name_len-ret, 0);
        if (recv_cnt < 0) {
            printf("Error: Socket receive error.\n");
            exit(1);
        }
        ret += recv_cnt;
    }
    // printf("%s\n",filename);
    filename[--file_name_len] = '\0';
   
    struct myFTP_Header * put_reply;
    char * buf_send;
    put_reply = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    buf_send = (char *) malloc(SHORT_MSG_LENGTH);
    for (int i=0; i<6; i++) 
        put_reply->m_protocol[i] = PROTOCOL_NAME[i];
    put_reply->m_type = PUT_REPLY;
    put_reply->m_status = STATUS_UNUSED;
    put_reply->m_length = htonl(HEADER_LENGTH);
    size_t send_len = HEADER_LENGTH, send_cnt;
    for (int i=0; i<send_len; i++) 
        buf_send[i] = *((char *)(put_reply)+i);
    ret = 0;
    while (ret < send_len) {
        send_cnt = send(curr_fd, buf_send+ret, send_len-ret, 0);
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }  
    free(put_reply);
    put_reply = NULL;
    free(buf_send);
    buf_send = NULL;

    struct myFTP_Header * file_data;
    char * buf_recv;
    file_data = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    buf_recv = (char *) malloc(LONG_MSG_LENGTH);
    ret = 0;
    size_t recv_len = HEADER_LENGTH;
    while (ret < recv_len) {
        recv_cnt = recv(curr_fd, buf_recv+ret, recv_len-ret, 0);
        if (recv_cnt < 0) {
            printf("Error: Socket receive error.\n");
            exit(1);
        }
        ret += recv_cnt;
    }
    for (int i=0; i<HEADER_LENGTH; i++) {
        *((unsigned char *)(file_data) + i) = buf_recv[i];
    }
    
    int file_len = ntohl(file_data->m_length)-HEADER_LENGTH;
    // printf("file_len = %d\n",file_len);
    FILE * localfile = fopen(filename, "w+");
    int some_waste;
    if (localfile == NULL) {
        printf("Error: Can not open file %s.\n", filename);
        printf("Error details: %s\n", strerror(errno));
    }
    int cut = 0;
    while (file_len > LONG_MSG_LENGTH) {
        recv_len = LONG_MSG_LENGTH;
        ret = 0;
        while (ret < recv_len) {
            recv_cnt = recv(curr_fd, buf_recv+ret, recv_len-ret, 0);
            if (recv_cnt < 0) {
                printf("Error: Socket receive error.\n");
                exit(1);
            }
            ret += recv_cnt;
        }
        // printf("recv_len = %d\n",recv_len);
        cut++;
        file_len-=recv_len;
        some_waste = fwrite(buf_recv, recv_len, 1, localfile);

        //printf("%s\n",buf_recv);

        if (some_waste != 1) {
            printf("Error: Can not write file %s.\n", filename);
            printf("Error details: %s\n", strerror(errno));
        }
    }
    recv_len = file_len;
    ret = 0;
    while (ret < recv_len) {
        recv_cnt = recv(curr_fd, buf_recv+ret, recv_len-ret, 0);
        if (recv_cnt < 0) {
            printf("Error: Socket receive error.\n");
            exit(1);
        }
        ret += recv_cnt;
    }
    // printf("recv_len = %d\n",recv_len);
    // printf("%s\n",buf_recv);
    some_waste = fwrite(buf_recv, recv_len, 1, localfile);
    if (some_waste != 1) {
        printf("Error: Can not write file %s.\n", filename);
        printf("Error details: %s\n", strerror(errno));
    }
    fclose(localfile);
    
    free(filename);
    filename = NULL;
    free(file_data);
    file_data = NULL;
    free(buf_recv);
    buf_recv = NULL;

    return;
}

void File_Recv(struct myFTP_Header * header, int curr_fd, int his_id) {
    // ?
    return;
}

/*
    Quit_Reply
*/
void Quit_Reply(struct myFTP_Header * header, int curr_fd, int his_id) {
    struct myFTP_Header * quit_reply;
    quit_reply = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    for (int i=0; i<6; i++) 
        quit_reply->m_protocol[i] = PROTOCOL_NAME[i];
    quit_reply->m_type = QUIT_REPLY;
    quit_reply->m_status = STATUS_UNUSED;
    quit_reply->m_length = htonl(HEADER_LENGTH);
    char buf_send[SHORT_MSG_LENGTH], send_len = HEADER_LENGTH;
    for (int i=0; i<HEADER_LENGTH; i++) 
        buf_send[i] = *((char *)(quit_reply)+i);  
    size_t ret = 0, send_cnt;
    while (ret < send_len) {
        send_cnt = send(curr_fd, buf_send+ret, send_len-ret, 0);
        if (send_cnt == 0) printf("Socket closed.\n");
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }
    my_clients[his_id].state = IDLE;
    free(quit_reply);
    quit_reply = NULL;
    return;
}

int main(int argc, char ** argv) {
    if (argc != 3) {
        printf("Error: The server requires 2 arguments.\nUsage: <Path> <Host> <Port>.\n");
        return 0;
    }

    int tmp = (uint16_t) atoi((const char *) argv[2]);
    uint16_t my_port;
    char * my_ip;
    if (tmp == 0) {
        printf("Error: Failed to decode the port. The port should be an integer in [0,65535]\n");
        exit(1);
    }
    if (tmp<0 || tmp>65535) {
        printf("Error: Invalid port number. The port should be an integer in [0,65535].\n");
        exit(1);
    }
    
    my_port = (uint16_t)tmp;
    my_ip = (char *)malloc(strlen(argv[1])*sizeof(char));   
    for (int i=0; i<strlen(argv[1]); i++) 
        my_ip[i] = argv[1][i];

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);    // Apply for a TCP socket.
    struct sockaddr_in server_addr;                     // Descriptor of the address listening to.
    if (listen_fd == -1) {
        printf("Error: Failed to build a listen socket.\n");
        free(my_ip);
        my_ip = NULL;
        exit(1);
    }
    server_addr.sin_port = htons(my_port);      
    server_addr.sin_family = AF_INET;
    tmp = inet_pton(AF_INET, my_ip, &server_addr.sin_addr);
    if (tmp == 0) {
        printf("Error: Invalid IP address. The server address should be a IPv4 address in dotted decimal notation.\n");
        close(listen_fd);
        free(my_ip);
        my_ip = NULL;
        exit(1);
    }
    else if (tmp == -1) {
        printf("Error: Failed to set IP address.\n");
        printf("Error details: %s\n", strerror(errno));
        close(listen_fd);
        free(my_ip);
        my_ip = NULL;
        exit(1);
    }
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        printf("Error: Failed to bind socket address and the descriptor.\n");
        printf("Error details: %s\n", strerror(errno));
        close(listen_fd);
        free(my_ip);
        my_ip = NULL;
        exit(1);
    }
    if (listen(listen_fd, MY_BACKLOG) != 0) {
        printf("Error: Failed to listening socket.\n");
        printf("Error details: %s\n", strerror(errno));
        close(listen_fd);
        free(my_ip);
        my_ip = NULL;
        exit(1);
    }

    // Epoll
    /*
    struct epoll_event {
    uint32_t events;    // Epoll events 
    epoll_data_t data;    // User data variable 
    };
    typedef union epoll_data {
        void *ptr;
        int fd;
        uint32_t u32;
        uint64_t u64;
    } epoll_data_t;
    */
    int epfd = epoll_create(128);   // 128 is a meaningless argument.
    struct epoll_event evt;
    evt.events = EPOLLIN;           // a socket is available to read.
    evt.data.fd = listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &evt);

    struct epoll_event evts[EPOLL_NUM]; 

    while (1) {
        int eve_num = epoll_wait(epfd, evts, EPOLL_NUM, -1);
        if (eve_num == -1) {
            printf("Error: Epoll Error.\n");
            printf("Error details: %s\n", strerror(errno));
            close(listen_fd);
            free(my_ip);
            my_ip = NULL;
            exit(1);
        }
        for (int i=0; i<eve_num; i ++) {
            if (evts[i].data.fd == listen_fd) {   // Accept a new connection
                struct sockaddr_in curr_addr;
                int len = sizeof(curr_addr);
                int curr_fd = accept(listen_fd, (struct sockaddr *)&curr_addr, &len);
                char his_ip[IP_ADDR_LENGTH];
                inet_ntop(AF_INET, &curr_addr.sin_addr.s_addr, his_ip, sizeof(his_ip));
                uint16_t his_port = ntohs(curr_addr.sin_port);
                int his_id = Register(his_ip, his_port, curr_fd);
                if (his_id != -1)   // A new client connection
                    printf("A new client connection from (IP = %s, Port = %d) succeeds, whose client id is %d.\n", his_ip, his_port, his_id);
                evt.events = EPOLLIN;
                evt.data.fd = curr_fd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, curr_fd, &evt);

                struct myFTP_Header * request;
                char buf_recv[SHORT_MSG_LENGTH];
                request = (struct myFTP_Header *) malloc(HEADER_LENGTH);
                size_t recv_cnt;
                recv_cnt = recv(curr_fd, buf_recv, HEADER_LENGTH, 0);
                if (recv_cnt < 0) {
                    printf("Error: Socket receive error.\n");
                    exit(1);
                }
                else if (recv_cnt == 0) {
                    printf("Client %d closed1.\n", his_id);
                    my_clients[his_id].state = IDLE;
                    close(curr_fd);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, evts[i].data.fd, NULL);
                }
                else {
                    for (int i=0; i<HEADER_LENGTH; i++) {
                        *((unsigned char *)(request)+i) = buf_recv[i];
                        // printf("%d %02x\n",i,buf_recv[i]);        
                    }  
                    if (request->m_type == OPEN_CONN_REQUEST) {
                        Open_Reply(request, curr_fd, his_id);
                    } 
                }
            }
            else {
                if (evts[i].events & EPOLLOUT) {    // A connected port is available to write
                    continue;
                }
                if (evts[i].events & EPOLLIN) {      // A connected port is available to read
                    // recv() is safe here without blocking.
                    // printf("this is eve[%d]\n",i);
                    struct myFTP_Header * request;
                    char buf_recv[SHORT_MSG_LENGTH];
                    size_t recv_cnt;
                    int curr_fd = evts[i].data.fd;
                    int his_id;
                    for (his_id = 0; his_id < MAX_CLIENT_NUM; his_id++) 
                        if (my_clients[his_id].fd == curr_fd) 
                            break;
                    request = (struct myFTP_Header *) malloc(HEADER_LENGTH);
                    recv_cnt = recv(curr_fd, buf_recv, HEADER_LENGTH, 0);
                    if (recv_cnt < 0) {
                        printf("Error: Socket receive error.\n");
                        exit(1);
                    }
                    else if (recv_cnt == 0) {
                        printf("Client %d closed2.\n", his_id);
                        my_clients[his_id].state = IDLE;
                        close(curr_fd);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, evts[i].data.fd, NULL);
                    }
                    else {
                        for (int i=0; i<HEADER_LENGTH; i++) {
                            *((unsigned char *)(request)+i) = buf_recv[i];
                            //printf("%d %02x\n",i,buf_recv[i]);        
                        }  
                        // Now, dispatch the task according to the header's m_type
                        if (request->m_type == OPEN_CONN_REQUEST) {
                            Open_Reply(request, curr_fd, his_id);
                            printf("Open Reply finished.\n");
                        } 
                        else if (request->m_type == AUTH_REQUEST) {
                            Auth_Reply(request, curr_fd, his_id);
                            printf("Auth Reply finished.\n");
                        }
                        else if (request->m_type == LIST_REQUEST) {
                            List_Reply(request, curr_fd, his_id);
                            printf("List Reply finished.\n");
                        }
                        else if (request->m_type == GET_REQUEST) {
                            Get_Reply(request, curr_fd, his_id);
                            printf("Get Reply finished.\n");
                        }
                        else if (request->m_type == PUT_REQUEST) {
                            Put_Reply(request, curr_fd, his_id);
                            printf("Put Reply finished.\n");
                        }
                        else if (request->m_type == FILE_DATA) {
                            File_Recv(request, curr_fd, his_id);
                            printf("File Recv finished.\n");
                        }
                        else if (request->m_type == QUIT_REQUEST) {
                            Quit_Reply(request, curr_fd, his_id);
                            close(curr_fd);
                            epoll_ctl(epfd, EPOLL_CTL_DEL, evts[i].data.fd, NULL);
                            printf("Quit Reply finished.\n");
                        }
                        else {
                            printf("An unknown message is received.\n");
                        }
                    }
                    free(request);
                    request = NULL;
                }
            }
        }
    }

    close(listen_fd);    
    free(my_ip);
    my_ip = NULL;

    return 0;
}