#define RRQ 0x01
#define WRQ 0x02
#define DATA 0x03
#define ACK 0x04
#define ERR 0x05
#define ERROR -1
#define MAXREQPACKET 256
#define MAX_FILE_BUFFER 1024
#define MAX_RETRY 10 
char ERROR_MESSAGE[8][256] = {	"Not defined, see error message (if any).\0",
								"File not found.\0",
								"Access violation.\0",
								"Disk full or allocation exceeded.\0",
								"Illegal TFTP operation.\0",
								"Unknown transfer ID.\0",
								"File already exists.\0",
								"No such user.\0"};
struct timeval time_channel,start_time,end_time;