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

struct myFTP_Header{
    uint8_t m_protocol[MAGIC_NUMBER_LENGTH];    /* protocol magic number (6 bytes) */
    uint8_t m_type;                             /* type (1 byte) */
    uint8_t m_status;                           /* status (1 byte) */
    uint32_t m_length;                          /* length (4 bytes) in Big endian*/
} __attribute__ ((packed));

void Init(void);
void Idle(void);
void Open(char * ip, uint16_t port);
void Auth(char * user, char * password);
void List(void);
void Get(char * filename);
void Put(char * filename);
void Quit(void);
void Print_Usage(int flag);
uint32_t Small2Big_Endian(uint32_t num);
uint32_t Big2Small_Endian(uint32_t num);

// Begin.
int curr_clientfd;
struct sockaddr_in curr_addr;
int client_state = IDLE;

uint32_t Small2Big_Endian(uint32_t num) {
    return ((num & 0x000000ff) << 24 ) | ((num & 0x0000ff00) << 8) | ((num & 0x00ff0000) >> 8) | ((num & 0xff000000 ) >> 24);
}
uint32_t Big2Small_Endian(uint32_t num) {
    return ((num & 0x000000ff) << 24 ) | ((num & 0x0000ff00) << 8) | ((num & 0x00ff0000) >> 8) | ((num & 0xff000000 ) >> 24);
}
/*
    Open
    1. When the user run the client, and input "open SERVER_IP SERVER_PORT",
        a TCP connection should be connected to the server's IP address and port.
    2. Next, the client send a protocol message "OPEN_CONN_REQUEST" to the server,
        so as to request open a connection.
    3. If the server is running (it is listening to the port), 
        it replies with a protocol message "OPEN_CONN_REPLY".
        Otherwise, ? 
        (Temporarily, it will stuck on "recv").
    4. The client receives the reply. 
        If status == 1, then the connection is open.
*/
void Open(char * ip, uint16_t port) {
    int clientfd;
    struct sockaddr_in addr;
    int some_waste;
    
    clientfd = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &addr.sin_addr);
    connect(clientfd, (struct sockaddr*)&addr, sizeof(addr));
    
    struct myFTP_Header * open_conn_request;
    open_conn_request = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    for (int i=0; i<6; i++)
        open_conn_request->m_protocol[i] = PROTOCOL_NAME[i];
    open_conn_request->m_type = OPEN_CONN_REQUEST;
    open_conn_request->m_status = STATUS_UNUSED;
    open_conn_request->m_length = Small2Big_Endian(HEADER_LENGTH);    // 6+1+1+4 = 12, only a header is included.
    char buf_send[SHORT_MSG_LENGTH];
    for (int i=0; i<HEADER_LENGTH; i++) 
        buf_send[i] = *((char *)(open_conn_request)+i);  
    // for (int i=0; i<HEADER_LENGTH; i++) {
    //     printf("%d %02x\n",i, (unsigned char)buf_send[i]);
    // }
    // Safe send
    size_t ret = 0, len = 12, send_cnt;
    while (ret < len) {
        send_cnt = send(clientfd, buf_send+ret, len-ret, 0);
        if (send_cnt == 0) printf("Socket closed.\n");
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }    
    // Unsafe send
    //send(clientfd, buf_send, strlen(buf_send), 0);

    struct myFTP_Header * open_conn_reply;
    open_conn_reply = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    char buf_recv[SHORT_MSG_LENGTH];
    // Safe recv
    size_t cnt = 0, recv_cnt = 1, read_more = 0;
    recv_cnt = recv(clientfd, buf_recv+cnt, SHORT_MSG_LENGTH, 0);
    if (recv_cnt < 0) {
        printf("Error: Socket receive error.\n");
        exit(1);
    }
    cnt += recv_cnt;
    if (cnt == SHORT_MSG_LENGTH) // Probably the message is more than what is received.
        read_more = 1;  // Further receive ?
    // Unsafe recv
    //recv(clientfd, buf_recv, SHORT_MSG_LENGTH, 0);
    for (int i=0; i<HEADER_LENGTH; i++) {
        *((unsigned char *)(open_conn_reply)+i) = buf_recv[i]; 
        //printf("%d %02x\n",i, *((unsigned char*)open_conn_reply + i));
    }
    if (open_conn_reply->m_type == OPEN_CONN_REPLY && open_conn_reply->m_status == STATUS_1) {
        curr_clientfd = clientfd;
        curr_addr = addr;
        client_state = CONNECTED;
        printf("Server connection accepted.\n");
    }

    free(open_conn_request);
    open_conn_request = NULL;
    free(open_conn_reply);
    open_conn_reply = NULL;
    return;
}

