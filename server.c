/* ======== UDP ECHO SERVER ======== */
/* ================================= */

//include
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>
#include <signal.h>

//macros
#define slist_head(l) l->head
#define slist_tail(l) l->tail
#define slist_size(l) l->size
#define slist_next(n) n->next
#define slist_data(n) n->data
#define FAILURE -1
#define SUCCESS 0
#define KILOBYTE 1024

//the list structures
struct slist_node
{
	void *data; //pointer to data of this node
	struct slist_node *next; //pointer to next node on list
}; 
typedef struct slist_node slist_node_t;

struct slist
{
	slist_node_t *head; //pointer to head of list
	slist_node_t *tail; //pointer to tail of list
	unsigned int size; //the number of elements in the list
}; 
typedef struct slist slist_t;

//functions prototypes
void slist_init(slist_t *);
void slist_destroy(slist_t *);
void *slist_pop_first(slist_t *);
int slist_append(slist_t *,void *);
void to_upper_case(char*);
int digits_only(char*);
void error(const char*);
void sig_handler(int);

//static globals
static int socket_fd;
static slist_t *sources_queue;
static slist_t *data_queue;

int main(int argc, char *argv[])
{
	//checking input
	if (argc != 2)
	{
		printf("Usage: %s <port>\n", argv[0]);
		exit(EXIT_FAILURE);
	}	
	if (digits_only(argv[1]) < 0)
	{
		printf("Illegal input\n");
		exit(EXIT_FAILURE);
	}
	
	signal(SIGINT, sig_handler); //setting up signal catcher for Ctrl+C
	//variables
	char read_buffer[4 * KILOBYTE] = { 0 }, write_buffer[4 * KILOBYTE] = { 0 }, *buffer;
	int port_no = atoi(argv[1]), free_fd_received;
	struct sockaddr_in server = { 0 }, *next_client = NULL, *client = NULL; 
	socklen_t socklen = sizeof(client);
	
	//server definitions
	server.sin_family = AF_INET;
	server.sin_port = htons(port_no);
	server.sin_addr.s_addr = INADDR_ANY;
	
	//setting up the socket
	socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socket_fd < 0) 
		error("socket");
	
	//binding
	if (bind(socket_fd, (struct sockaddr*)&server, sizeof(server)) < 0)
	{
		close(socket_fd);
		error("bind");
	}
	
	//setting up the queues
	data_queue = (slist_t*)calloc(1, sizeof(slist_t));
	if (!data_queue)
	{
		close(socket_fd);
		error("memory allocation");
	}
	slist_init(data_queue);
	sources_queue = (slist_t*)calloc(1, sizeof(slist_t));
	if (!sources_queue)
	{
		slist_destroy(data_queue);
		close(socket_fd);
		error("memory allocation");
	}
	slist_init(sources_queue);
	
	//file descriptors sets
	fd_set read_set, write_set;
	int max_requests = 0;
	//listening to multiple requests through one socket
	while (max_requests < 50)
	{
		FD_ZERO(&read_set);
		FD_ZERO(&write_set);
		/* if the queue is not empty that means that we read already from the client
		 and he wasn't ready to receive our response. in that case we do select and 
		 check if he sent us more to read, but also if we can start write back to him */
		if (data_queue->size)
		{
			FD_SET(socket_fd, &read_set);
			FD_SET(socket_fd, &write_set);
		}
		else
		{
			FD_SET(socket_fd, &read_set);
		}
		free_fd_received = select(socket_fd + 1, &read_set, &write_set, NULL, NULL);
		//error accurred
		if (free_fd_received < 0)
		{
			slist_destroy(sources_queue);
			slist_destroy(data_queue);
			close(socket_fd);
			error("select");
		}
		
		//check if the read operation is ready on the socket
		if (FD_ISSET(socket_fd, &read_set))
		{
			printf("server is ready to read\n");
			//allocating new client
			client = (struct sockaddr_in*)calloc(1, sizeof(struct sockaddr_in));
			if (!client)
			{
				slist_destroy(sources_queue);
				slist_destroy(data_queue);
				close(socket_fd);
				error("memory allocation");
			}
			
			//reading from the client
			if(recvfrom(socket_fd, read_buffer, sizeof(read_buffer), 0, (struct sockaddr*)client, &socklen) < 0)
			{
				free(client);
				slist_destroy(sources_queue);
				slist_destroy(data_queue);
				close(socket_fd);
				error("receive");
			}
			else
			{
				//add the client to the sources queue
				if (slist_append(sources_queue, client) < 0)
				{
					free(client);
					slist_destroy(sources_queue);
					slist_destroy(data_queue);
					close(socket_fd);
					error("memory allocation");
				}
				
				//turn all the letters in the message to upper case letters
				to_upper_case(read_buffer);
				
				//allocating new messege
				buffer = (char*)calloc(4 * KILOBYTE, sizeof(char));
				if (!buffer)
				{
					slist_destroy(sources_queue);
					slist_destroy(data_queue);
					close(socket_fd);
					error("memory allocation");
				}
				strcpy(buffer, read_buffer);
				
				//add the message to the data queue
				if (slist_append(data_queue, buffer) < 0)
				{
					slist_destroy(sources_queue);
					slist_destroy(data_queue);
					close(socket_fd);
					error("memory allocation");
				}
				bzero(read_buffer, sizeof(read_buffer));
			}
		}
		//check if the read operation is ready on the socket, and that there's anything to write
		if (FD_ISSET(socket_fd, &write_set) && data_queue->size)
		{
			printf("server is ready to write\n");
			//copying the next messege from the list
			char *next_message = (char*)slist_pop_first(data_queue);
			strcpy(write_buffer, next_message);
			//pop the next client which belongs to the current message 
			next_client = (struct sockaddr_in*)slist_pop_first(sources_queue);
			if (sendto(socket_fd, write_buffer, sizeof(write_buffer), 0, (struct sockaddr*)next_client, socklen) < 0)
			{
				slist_destroy(sources_queue);
				slist_destroy(data_queue);
				close(socket_fd);
				error("send");
			}
			bzero(write_buffer, sizeof(write_buffer));
			free(next_message);
			free(next_client);
		}
	}
}

