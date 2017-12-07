//
// Created by Dharma Naidu on 11/13/17.
//

#include <stdio.h>
#include <mraa.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <stdbool.h>
#include <getopt.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdint.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>



static struct option long_options[]={
        {"period",required_argument,NULL,'p'},
        {"scale",required_argument,NULL,'s'},
        {"log",required_argument,NULL,'l'},
        {"id", required_argument, NULL, 'i'},
        {"host", optional_argument, NULL, 'h'},
        {0,0,0,0}
};

bool processedOne = false;
int period = 1;
int running = 1;
char tempType = 'F';
char *logfile = "";
int log_val;
int logging = 0;
const int R0 = 100000;
const int B=4275;



char command[1024];
time_t timer;
struct tm* current_time;
char time_storage[9];
sig_atomic_t volatile run_flag=1;

//server-client variables
int portnum = 19000;
int id = 000011100;
char* host = "lever.cs.ucla.edu";
int sockfd;

SSL_CTX* ssl_context;
SSL * ssl;


void do_when_interrupted()
{
    if (logging) {
        //get time
        time(&timer);
        current_time = localtime(&timer);
        strftime(time_storage, 9, "%H:%M:%S", current_time);

        write(log_val, time_storage, strlen(time_storage));
        write(log_val, " SHUTDOWN", 9);
    }
    exit(0);
}

int thread_function()
{
    mraa_aio_context temperature;
    temperature=mraa_aio_init(1);

    //mraa_aio_dir(temperature,MRAA_AIO_OUT);
    signal(SIGINT,do_when_interrupted);

    while(run_flag)
    {

        uint16_t temp;
        temp = mraa_aio_read(temperature);
        float R = 625.0 / temp - 1.0;
        R = R0 * R;
        float real_temp = 1.0 / (log(R / R0) / B + 1 / 298.15) - 273.15; //temperature conversion
        if(tempType == 'C')
            real_temp = (real_temp - 32) * 5 / 9;
        time(&timer);
        current_time = localtime(&timer);
        strftime(time_storage, 9, "%H:%M:%S",current_time);
        if(running)
        {
            if(logging)
            {
                write(log_val,time_storage,strlen(time_storage));
                write(log_val," ",1);
                char s_temp[10];
                sprintf(s_temp,"%.1f",real_temp);
                write(log_val,s_temp,strlen(s_temp));
                write(log_val,"\n",1);
            }
            //printf("%s %.1f \n", time_storage, real_temp);
        }
        processedOne = true;
        sleep(period);
    }
    mraa_aio_close(temperature);
    return 0;
}