/*
    Auth
    1. The client send a "AUTH_REQUEST" message to the server, including username and password.
    2. If authentication succeed, the server reply a "AUTH_REPLY" with m_status == 1; 
        Otherwise, with m_status == 0. 
    3. If m_status == 0, the client closes the connection.

*/
void Auth(char * user, char * password){
    if (client_state == IDLE) {
        printf("ERROR: No connection. Please connect to a server.\n");
        Print_Usage(1);
        return;
    }
    if (client_state == AUTHENTICATED) {
        printf("ERROR: Authentication already granted.\n");
        return;
    }
    
    char * payload;
    int pld_len = 0;
    struct myFTP_Header * auth_request;
    auth_request = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    payload = (char *) malloc(SHORT_MSG_LENGTH*sizeof(char));
    for (int i=0; i<strlen(user); i++) 
        payload[pld_len++] = user[i];
    payload[pld_len++] = ' ';
    for (int i=0; i<strlen(password); i++) 
        payload[pld_len++] = password[i];
    payload[pld_len++] = '\0';
    for (int i=0; i<6; i++) 
        auth_request->m_protocol[i] = PROTOCOL_NAME[i];
    auth_request->m_type = AUTH_REQUEST;
    auth_request->m_status = STATUS_UNUSED;
    auth_request->m_length = Small2Big_Endian(HEADER_LENGTH+pld_len);
    char buf_send[SHORT_MSG_LENGTH<<1];
    int send_len = 0;
    for (send_len=0; send_len<HEADER_LENGTH; send_len++) 
        buf_send[send_len] = *((char *)(auth_request)+send_len);
    for (int i=0; i<pld_len; i++)
        buf_send[send_len++] = payload[i];
    // for (int i=0;i<send_len;i++) 
    //     printf("%d %02x\n",i,buf_send[i]);
    // Safe send
    size_t ret = 0, send_cnt;
    while (ret < send_len) {
        send_cnt = send(curr_clientfd, buf_send+ret, send_len-ret, 0);
        if (send_cnt == 0) printf("Socket closed.\n");
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }

    struct myFTP_Header * auth_reply;
    auth_reply = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    char buf_recv[SHORT_MSG_LENGTH];
    size_t cnt = 0, recv_cnt = 1, read_more = 0;
    recv_cnt = recv(curr_clientfd, buf_recv+cnt, SHORT_MSG_LENGTH, 0);
    if (recv_cnt < 0) {
        printf("Error: Socket receive error.\n");
        exit(1);
    }
    cnt += recv_cnt;
    if (cnt == SHORT_MSG_LENGTH) // Probably the message is more than what is received.
        read_more = 1;  // Further receive ?
    for (int i=0;i<HEADER_LENGTH; i++) {
        *((unsigned char *)(auth_reply)+i) = buf_recv[i];
        //printf("%d %02x\n",i,buf_recv[i]);        
    }

    // Authentication succeed
    if (auth_reply->m_type == AUTH_REPLY && auth_reply->m_status == STATUS_1) {
        client_state = AUTHENTICATED;
        printf("Authentication granted.\n");
    } 
    // Failed
    else {
        close(curr_clientfd);
        client_state = IDLE;
        printf("Error: Authentication rejected. connection closed.\n");
    }

    free(auth_request);
    auth_request = NULL;
    free(payload);
    payload = NULL;
    free(auth_reply);
    auth_reply = NULL;
    return;
}

