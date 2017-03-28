CC = g++
CFLAGs = -g -Wall -o
tftp_client: tftp_client.cpp
			$(CC) $(CFLAGs) tftp_client tftp_client.cpp
clean:  
	rm tftp_client