int main(int argc, char *argv[])
{
    int *opt_index = NULL;
    int opt = getopt_long(argc, argv, "", long_options, opt_index);

    while (opt != -1) {
        switch (opt) {
            case 'p' :
                period = atoi(optarg);
                break;
            case 'l':
                logfile = optarg;
                logging = 1;
                break;
            case 's' :
                tempType = optarg[0];
                break;
            case 'i':
                id = atoi(optarg);
                break;
            case 'h':
                host = optarg;
                break;
            case '?':
                fprintf(stderr, "Unrecognized argument, correct usage is \n");
                fprintf(stderr, "--period=INTERVAL --log=LOGFILE");
                exit(1);
        }
        opt = getopt_long(argc, argv, "", long_options, opt_index);
    }

    //PORT INITIALIZATION
    portnum = atoi(argv[optind]);



    //SOCKET
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        fprintf(stderr, "ERROR: SOCKET FAILED\n");
        exit(2);
    }
    //server testing
    struct hostent *server = gethostbyname(host);
    if(server == NULL){
        fprintf(stderr, "ERROR: NO SUCH HOST\n");
        exit(1);
    }

    //connection

    struct sockaddr_in addr;
    addr.sin_port = htons(portnum);
    addr.sin_family = AF_INET;
    memcpy( (char *) &addr.sin_addr.s_addr, (char *) server->h_addr, server->h_length);

    if(connect(sockfd, (struct sockaddr*) &addr, sizeof(addr)) < 0){
        fprintf(stderr, "ERROR: SERVER CONNECTION FAILED\n");
        exit(2);
    }

    //initialize ssl lib
    if (SSL_library_init() < 0) {
        fprintf(stderr, "ERROR IN OPENSSL INITIALIZATION\n");
        exit(2);
    }
    OpenSSL_add_all_algorithms();
    //create ssl context
    ssl_context = SSL_CTX_new(TLSv1_client_method());
    if(ssl_context == NULL) {
        fprintf(stderr, "ERROR: SSL CONTEXT\n");
        exit(2);
    }
    //set file descriptor & connect
    ssl = SSL_new(ssl_context);
    if(SSL_set_fd(ssl, sockfd) == 0) {
        fprintf(stderr, "ERROR SOCKFD AND SSL SOCKET\n");
        exit(2);
    }
    if(SSL_connect(ssl) != 1) {
        fprintf(stderr, "SSL CONNECTION ERROR\n");
        exit(2);
    }


    if (strlen(logfile) != 0)
    {
        log_val = creat(logfile, 0666);
        if (log_val < 0) {
            fprintf(stderr, "error creating logfile");
            exit(1);
        }
    }

    //IMMEDIATELY REPORT ID:
    dprintf(sockfd, "ID=%d\n", id);
    if (logging){
        char s_id[10];
        sprintf(s_id, "%d", id);
        write(log_val, "ID=", 3);
        write(log_val, s_id,strlen(s_id));
        write(log_val,"\n", 1);
    }

    //thread stuff
    pthread_t printThread;
    if (pthread_create(&printThread, NULL, (void *)thread_function, NULL) < 0){
        perror("error creating thread");
        exit(1);
    }

    //variables for read and write
    char buffer[2048];
    int count = 0;

    struct pollfd poll_list[1];
    poll_list[0].fd = sockfd;
    poll_list[0].events = POLLIN|POLLHUP|POLLERR;




    int command_index = 0;


    mraa_gpio_context button;
    button = mraa_gpio_init(60);
    mraa_gpio_dir(button, MRAA_GPIO_IN);
    mraa_gpio_isr(button, MRAA_GPIO_EDGE_RISING, &do_when_interrupted, NULL);
    while(true) {
        if(processedOne)
        {
            int retval = poll(poll_list, (unsigned long) 1, 0);
            if (retval < 0) {
                fprintf(stderr, "Polling Error\n");
                exit(1);
            }

            if ((poll_list[0].revents & POLLIN) == POLLIN) {
                count=SSL_read(ssl, buffer, 2048);
                int i;
                for (i = 0; i < count; i++) {
                    if (buffer[i] == 3) {
                        exit(1);
                    } else if (buffer[i] == 10 || buffer[i] == 13) {
                        command[command_index] = '\0';
                        //printf("RECEIVED: %s", command);
                        if (strcmp(command, "OFF") == 0) {
                            //printf("TURNING OFF NOW!\n");
                            if (logging) {
                                write(log_val, command, strlen(command));
                                write(log_val, "\n", 1);
                                //get time
                                time(&timer);
                                current_time = localtime(&timer);
                                strftime(time_storage, 9, "%H:%M:%S", current_time);

                                write(log_val, time_storage, strlen(time_storage));
                                write(log_val, " SHUTDOWN", 9);
                            }
                            exit(0);
                        } else if (strcmp(command, "STOP") == 0) {
                            running = 0;
                            if (logging) {
                                write(log_val, command, strlen(command));
                                write(log_val, "\n", 1);
                            }
                        } else if (strcmp(command, "START") == 0) {
                            running = 1;
                            //printf("START\n");
                            if (logging) {
                                write(log_val, command, strlen(command));
                                write(log_val, "\n", 1);
                            }
                        } else if (strcmp(command, "SCALE=F") == 0) {
                            tempType = 'F';
                            if (logging) {
                                write(log_val, command, strlen(command));
                                write(log_val, "\n", 1);
                            }
                        } else if (strcmp(command, "SCALE=C") == 0) {
                            tempType = 'C';
                            if (logging) {
                                write(log_val, command, strlen(command));
                                write(log_val, "\n", 1);
                            }
                        } else {
                            //check for period
                            char substr[8];
                            memcpy(substr, &command[0], 7);
                            substr[7] = '\0';
                            if (strcmp(substr, "PERIOD=") == 0) {

                                char period_buffer[1018];
                                memcpy(period_buffer, &command[7], 1017);
                                period_buffer[1017] = '\0';

                                int newTime = atoi(period_buffer);
                                //printf("%i \n", newTime);
                                period = newTime;
                                if (logging) {
                                    write(log_val, command, strlen(command));
                                    write(log_val, "\n", 1);
                                }

                            }
                        }
                        strcpy(command, "");
                        command_index = 0;
                    } else {
                        command[command_index] = buffer[i];
                        command_index += 1;
                    }
                }
            }
        }
    }
}