/*
    List
    1. The client sends a "LIST_REQUEST" message to the server.
    2. The server replies a "LIST_REPLY" message with a list of available files.
*/
void List(void) {
    if (client_state == IDLE) {
        printf("ERROR: No connection. Please connect to a server.\n");
        Print_Usage(1);
        return;
    }
    if (client_state == CONNECTED) {
        printf("ERROR: Authentication not started. Please issue an authentication command.\n");
        Print_Usage(2);
        return;
    }

    struct myFTP_Header * list_request;
    list_request = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    for (int i=0; i<6; i++) 
        list_request->m_protocol[i] = PROTOCOL_NAME[i];
    list_request->m_type = LIST_REQUEST;
    list_request->m_status = STATUS_UNUSED;
    list_request->m_length = Small2Big_Endian(HEADER_LENGTH);
    char buf_send[SHORT_MSG_LENGTH];
    for (int i=0; i<HEADER_LENGTH; i++) 
        buf_send[i] = *((char *)(list_request)+i);  
    // Safe send
    size_t ret = 0, send_cnt, send_len = HEADER_LENGTH;
    while (ret < send_len) {
        send_cnt = send(curr_clientfd, buf_send+ret, send_len-ret, 0);
        if (send_cnt == 0) printf("Socket closed.\n");
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }

    struct myFTP_Header * list_reply = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    char * filelist;
    int ls_len;
    char buf_recv[SHORT_MSG_LENGTH];
    size_t recv_cnt = 0;
    recv_cnt = recv(curr_clientfd, buf_recv, HEADER_LENGTH, 0);
    if (recv_cnt < 0) {
        printf("Error: Socket receive error.\n");
        exit(1);
    }
    for (int i=0; i<HEADER_LENGTH; i++) {
        *((unsigned char *)(list_reply)+i) = buf_recv[i];
        //printf("%d %02x\n",i,buf_recv[i]);        
    }
    if (list_reply->m_type != LIST_REPLY) {
        printf("Error: Received message error.\n");
        free(list_request);
        list_request = NULL;
        free(filelist);
        filelist = NULL;
        free(list_reply);
        list_reply = NULL;
        return;
    }
    ls_len = Big2Small_Endian(list_reply->m_length)-HEADER_LENGTH;
    filelist = (char *) malloc(ls_len*sizeof(char));
    recv_cnt = recv(curr_clientfd, filelist, ls_len, 0);
    if (recv_cnt == ls_len) {
        printf("----- file list start -----\n");
        for (int i=0; i<ls_len-1; i++) 
            putchar(filelist[i]);
        printf("----- file list end -----\n");
    }
    else {
        printf("Error: Received message error.\n");
    }

    free(list_request);
    list_request = NULL;
    free(filelist);
    filelist = NULL;
    free(list_reply);
    list_reply = NULL;
    return;
}

