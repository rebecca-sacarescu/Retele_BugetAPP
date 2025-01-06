#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 2908
#define SERVER_ADDR "127.0.0.1"

void meniu_principal();
int conectLaServer(const char *serverAddr, int port);
int signup(int sd);
int loginUser(int sd);
void meniu_account();
void comenziClient(int sd);

int main()
{
    while (1)
    {
        char comanda[1024];
        meniu_principal();

        printf("Introduceti o comandă: ");
        fgets(comanda, sizeof(comanda), stdin);
        comanda[strcspn(comanda, "\n")] = '\0';

        if (strcmp(comanda, "LOGIN") == 0)
        {
            int sd = conectLaServer(SERVER_ADDR, PORT);
            if (loginUser(sd) == 0)
            {
                meniu_account();
                comenziClient(sd);
            }
            close(sd);
        }
        else if (strcmp(comanda, "SIGNUP") == 0)
        {
            int sd = conectLaServer(SERVER_ADDR, PORT);
            if (signup(sd) == 0)
            {
                printf("Inregistrare reusita! Va rugam sa va autentificati.\n");
            }
            close(sd);
        }
        else if (strcmp(comanda, "QUIT") == 0)
        {
            printf("Ieșire din aplicatie.\n");
            break;
        }
        else
        {
            printf("Comanda invalida. Incercati din nou.\n");
        }
    }
    return 0;
}

void meniu_principal()
{
    printf("-----------------------------------MENIU PRINCIPAL-----------------------------------------------\n");
    printf("LOGIN\n");
    printf("SIGNUP\n");
    printf("QUIT\n");
    printf("-------------------------------------------------------------------------------------------------\n");
}

int conectLaServer(const char *serverAddr, int port)
{
    struct sockaddr_in server;
    int sd;

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Eroare - socket()");
        exit(EXIT_FAILURE);
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(serverAddr);

    if (connect(sd, (struct sockaddr *)&server, sizeof(server)) == -1)
    {
        perror("Eroare - connect()");
        exit(EXIT_FAILURE);
    }

    return sd;
}

int signup(int sd)
{
    char username[1024], parola[1024], signupData[1024], raspuns[1024];

    printf("Introduceti username pentru SIGNUP: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = '\0';

    printf("Introduceti parola pentru SIGNUP: ");
    fgets(parola, sizeof(parola), stdin);
    parola[strcspn(parola, "\n")] = '\0';

    snprintf(signupData, sizeof(signupData), "SIGNUP:%s:%s", username, parola);

    if (write(sd, signupData, strlen(signupData) + 1) <= 0)
    {
        perror("Eroare - write() pentru SIGNUP");
        return -1;
    }

    if (read(sd, raspuns, sizeof(raspuns)) > 0)
    {
        if (strcmp(raspuns, "succes") == 0)
        {
            return 0;
        }
        printf("Inregistrare esuata: %s\n", raspuns);
    }
    else
    {
        perror("Eroare - read() pentru SIGNUP");
    }
    return -1;
}

int loginUser(int sd)
{
    char username[1024], parola[1024], loginData[1024], raspuns[1024];

    printf("Introduceți username: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = '\0';

    printf("Introduceti parola: ");
    fgets(parola, sizeof(parola), stdin);
    parola[strcspn(parola, "\n")] = '\0';

    snprintf(loginData, sizeof(loginData), "LOGIN:%s:%s", username, parola);

    if (write(sd, loginData, strlen(loginData) + 1) <= 0)
    {
        perror("Eroare - write() pentru LOGIN");
        return -1;
    }

    if (read(sd, raspuns, sizeof(raspuns)) > 0)
    {
        if (strcmp(raspuns, "succes") == 0)
        {
            printf("Autentificare reusita!\n");
            return 0;
        }
        printf("Autentificare esuata: %s\n", raspuns);
    }
    else
    {
        perror("Eroare - read() pentru LOGIN");
    }
    return -1;
}

void meniu_account() {
    printf("-----------------------------------------CONT------------------------------------------------------\n");
    printf("AFISEAZA SOLDUL\n");
    printf("AFISEAZA BUDGET\n");
    printf("ADD TRANSACTION <type:valoare:descriere>\n");
    printf("ADD VENIT <valoare:descriere>\n");
    printf("CATEGORII\n");
    printf("LUNA\n");
    printf("ANALIZA FINANCIARA\n");
    printf("LIST_TRANSACTIONS\n");
    printf("SUGESTII ECONOMISIRE\n");
    printf("QUIT\n");
    printf("-------------------------------------------------------------------------------------------------\n");
}

void golesteSocket(int sd)
{
    char buffer[1024];
    ssize_t bytes_read;

    // Setăm socketul în mod non-blocant
    int flags = fcntl(sd, F_GETFL, 0);
    fcntl(sd, F_SETFL, flags | O_NONBLOCK);

    // Citim toate datele disponibile din socket
    while ((bytes_read = read(sd, buffer, sizeof(buffer))) > 0)
    {
        // Doar consumăm datele, fără a face nimic cu ele
    }

    // Verificăm erorile, dar ignorăm dacă nu sunt date disponibile
    if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
    {
        perror("Eroare la golirea socketului");
    }

    // Revenim la modul blocant
    fcntl(sd, F_SETFL, flags);
}


void comenziClient(int sd)
{
    char comanda[1024];
    char buffer[4096];

    while (1)
    {
        printf("Introduceti comanda: ");
        fgets(comanda, sizeof(comanda), stdin);
        comanda[strcspn(comanda, "\n")] = '\0'; // Eliminăm newline-ul

        if (strcmp(comanda, "QUIT") == 0)
        {
            printf("Ieșire din cont.\n");
            break;
        }

        // Golește socketul de mesaje anterioare
        golesteSocket(sd);

        // Trimite comanda către server
        if (write(sd, comanda, strlen(comanda) + 1) <= 0)
        {
            perror("Eroare - write()");
            break;
        }

        // Citim răspunsurile din server
        ssize_t bytes_read;
        do
        {
            memset(buffer, 0, sizeof(buffer)); // Golește bufferul înainte de citire
            bytes_read = read(sd, buffer, sizeof(buffer) - 1);

            if (bytes_read > 0)
            {
                buffer[bytes_read] = '\0'; // Null-terminate pentru siguranță
                printf("%s \n", buffer);     // Afișăm răspunsul primit
                
            }
            else if (bytes_read < 0)
            {
                perror("Eroare - read()");
                break;
            }
        } while (bytes_read == sizeof(buffer) - 1); // Continuăm citirea dacă bufferul e plin
    }
}
