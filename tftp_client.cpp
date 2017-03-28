#include <iostream>
#include <stdio.h>
#include <cstdlib>
#include <sys/socket.h>		
#include <netinet/in.h>
#include <sys/time.h>
#include <arpa/inet.h>		
#include <netdb.h>
#include <string.h>
#include <cstdlib>
#include <unistd.h>
#include <errno.h>
#include "my_tftp.h"  //some constants have been defined there
using namespace std;
bool validation(int argc);
int connect_to_server(const char *argv[],struct sockaddr_in &server_addr);
void display_server_details(struct sockaddr_in addr);
bool req_packet(unsigned char opcode,char *filename, char request_buf[],int *request_length);
void get_file(char filename[],struct sockaddr_in server_addr,int socket);
void post_file(char filename[],struct sockaddr_in server_addr,int socket);
int ack_packet(int block,char ack_buf[]);
int error_packet(unsigned char error_buf[],unsigned char error_code);
char current_dir[256];
bool debug = false;
/*
	** server_address port <-g filename> <-p filename>
*/
int main(int argc, char const *argv[])
{
	if(!validation(argc)){
		fprintf(stderr, "%s\n"," Press -g followed by file_name to download file and -p followed by file_name to upload the file into the tftp server. -d optional to see debug message");
		return ERROR;
	}
	if(argc == 6){
		if(!strcmp(argv[5],"-d")){
			debug = true;
		}
	}
	time_channel.tv_sec = 2;
	time_channel.tv_usec = 0;
	int client_socket = -1;
	struct hostent *server = NULL;
	struct sockaddr_in server_addr;
	server = gethostbyname(argv[1]);
	if(server == NULL){
		perror(" Can not get server by hostname. Please try to use IP format");
		return ERROR;
	}
	if(debug){
		cout<<" Server has been found successfully"<<endl;
	}
	bzero((char *)&server_addr , sizeof(server_addr)); 	// clean the var
	if((client_socket = connect_to_server(argv,server_addr)) < 0){
		perror(" Can not connect to the server or socket error");
		return ERROR;
	}
	display_server_details(server_addr);
	unsigned char opcode = 0x06;
	char filename[50];
	getcwd(current_dir,256);
	strcat(current_dir,"/");
	switch(argv[3][1]){
		case 'g':
			opcode = RRQ;
			strncpy(filename,argv[4],strlen(argv[4]));	
			if(debug){
				printf("%s -> %02x --> %s\n","READ REQUEST",opcode,filename );	
			}
			break;
		case 'p':
			opcode = WRQ;
			strncpy(filename,argv[4],strlen(argv[4]));
			if(debug){
				printf("%s -> %02x --> %s\n","WRITE REQUEST",opcode,filename );
			}
			break;
		default:
			cout<<" Error Arguments "<<endl;
			return ERROR;
	}
	char request_buf[MAXREQPACKET];
	int request_length;
	/*
		** request packet will be sent to server request can be RRQ or WRQ
	*/
	if(!req_packet(opcode,filename,request_buf,&request_length)){
		perror(" Can not create request packet ");
		return ERROR;
	}
	if(debug)
		cout<<" Request Packet has been created "<<endl;
	int request_send_status = -1;
	if(debug)
		printf("Opcode : %02x",request_buf[1]);
	if((request_send_status = sendto(client_socket,request_buf,request_length,0,(const struct sockaddr *)&server_addr,
									sizeof(server_addr))) < 0){
		perror(" Can not send request packet ");
		return ERROR;
	}
	if(debug)
		cout<<" Successfully sent request packet ==> "<<request_send_status<<endl;
	switch(opcode){
		case RRQ:
			gettimeofday(&start_time, NULL);
			get_file(filename,server_addr,client_socket);
			gettimeofday(&end_time, NULL);
			break;
		case WRQ:
			gettimeofday(&start_time, NULL);
			post_file(filename,server_addr,client_socket);
			gettimeofday(&end_time, NULL);
			break;
		default: 
			cout<<" No predefined function "<<endl;
			return ERROR;
	}
	close(client_socket);
	printf("Total operation took => %d millisecond(s)\n",(int)((end_time.tv_sec - start_time.tv_sec) * 1000 + (end_time.tv_usec - start_time.tv_usec)/1000));
	return 0;
}
int connect_to_server(const char *argv[],struct sockaddr_in &server_addr){
	bzero((char *)&server_addr,sizeof (server_addr));	/*Clear the structure */
  	server_addr.sin_family = AF_INET;	/*address family for TCP and UDP */
  	int port = atoi(argv[2]);
  	server_addr.sin_port = htons (port);
  	server_addr.sin_addr.s_addr = inet_addr(argv[1]);
  	int client_socket = -1;
  	client_socket = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  	return client_socket;
}
bool validation(int argc){
	return (argc == 5 || argc == 6);
}
void display_server_details(struct sockaddr_in addr){
	if(debug){
		printf("%s\n","=================================================================" );
		printf("%s\n","You have successfully connected with the server . Details are as below : " );
		printf(" Server Ip -> %s\n",inet_ntoa(addr.sin_addr));
		printf(" Server Port -> %d\n",ntohs(addr.sin_port));
	}
}
bool req_packet(unsigned char opcode,char *filename,char request_buf[],int *request_length){
	int packet_length = sprintf(request_buf,"%c%c%s%c%s%c",0x00,opcode,filename,0x00,"octet",0x00);
	if(packet_length > 0){
		if(debug)
			printf("Request packet length => %d\n",packet_length);
		*request_length = packet_length;
	}
	return (packet_length > 0);
}
void get_file(char filename[],struct sockaddr_in server_addr,int socket){
	if(debug){
		printf(" FILENAME - %s\n",filename );
		printf(" Server IP - %s\n",inet_ntoa(server_addr.sin_addr));
		printf("Socket - %d\n GOING TO DOWNLOAD FILE FROM THE SERVER\n",socket );
		printf("%s\n","-------------------------------------------------------" );
	}	
	FILE *fp = NULL;
	int data_section = 512;			//tftpd will send 516 = 4 + 512 bytes  
	extern int errno;				// So that this var can be modified in case of any error
	int response = data_section + 4;	// server response should be 516
	unsigned char file_buffer[MAX_FILE_BUFFER + 1] = {0};		// ascii >= 0 clean the buffer to prevent garbage writing
	unsigned char response_buf[MAX_FILE_BUFFER + 1] = {0};
	char ack_buf[256] = {0};
	int received_packet,next_packet;
	received_packet = 0;
	next_packet = 0;
	int TID = 0;
	struct sockaddr_in anonymous;
	bzero((char *)&anonymous,sizeof(anonymous));
	int len = sizeof(anonymous);
	char *response_handler = NULL;
	int server_opcode;
	bool first_time_response = true;
	int i;
	bool file_open = true;
	char request_buf[MAXREQPACKET];
	int request_length;
	req_packet(RRQ,filename,request_buf,&request_length);
	while(response == data_section + 4){
		/*
			** receive each chunk of data
		*/
		bzero((char *)response_buf,MAX_FILE_BUFFER);
		bzero((char *)file_buffer,MAX_FILE_BUFFER);
		bzero(ack_buf,256);
		for (i = 0; i < MAX_RETRY; i++){	
			response = -1;
			/*
				** set a 2 sec time out
			*/
			if(setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,(char *)&time_channel,sizeof(struct timeval)) < 0){
				perror("time out set can not be done");
			}
            response = recvfrom(socket,response_buf,sizeof(response_buf)-1,0,(struct sockaddr *)&anonymous,
									(socklen_t *)&len);
			/*
				** get the port from which response has come.
					*** TID => Transfer Identifier
					*** We have stored the TID only for the first time to ensure reliable connection
			*/
			if(!TID){
				TID = ntohs(anonymous.sin_port);			// get the port from where the response has come
				server_addr.sin_port = htons(TID);		// Consider that port as the main port
			}
			if(response < 0){
				if(debug)
					printf("Response --> %d error no --> %d\n",response,errno);
				/*
					** send again the ack signal for the previously received packet.
				*/
				if(next_packet > 0){
					int ack_length = ack_packet(next_packet,ack_buf);
						if(ack_length != sendto(socket,ack_buf,ack_length,0,(struct sockaddr *)&server_addr,
												sizeof(server_addr))){
							if(debug)
								printf("%s\n","Can not sent Ack Correctly  for next data packet" );
					}
				}
				else{
					server_addr.sin_port = htons(69);
					if(request_length != sendto(socket,request_buf,request_length,0,(const struct sockaddr *)&server_addr,
									sizeof(server_addr))){
						if(debug)
							fprintf(stderr, "%s\n","could not send re request" );
					}
				}
				if(debug)
					perror(" Client could not receive any response from error.. Time Out.. Re request send");				
			}
			/*
				** Got a valid >0 response
			*/
			else {
				/*
					** Server has responded but need to validate the intended server response
				*/
				if(anonymous.sin_addr.s_addr != server_addr.sin_addr.s_addr){
					if(debug)
						printf("Response from Different server -----> %s\n",inet_ntoa(anonymous.sin_addr));
					i--;
					continue;
				}
				/*
					** Server IP address is matched. Intended server
				*/
				else{
					/*
						** Validate the previously stored TID with new one.
					*/
					if(TID != htons(server_addr.sin_port)){
						if(debug)
							printf(" Different Transmission Identifier Act - %d But - %d \n",TID,anonymous.sin_port );
						int error_length = error_packet(response_buf,0x05); //unknown TID error
						if(error_length != sendto(socket,response_buf,error_length,0,(const struct sockaddr *)&server_addr,
							sizeof(server_addr))){
							perror("can not send error message");
						}
						i--;
						continue;
					}
					/*
						** All is Ok. Store it to file 
							*** DATA PACKET 2 + 2 + 512
					*/
					response_handler = (char *)response_buf;
					response_handler++;							// put the pointer to buf[1]
					server_opcode = *response_handler++;		// opcode storage
					/*
						** get higher 8 bit data from response_buf[2]
					*/
					received_packet = *response_handler << 8;	
					received_packet &= 0xff00;		
					/*
						** get  8 bit data from response_buf[3]
					*/	
					response_handler++;	
		      		received_packet += (*response_handler & 0x00ff);	// pointer points to the begining of the actual data section
					response_handler++;	
					memcpy((char *)file_buffer,response_handler,response-4); 	// As first 4 byte has already been processed					
		      		if(debug)
		      			printf(" Port = %d  The opcode = %02x --- The Block received -- %d\n",TID,server_opcode,received_packet);
					/*
						** First time server response handler data limit exceed
					*/
					if(first_time_response){
						if(response > 516){
							data_section = response - 4;
						}
						else if(response < 516){
							data_section = 512;		// to make ensure that this will be last file chunk
						}
						first_time_response = !first_time_response;
					}
					/*
						** To make ensure that the received packet is data packet
					*/
					if(server_opcode != 0x03){
						if(debug)
							printf(" The received packet is not data packet. Opcode %02x -- Error Message ==> %s\n",server_opcode,response_handler);
						if(server_opcode > 0x05){
							if(debug)
								printf(" Wrong opcode %02x\n",server_opcode );
							int error_length = error_packet(response_buf,0x04); //Illegal FTP operation
							if(error_length != sendto(socket,response_buf,error_length,0,(struct sockaddr *)&server_addr,
												sizeof(server_addr))){
								perror(" Wrong bytes error message sent");
							}
						}
					}
					else{
						if(file_open){
							char *token = strtok(filename,"/");
							while(token != NULL){
								filename = token;		// to pick the last node of the directory input
								token = strtok(NULL,"/");
							}
							strcat(current_dir,filename);
							if(debug)
								printf("The Absolute path --> %s\n",current_dir);
							fp = fopen(filename,"w");
							if(fp == NULL){
								perror(" Filecan not be opened for writing");
								exit(0);
							}
							file_open = !file_open;
						}
						next_packet++;
						int ack_length = ack_packet(next_packet,ack_buf);
						if(ack_length != sendto(socket,ack_buf,ack_length,0,(struct sockaddr *)&server_addr,
												sizeof(server_addr))){
							if(debug)
								printf("%s\n"," Can not sent Ack Correctly  for next data packet" );
						}
						break;		// Got a valid data packet so break the RETRY_LOOP and write that into file
					}
				}
			}
		}
		if(i == MAX_RETRY){
			printf("%s\n","Exhausted with retrying ........ ");
			if(!file_open){
				fclose(fp);
			}
			return;
		}
		if(fwrite((char *)file_buffer,1,response-4,fp) != (unsigned int)(response-4)){
			fclose(fp);
			sync();				// It will sync the inodes to  buffers and buffers to the disk --> filename
			if(debug)
				printf("%s\n","Can not write data to file Sorry ......... :(" );
			return;
		}
	}
	if(response < data_section + 4){
		/*
			** Last chunk data
		*/
		int ack_length = ack_packet(next_packet,ack_buf);
		if(ack_length != sendto(socket,ack_buf,ack_length,0,(struct sockaddr *)&server_addr,
								sizeof(server_addr))){
			if(debug)
				printf("%s\n"," Can not sent Ack Correctly  for next data packet" );
		}
		else{
			printf("%s\n","Successfully downloaded file ....... :)" );
			fclose(fp);
			sync();
			return;
		}
	}
}
void post_file(char filename[],struct sockaddr_in server_addr,int socket){
	if(debug){
		printf(" FILENAME - %s\n",filename );
		printf(" Server IP - %s\n",inet_ntoa(server_addr.sin_addr));
		printf("Socket - %d\n GOING TO UPLOAD FILE FROM THE SERVER\n",socket );
		printf("%s\n","-------------------------------------------------------" );
	}
	int received_packet = 0;
	int sent_packet = 0;
	char backup_buffer[MAXREQPACKET][MAX_FILE_BUFFER+1];     	// For storing data incase of lost ACK
	struct sockaddr_in anonymous;
	int server_length = sizeof(anonymous);
	int i,response;
	int TID = 0;
	unsigned char response_buf[MAX_FILE_BUFFER+1];
	char *response_handler = NULL;
	char file_buffer[MAX_FILE_BUFFER+1];
	unsigned char server_opcode;
	int error_length;
	char request_buf[MAXREQPACKET];
	int request_length;
	req_packet(WRQ,filename,request_buf,&request_length);
	for ( i = 0; i < MAX_RETRY; i++){
		response = -1;
		/*
				** set 2 sec time out
		*/
		if(setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,(char *)&time_channel,sizeof(struct timeval)) < 0){
				perror("time out set can not be done");
		}
        response = recvfrom(socket,response_buf,sizeof(response_buf)-1,0,(struct sockaddr *)&anonymous,
							(socklen_t *)&server_length);
		if(response < 0){
			if(received_packet == 0){
				server_addr.sin_port = htons(69);
				if(request_length != sendto(socket,request_buf,request_length,0,(const struct sockaddr *)&server_addr,
									sizeof(server_addr))){
					if(debug)
						fprintf(stderr, "%s\n","could not send re request" );
				}
			}
			if(debug)
				fprintf(stderr, "%s\n","Can not get WRQ ACK from server");
		}
		else{
			/*
				** Got a > 0 response
			*/
			if(!TID){
				TID = ntohs(anonymous.sin_port);
				server_addr.sin_port = htons(TID);
			}
			if(server_addr.sin_addr.s_addr != anonymous.sin_addr.s_addr){
				if(debug)
					fprintf(stderr, "%s -->%s\n","Got response from Different Server",inet_ntoa(anonymous.sin_addr));
				i--;
				continue;
			}
			/*
				** Got response from intended server
			*/
			else{
				if(TID != htons(server_addr.sin_port)){
					if(debug)
						printf(" Different Transmission Identifier Act - %d But - %d \n",TID,anonymous.sin_port );
					int error_length = error_packet(response_buf,0x05); // unknown TID
					if(error_length != sendto(socket,response_buf,error_length,0,(const struct sockaddr *)&server_addr,
						sizeof(server_addr))){
						perror("can not send error message");
					}
					i--;
					continue;
				}
				/*
					** All is Perfect
				*/
				response_handler = (char *)response_buf;
				response_handler++;
				server_opcode = *response_handler++;
				received_packet = *response_handler++ << 8;
		      	received_packet &= 0xff00;
		      	received_packet += (*response_handler++ & 0x00ff);
		      	if(server_opcode != ACK || received_packet != sent_packet){
		      		/*
						server sent wrong ACK
		      		*/
		      		if(debug)
		      			printf(" Server sent wrong ACK .. Opcode -> %02x -- block number --> %d message => %s\n",server_opcode,received_packet,response_buf+4);
		      		if(server_opcode > ERR){
						error_length = error_packet(response_buf,0x04);
		      			if(error_length != sendto(socket,response_buf,error_length,0,(struct sockaddr *)&server_addr,
		      									server_length)){
		      				if(debug)
		      					fprintf(stderr, "%s\n","Can not send error packet to server.." );
		      			}
		      		}
		      	}
		      	else{
		      		break;
		      	}
			}
		}
	}
	if(i == MAX_RETRY){
		fprintf(stderr, "%s\n","Exhausted with retrying........ :(" );
		return;
	}
	if(debug)
		printf(" Server sent correct ACK .. Opcode -> %02x -- block number --> %d\n",server_opcode,received_packet);	
	/*
		** Start the data transfer with the server
	*/
	FILE *fp = NULL;
	if((fp = fopen(filename,"r")) == NULL){
		fprintf(stderr, "%s\n","Can not open file for reading in client side" );
		return;
	}
	int file_size = 0;
	int data_section = 512;
	int data_packet_length = 0;
	while(true){
		file_size = fread(file_buffer,1,data_section,fp);
		sent_packet++;			// block number will start from 1
		received_packet = 0;	// Always take the first row as data packet and leave others for backup
		sprintf(backup_buffer[received_packet],"%c%c%c%c",0x00,DATA,0x00,0x00);
		backup_buffer[received_packet][2] = (sent_packet & 0xff00) >> 8 ;  
		backup_buffer[received_packet][3] = sent_packet & 0x00ff;
		memcpy((char *)backup_buffer[received_packet]+4,file_buffer,file_size);
		data_packet_length = file_size + 4;
		if(debug)
			printf(" Data packet [%d] has been prepared ..... Size : %d\n",sent_packet,data_packet_length);
		if(data_packet_length != sendto(socket,backup_buffer[received_packet],data_packet_length,0,
										(struct sockaddr *)&server_addr,server_length)){
			fprintf(stderr, "%s\n","Can not sent data packet to server properly");
			return;		// Abort transmition
		}
		/*
			** Wait for next ack packet
		*/
		TID = 0;
		for (i = 0; i < MAX_RETRY; i++){
			response = -1;
			/*
				** set 2 sec time out
			*/
			if(setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,(char *)&time_channel,sizeof(struct timeval)) < 0){
				perror("time out set can not be done");
			}
        	response = recvfrom(socket,response_buf,sizeof(response_buf)-1,0,(struct sockaddr *)&anonymous,
							(socklen_t *)&server_length);
			if(response < 0){
				if(debug)
					fprintf(stderr, "%s\n","Client can not get ACK from the server");
			}
			else{
				/*
					Got some >0 response
				*/
				if(!TID){
					TID = ntohs(anonymous.sin_port);
					server_addr.sin_port = htons(TID);
				}
				if(server_addr.sin_addr.s_addr != anonymous.sin_addr.s_addr){
					if(debug)
						fprintf(stderr, "%s\n","Got response from different server" );
					i--;
					continue;
				}
				/*
					** Got response from valid ip address
				*/
				if(TID != htons(server_addr.sin_port)){
					if(debug)
						printf(" Different Transmission Identifier Act - %d But - %d \n",TID,anonymous.sin_port );
					error_length = error_packet(response_buf,0x05);
					if(error_length != sendto(socket,response_buf,error_length,0,(const struct sockaddr *)&server_addr,
						sizeof(server_addr))){
						perror("can not send error message");
					}
					i--;
					continue;
				}
				/*
					** All is perfect process the response
				*/
				response_handler = (char *)response_buf;
				response_handler++;
				server_opcode = *response_handler++;
				received_packet = *response_handler++ << 8;
		      	received_packet &= 0xff00;
		      	received_packet += (*response_handler++ & 0x00ff);
		      	if(debug)
		      		printf("opcode : => %02x received block  => %d  sent block => %d\n",server_opcode,received_packet,sent_packet);
		      	fflush(stdout);
		      	if(server_opcode != ACK ||(received_packet != sent_packet)){
		      		if(debug)
		      			printf("This is not a ack packet... Opcode => %02x and acked block number - %d and actual ack number %d",server_opcode,received_packet,sent_packet);
		      		if(server_opcode > ERR){
						error_length = error_packet(response_buf,0x04);
		      			if(error_length != sendto(socket,response_buf,error_length,0,(struct sockaddr *)&server_addr,server_length)){
		      				if(debug)
		      					fprintf(stderr, "%s\n","Can not sent error packet to server");
		      			}
		      		}
		      	}
		      	else{
		      		if(debug)
		      			printf("Client got ack successfully opcode -> %02x ---- acked block %d\n",server_opcode,received_packet);
		      		break;
		      	}
			}
			/*
				Retransmission of all datapackets those already have been backed up 
			*/
			for (int k = 0; i <= received_packet; k++){
				if(sendto(socket,backup_buffer[i],data_packet_length,0,(struct sockaddr *)&server_addr,
						  server_length) < 0){
					if(debug)
						printf("can not sent back up packet index %d\n",i);
					return; //abort transmission
				}
			}
		}
		if(i == MAX_RETRY){
			fprintf(stderr, "%s\n","Max number of Retrying has been done..." );
			return;
		}
		if(file_size < data_section){
			printf("%s\n","All file has been uploaded.. " );
			bzero(file_buffer,MAX_FILE_BUFFER+1);
			break;
		}
	}	
	if(fp != NULL){
		fclose(fp);
		sync();
	}
	return;
}
int ack_packet(int block,char ack_buf[]){
	/*
		** 4 Byte data:
			*** 2 byte opcode 0x0004 
			*** 2 byte block number 
	*/
	int packet_length = sprintf(ack_buf,"%c%c%c%c",0x00,ACK,0x00,0x00);
	ack_buf[2] = (block & 0xff00) >> 8;
	ack_buf[3] = (block & 0x00ff);
	if(debug)
		printf(" After Receiving ==> Ack packet Length : %d == Opcode : %02x ---- Block number : %02x:%02x\n",packet_length,ACK,ack_buf[2],ack_buf[3]&0x00ff);
	return packet_length;
}
int error_packet(unsigned char error_buf[],unsigned char error_code){
	int length = sprintf((char *)error_buf,"%c%c%c%c%s%c",0x00,ERR,0x00,error_code,ERROR_MESSAGE[error_code],0x00);
	return length;
}