/*
    Get
    1. The client sends a "GET_REQUEST" message to the server, including the filename as payload.
    2. The server checks if the file is available in its resource directory (probably its current directory).
        If negative, the server replies a "GET_REPLY" message with the m_status == 0;
        Otherwise, m_status == 1, and the server sends a "FILE_DATA" message;
    3. If m_status == 0, print "File does not exists."
        if m_status == 1, save the file locally.
    Note: The file will cover existing local file of the same name.
        Both ASCII and binary files should be supported.
*/
void Get(char * filename) {
    if (client_state == IDLE) {
        printf("ERROR: No connection. Please connect to a server.\n");
        Print_Usage(1);
        return;
    }
    if (client_state == CONNECTED) {
        printf("ERROR: Authentication not started. Please issue an authentication command.\n");
        Print_Usage(2);
        return;
    }

    struct myFTP_Header * get_request;
    char * pld_send;
    int pld_send_len = strlen(filename)+1;
    get_request = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    for (int i=0; i<6; i++) 
        get_request->m_protocol[i] = PROTOCOL_NAME[i];
    get_request->m_type = GET_REQUEST;
    get_request->m_status = STATUS_UNUSED;
    get_request->m_length = Small2Big_Endian(HEADER_LENGTH+pld_send_len);
    pld_send = (char *) malloc((strlen(filename)+1)*sizeof(char));
    for (int i=0; i<pld_send_len-1; i++) 
        pld_send[i] = filename[i];
    pld_send[pld_send_len-1] = '\0';
    char buf_send[SHORT_MSG_LENGTH], send_len = HEADER_LENGTH+pld_send_len;
    for (int i=0; i<HEADER_LENGTH; i++) 
        buf_send[i] = *((char *)(get_request)+i);  
    for (int i=0; i<pld_send_len; i++) 
        buf_send[i+HEADER_LENGTH] = pld_send[i];
    // Safe send
    size_t ret = 0, send_cnt;
    while (ret < send_len) {
        send_cnt = send(curr_clientfd, buf_send+ret, send_len-ret, 0);
        if (send_cnt == 0) printf("Socket closed.\n");
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }

    struct myFTP_Header * get_reply;
    char buf_recv[SHORT_MSG_LENGTH];
    size_t recv_cnt;
    get_reply = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    recv_cnt = recv(curr_clientfd, buf_recv, HEADER_LENGTH, 0);
    if (recv_cnt < 0) {
        printf("Error: Socket receive error.\n");
        exit(1);
    }
    for (int i=0; i<HEADER_LENGTH; i++) {
        *((unsigned char *)(get_reply)+i) = buf_recv[i];
        //printf("%d %02x\n",i,buf_recv[i]);        
    }    
    if (get_reply->m_type != GET_REPLY) {
        printf("Error: Received message error.\n");
        free(get_request);
        get_request = NULL;
        free(pld_send);
        pld_send = NULL;
        free(get_reply);
        get_reply = NULL;
        return;
    }
    if (get_reply->m_status != STATUS_1) {
        printf("Sorry! Target file %s does not exist, or is not available.\n", filename);
        free(get_request);
        get_request = NULL;
        free(pld_send);
        pld_send = NULL;
        free(get_reply);
        get_reply = NULL;
        return;
    }
    
    struct myFTP_Header * file_data;
    file_data = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    char * pld_file;
    int pld_file_len;
    int some_waste;
    recv_cnt = recv(curr_clientfd, buf_recv, HEADER_LENGTH, 0);
    if (recv_cnt < 0) {
        printf("Error: Socket receive error.\n");
        exit(1);
    }
    for (int i=0; i<HEADER_LENGTH; i++) {
        *((unsigned char *)(file_data)+i) = buf_recv[i];
        //printf("%d %02x\n",i,buf_recv[i]);        
    }    
    if (file_data->m_type != FILE_DATA) {
        printf("Error: Received message error.\n");
        free(get_request);
        get_request = NULL;
        free(pld_send);
        pld_send = NULL;
        free(get_reply);
        get_reply = NULL;
        free(file_data);
        file_data = NULL;
        free(pld_file);
        pld_file = NULL;
        return;
    }
    pld_file_len = Big2Small_Endian(file_data->m_length)-HEADER_LENGTH;
    pld_file = (char *) malloc(LONG_MSG_LENGTH);

    FILE * localfile = fopen(filename, "w+");
    if (localfile == NULL) {
        printf("Error: Can not open file %s.\n", filename);
        printf("Error details: %s\n", strerror(errno));
    }
    int cut = 0;
    size_t recv_len;
    while (pld_file_len > LONG_MSG_LENGTH) {
        recv_len = LONG_MSG_LENGTH;
        ret = 0;
        while (ret < recv_len) {
            recv_cnt = recv(curr_clientfd, pld_file+ret, recv_len-ret, 0);
            if (recv_cnt < 0) {
                printf("Error: Socket receive error.\n");
                exit(1);
            }
            ret += recv_cnt;
        }
        cut++;
        pld_file_len-=recv_len;
        some_waste = fwrite(pld_file, recv_len, 1, localfile);

        if (some_waste != 1) {
            printf("Error: Can not write file %s.\n", filename);
            printf("Error details: %s\n", strerror(errno));
        }
    }
    recv_len = pld_file_len;
    ret = 0;
    while (ret < recv_len) {
        recv_cnt = recv(curr_clientfd, pld_file+ret, recv_len-ret, 0);
        if (recv_cnt < 0) {
            printf("Error: Socket receive error.\n");
            exit(1);
        }
        ret += recv_cnt;
    }
    some_waste = fwrite(pld_file, recv_len, 1, localfile);
    if (some_waste != 1) {
        printf("Error: Can not write file %s.\n", filename);
        printf("Error details: %s\n", strerror(errno));
    }
    fclose(localfile);
    /*pld_file = (char *) malloc(pld_file_len*sizeof(char));
    ret = 0;
    while (ret < pld_file_len) {
        recv_cnt = recv(curr_clientfd, pld_file+ret, pld_file_len-ret, 0);
        if (recv_cnt == 0) printf("Socket closed.\n");
        if (recv_cnt < 0) {
            printf("Error: Socket receive error.\n");
            exit(1);
        }
        ret += recv_cnt;
    }
    if (recv_cnt != pld_file_len) {
        printf("Error: Received message error.\n");
        free(get_request);
        get_request = NULL;
        free(pld_send);
        pld_send = NULL;
        free(get_reply);
        get_reply = NULL;
        free(file_data);
        file_data = NULL;
        free(pld_file);
        pld_file = NULL;
        return;
    }

    FILE * localfile = fopen(filename, "w+");
    if (localfile != NULL) {
        some_waste=fwrite(pld_file, pld_file_len, 1, localfile);
        fclose(localfile);
    }
    if (localfile == NULL || some_waste != 1) {
        printf("Error: Can not save file %s.\n", filename);
        printf("Error details: %s\n", strerror(errno));
    }
    */
    printf("File %s downloaded\n", filename);

    free(get_request);
    get_request = NULL;
    free(pld_send);
    pld_send = NULL;
    free(get_reply);
    get_reply = NULL;
    free(file_data);
    file_data = NULL;
    free(pld_file);
    pld_file = NULL;
    return;
}