//this function will check if a text contains only digits
int digits_only(char* text)
{
	int i;
	for (i = 0; i < strlen(text); i++)
		if (text[i] < 48 || text[i] > 57) //by ASCII 
			return FAILURE;
	return SUCCESS;
}

//this function will change a text small letters to capital
void to_upper_case(char* text)
{
	int i;
	for (i = 0; i < strlen(text); i++)
		if (text[i] > 96 && text[i] < 123)
			text[i] -= 32;
}

/* the list constructor */
void slist_init(slist_t *list)
{
	slist_head(list) = NULL;
	slist_tail(list) = NULL;
	slist_size(list) = 0;
}

/* the list destroyer */
void slist_destroy(slist_t *list)
{
	if (!list)
		return;
	slist_node_t *temp_h = slist_head(list), *temp_s;
	while (temp_h)
	{
		temp_s = slist_next(temp_h);
		free(temp_h->data);
		free(temp_h);
		temp_h = temp_s;
	}
	free(list);
}

/* pop the head */
void *slist_pop_first(slist_t *list)
{
	if (!list || !slist_head(list))
		return NULL;
	
	void* data = slist_data(slist_head(list));
	if(list->size == 1) //if only one node, 
	{
		free(slist_head(list));
		slist_init(list); //initialize new list
		return data;
	}
	//else, reallocate the head and farward its pointer to the next node
	slist_node_t *temp = slist_head(list);
	slist_head(list) = slist_next(slist_head(list));
	slist_size(list)--;
	free(temp);
	
	return data;
}

/* add a node at the begining of the list */
int slist_append(slist_t *list, void *data)
{
	slist_node_t *new_node = (slist_node_t*)calloc(1, sizeof(slist_node_t));
	if (!new_node)
		return FAILURE;
		
	slist_data(new_node) = data;
	slist_next(new_node) = NULL;
	
	//if the first node
	if (!slist_head(list))
	{
		slist_head(list) = new_node;
		slist_tail(list) = new_node;
	}
	else
	{
		slist_next(slist_tail(list)) = new_node;
		slist_tail(list) = slist_next(slist_tail(list));
	}
	slist_size(list)++;
	return SUCCESS;
}

//perror and exit function
void error(const char* msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

//this function will handle the signal ctrl+C
void sig_handler(int sig_number)
{
	if (sig_number == SIGINT)
	{
		slist_destroy(sources_queue);
		slist_destroy(data_queue);
		close(socket_fd);
		printf("\n");
		exit(EXIT_SUCCESS);
	}
}