/*
    Put
    1. The client checks if the file is available in its current directory.
    2. The client sends a "PUT_REQUEST" message to the server, including the filename as payload.
    3. The server always agrees, and it will reply a "PUT_REPLY" message.
    4. The client sends a "FILE_DATA" message to the server, including the file as payload.
*/
void Put(char * filename) {
    if (client_state == IDLE) {
        printf("ERROR: No connection. Please connect to a server.\n");
        Print_Usage(1);
        return;
    }
    if (client_state == CONNECTED) {
        printf("ERROR: Authentication not started. Please issue an authentication command.\n");
        Print_Usage(2);
        return;
    }

    int file_access = access(filename, R_OK);
    if (file_access == -1) {
        printf("Error: File %s does not exist, or is not available.\n", filename);
        return;
    }
    
    struct myFTP_Header * put_request;
    char * pld_send;
    int pld_send_len = strlen(filename)+1;
    put_request = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    for (int i=0; i<6; i++) 
        put_request->m_protocol[i] = PROTOCOL_NAME[i];
    put_request->m_type = PUT_REQUEST;
    put_request->m_status = STATUS_UNUSED;
    put_request->m_length = Small2Big_Endian(HEADER_LENGTH+pld_send_len);
    pld_send = (char *) malloc((strlen(filename)+1)*sizeof(char));
    for (int i=0; i<pld_send_len-1; i++) 
        pld_send[i] = filename[i];
    pld_send[pld_send_len-1] = '\0';
    char buf_send[SHORT_MSG_LENGTH];
    size_t send_len = HEADER_LENGTH+pld_send_len;
    for (int i=0; i<HEADER_LENGTH; i++) 
        buf_send[i] = *((char *)(put_request)+i);  
    for (int i=0; i<pld_send_len; i++) 
        buf_send[i+HEADER_LENGTH] = pld_send[i];
    // Safe send
    size_t ret = 0, send_cnt;
    while (ret < send_len) {
        send_cnt = send(curr_clientfd, buf_send+ret, send_len-ret, 0);
        if (send_cnt == 0) printf("Socket closed.\n");
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }

    struct myFTP_Header * put_reply;
    char buf_recv[SHORT_MSG_LENGTH];
    size_t recv_cnt;
    put_reply = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    recv_cnt = recv(curr_clientfd, buf_recv, HEADER_LENGTH, 0);
    if (recv_cnt < 0) {
        printf("Error: Socket receive error.\n");
        exit(1);
    }
    for (int i=0; i<HEADER_LENGTH; i++) {
        *((unsigned char *)(put_reply)+i) = buf_recv[i];
        //printf("%d %02x\n",i,buf_recv[i]);        
    }    
    if (put_reply->m_type != PUT_REPLY) {
        printf("Error: Received message error.\n");
        free(put_request);
        put_request = NULL;
        free(pld_send);
        pld_send = NULL;
        free(put_reply);
        put_reply = NULL;
        return;
    }
    
    struct myFTP_Header * file_data;
    file_data = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    for (int i=0; i<6; i++) 
        file_data->m_protocol[i] = PROTOCOL_NAME[i];
    file_data->m_type = FILE_DATA;
    file_data->m_status = STATUS_UNUSED;
    char * pld_file;
    int pld_file_len;
    int some_waste;
    FILE * localfile = fopen(filename, "r");
    if (localfile == NULL) {
        printf("Error: Can not open file %s.\n", filename);
        return;
    }
    fseek(localfile, 0, SEEK_END);          // Locate the end of the file.
    pld_file_len = ftell(localfile);    // Get the length
    file_data->m_length = Small2Big_Endian(HEADER_LENGTH+pld_file_len);
    fseek(localfile, 0, SEEK_SET);          // Locate the begin of the file, for the read followed.
    pld_file = (char *)malloc(LONG_MSG_LENGTH);
    send_len = HEADER_LENGTH;
    for (int i=0; i<send_len; i++) 
        pld_file[i] = *((char *)(file_data)+i);
    ret = 0;
    while (ret < send_len) {
        send_cnt = send(curr_clientfd, pld_file+ret, send_len-ret, 0);
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }
    int cut = 0;
    while (pld_file_len > LONG_MSG_LENGTH) {
        send_len = LONG_MSG_LENGTH;
        some_waste = fread(pld_file, send_len, 1, localfile);
        ret = 0;
        while (ret < send_len) {
            send_cnt = send(curr_clientfd, pld_file+ret, send_len-ret, 0);
            if (send_cnt < 0) {
                printf("Error: Socket send error.\n");
                exit(1);
            } 
            ret += send_cnt;
        }
        cut++;
        pld_file_len-=send_len;
    }
    send_len = pld_file_len;
    some_waste = fread(pld_file, send_len, 1, localfile);
    ret = 0;
    while (ret < send_len) {
        send_cnt = send(curr_clientfd, pld_file+ret, send_len-ret, 0);
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }
    fclose(localfile);
    /*pld_file = (char *) malloc(pld_file_len*(sizeof(char)));
    some_waste = fread(pld_file, pld_file_len, 1, localfile);
    fclose(localfile);
    if (some_waste != 1) {
        printf("Error: Can not read file %s.\n", filename);
        printf("Error details: %s\n", strerror(errno));
        free(put_request);
        put_request = NULL;
        free(pld_send);
        pld_send = NULL;
        free(put_reply);
        put_reply = NULL;
        free(file_data);
        file_data = NULL;
        free(pld_file);
        pld_file = NULL;
        return;
    }    
    for (int i=0; i<HEADER_LENGTH; i++) 
        buf_send[i] = *((char *)(file_data)+i);  
    send_len = HEADER_LENGTH;
    // Safe send
    ret = 0;
    while (ret < send_len) {
        send_cnt = send(curr_clientfd, buf_send+ret, send_len-ret, 0);
        if (send_cnt == 0) printf("Socket closed.\n");
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }
    ret = 0;
    while (ret < pld_file_len) {
        send_cnt = send(curr_clientfd, pld_file+ret, pld_file_len-ret, 0);
        if (send_cnt == 0) printf("Socket closed.\n");
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }*/
    printf("File %s uploaded\n", filename);

    free(put_request);
    put_request = NULL;
    free(pld_send);
    pld_send = NULL;
    free(put_reply);
    put_reply = NULL;
    free(file_data);
    file_data = NULL;
    free(pld_file);
    pld_file = NULL;
    return;
}

/*
    Quit
    1. The client sends a "QUIT_REQUEST" message to the server.
    2. The server replies a "QUIT_REPLY" message to the client.
    3. The client closes the connection, set the client_state back to IDLE,
        prints goodbye and exit the program. 
*/
void Quit(void) {
    if (client_state == IDLE) {
        return;
    }

    struct myFTP_Header * quit_request;
    quit_request = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    for (int i=0; i<6; i++) 
        quit_request->m_protocol[i] = PROTOCOL_NAME[i];
    quit_request->m_type = QUIT_REQUEST;
    quit_request->m_status = STATUS_UNUSED;
    quit_request->m_length = Small2Big_Endian(HEADER_LENGTH);
    char buf_send[SHORT_MSG_LENGTH], send_len = HEADER_LENGTH;
    for (int i=0; i<HEADER_LENGTH; i++) 
        buf_send[i] = *((char *)(quit_request)+i);  
    size_t ret = 0, send_cnt;
    while (ret < send_len) {
        send_cnt = send(curr_clientfd, buf_send+ret, send_len-ret, 0);
        if (send_cnt == 0) printf("Socket closed.\n");
        if (send_cnt < 0) {
            printf("Error: Socket send error.\n");
            exit(1);
        }
        ret += send_cnt;
    }

    struct myFTP_Header * quit_reply;
    char buf_recv[SHORT_MSG_LENGTH];
    size_t recv_cnt;
    quit_reply = (struct myFTP_Header *) malloc(HEADER_LENGTH);
    recv_cnt = recv(curr_clientfd, buf_recv, HEADER_LENGTH, 0);
    if (recv_cnt < 0) {
        printf("Error: Socket receive error.\n");
        exit(1);
    }
    for (int i=0; i<HEADER_LENGTH; i++) {
        *((unsigned char *)(quit_reply)+i) = buf_recv[i];
        //printf("%d %02x\n",i,buf_recv[i]);        
    }    
    if (quit_reply->m_type != QUIT_REPLY) {
        printf("Error: Received message error.\n");
        free(quit_request);
        quit_request = NULL;
        free(quit_reply);
        quit_reply = NULL;
        return;
    }

    close(curr_clientfd);
    client_state = IDLE;
    printf("Goodbye! Have a nice day!\n");
    free(quit_request);
    quit_request = NULL;
    free(quit_reply);
    quit_reply = NULL;
    return;
}
/* 
    Init
    Reserved for possible initialization.    
*/
void Init(void) {
    return;
}

/*
    Print_Usage
    print the usage of commands when irregular input occurs.
*/
void Print_Usage(int flag) {
    printf("Usage:\n");
    if (flag & 1)
        printf("open <IP> <port>\t\t\t\tOpen a connection to a server.\n");
    if (flag & 2)
        printf("auth <username> <password>\t\t\t\tAuthentication on the server.\n");
    if (flag & 4)
        printf("ls\t\t\t\tList the files of the server's working directory.\n");
    if (flag & 8)
        printf("get <filename>\t\t\t\tGet a file from the server.\n");
    if (flag & 16)
        printf("put <filename>\t\t\t\tPut a file onto the server.\n");
    if (flag & 32)
        printf("quit\t\t\t\tQuit the client program.\n\n");
    return;
}
/* 
    Idle
    Print "Client>", read the commands from STDIN, identify and dispatch them.
*/
void Idle(void) {
    int some_waste;
    char buf[MAX_INPUT_LENGTH];
    int cmd_length;
    //char cmd_name[MAX_CMD_LENGTH];
    char my_argv[MAX_ARGC_NUM][MAX_INPUT_LENGTH/4];
    int my_argc;

    some_waste=write(1, "Client> ", 8);
    while((cmd_length=read(0, buf, 256)) != -1) {
        if (cmd_length == 0) {  // Is it ever possible?
            continue;
        }
        if (cmd_length == 1) {  // buf == "\n"
            printf("Client> ");
            continue;
        }
        // Parse the command
        my_argc = 0;
        int ptr_head = 0, ptr_tail = -1;
        memset(my_argv, 0, sizeof(my_argv));
        while (1) {
            ptr_tail++;
            ptr_head = ptr_tail;
            while (buf[ptr_tail] != ' ' && buf[ptr_tail] != '\n') {
                ptr_tail++;
            }
            if (ptr_tail == ptr_head) { // a ' ' or '\n' is read
                if (buf[ptr_tail] == '\n') break;
                else continue;
            }
            for (int i=0; i<ptr_tail-ptr_head; i++) 
                my_argv[my_argc][i] = buf[ptr_head+i];
            my_argc++;
            if (buf[ptr_tail] == '\n') 
                break;
        }
        if (my_argc == 0) {// buf == "    \n"
            some_waste=write(1, "Client> ", 8);
            continue;
        }
        // Dispatch the command to specific functions.
        if (strcmp(my_argv[0], "open") == 0) {
            if (my_argc != 3) {
                Print_Usage(1);
            }
            else {
                // Check the validity of IP and port.
                uint16_t my_port = (uint16_t) atoi((const char *) my_argv[2]);
                if (my_port == 0) {// Invalid port
                    Print_Usage(1);
                }
                else 
                    Open(my_argv[1], my_port);
            }
        }
        else if (strcmp(my_argv[0], "auth") == 0) {
            if (my_argc != 3) 
                Print_Usage(2);
            else 
                Auth(my_argv[1], my_argv[2]);
        }
        else if (strcmp(my_argv[0], "ls") == 0) {
            if (my_argc != 1) 
                Print_Usage(4);
            else 
                List();
        }
        else if (strcmp(my_argv[0], "get") == 0) {
            if (my_argc != 2)
                Print_Usage(8);
            else 
                Get(my_argv[1]); 
        }
        else if (strcmp(my_argv[0], "put") == 0) {
            if (my_argc != 2)
                Print_Usage(16);
            else 
                Put(my_argv[1]); 
        }
        else if (strcmp(my_argv[0], "quit") == 0) {
            if (my_argc != 1)
                Print_Usage(32);
            else {
                Quit();
                return;
            }
        }
        else {
            Print_Usage(63);
        }

        memset(buf, 0, sizeof(buf));
        some_waste=write(1, "Client> ", 8);
    }
    if (cmd_length == -1) { // Error in reading STDIN
        printf("Error details: %s\n", strerror(errno));
        exit(1);
    }
}

int main(int argc, char ** argv) {
    Init();

    Idle();
    return 0;
}
