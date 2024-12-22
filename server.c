#include <sqlite3.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#define PORT 2908
#define USERS_FILE "users.txt"

extern int errno;

typedef struct thData
{
    int idThread;
    int cl;
    sqlite3 *db;
} thData;

static void *treat(void *);
void comenziDeLaClient(void *, int client, int nr_thread);

sqlite3 *init_database(const char *db_name)
{
    sqlite3 *db;
    int rc = sqlite3_open(db_name, &db);
    if (rc)
    {
        fprintf(stderr, "Nu s-a putut deschide baza de date:%s\n", sqlite3_errmsg(db));
        exit(1);
    }
    printf("Baza de date %s a fost deschisă cu succes.\n", db_name);
    return db;
}

void check_error(int rc, sqlite3 *db, const char *msg)
{
    if (rc != SQLITE_OK && rc != SQLITE_DONE)
    {
        fprintf(stderr, "Eroare la %s: %s\n", msg, sqlite3_errmsg(db));
        exit(1);
    }
}

void create_tables(sqlite3 *db)
{
    const char *user_table_sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "username VARCHAR(100) NOT NULL UNIQUE, "
        "password VARCHAR(256) NOT NULL); ";

    const char *transaction_table_sql =
        "CREATE TABLE IF NOT EXISTS transactions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "user_id INTEGER NOT NULL, "
        "amount REAL NOT NULL, "
        "description TEXT, "
        "FOREIGN KEY (user_id) REFERENCES users(id)); ";

    check_error(sqlite3_exec(db, user_table_sql, NULL, NULL, NULL), db, "Creare tabel utilizatori");
    check_error(sqlite3_exec(db, transaction_table_sql, NULL, NULL, NULL), db, "Creare tabel tranzacții");
    printf("Tabelele au fost create/verificate cu succes.\n");
}

void add_user(sqlite3 *db, const char *name, const char *password)
{
    if (name == NULL || password == NULL || strlen(name) == 0 || strlen(password) == 0)
    {
        printf("Eroare: Numele de utilizator sau parola sunt invalide.\n");
        return;
    }

    const char *sql = "INSERT INTO users (username, password) VALUES(?, ?);";
    sqlite3_stmt *stmt;

    check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query");
    printf("Adăugăm utilizator: %s, %s\n", name, password);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    check_error(sqlite3_step(stmt), db, "Executare query");
    printf("Utilizator adăugat cu succes.\n");

    sqlite3_finalize(stmt);
}

void add_transaction(sqlite3 *db, int user_id, double amount, const char *description)
{

    const char *sql = "INSERT INTO transactions (user_id, amount, description) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt;

    check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query");

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_double(stmt, 2, amount);
    sqlite3_bind_text(stmt, 3, description, -1, SQLITE_STATIC);

    check_error(sqlite3_step(stmt), db, "Executare query");
    // de trimis la client
    printf("Tranzacție adăugată cu succes.\n");
    sqlite3_finalize(stmt);
}

void get_transactions(sqlite3 *db, int user_id)
{

    const char *sql =
        "SELECT id, amount, transaction_date, description FROM transactions WHERE user_id = ?;";

    sqlite3_stmt *stmt;

    check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query");
    sqlite3_bind_int(stmt, 1, user_id);

    printf("Tranzacții pentru utilizatorul cu ID-ul %d:\n", user_id);
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int id = sqlite3_column_int(stmt, 0);
        double amount = sqlite3_column_double(stmt, 1);

        const char *date = (const char *)sqlite3_column_text(stmt, 2);
        const char *description = (const char *)sqlite3_column_text(stmt, 3);
        // de trimis la client
        printf("ID: %d | Suma: %.2f | Data: %s | Descriere: %s\n", id, amount, date, description);
    }

    sqlite3_finalize(stmt);
}
int login_user(sqlite3 *db, const char *username, const char *password)
{
    const char *sql = "SELECT COUNT(*) FROM users WHERE username = ? AND password = ?;";
    sqlite3_stmt *stmt;
    int result = 0;

    printf("Pregătire query login...\n");

    check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query login");

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    printf("Executare query...\n");

    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        result = sqlite3_column_int(stmt, 0); // Dacă COUNT(*) > 0, utilizatorul există și parola este corectă
    }

    sqlite3_finalize(stmt);
    return result > 0; // Returnăm 1 dacă utilizatorul este găsit, altfel 0
}

void comenziDeLaClient(void *arg, int client, int nr_thread)
{
    thData tdL = *((thData *)arg);
    char buffer[1024];

    while (1)
    {
        bzero(buffer, 1024);

        if (read(tdL.cl, buffer, 1024) <= 0)
        {
            printf("[Thread %d] Clientul a inchis conexiunea.\n", tdL.idThread);
            break;
        }

        /*if (strncmp(buffer, "ERROR:", 6) == 0)
        {
            printf("[Thread %d] Eroare raportată de client: %s\n", tdL.idThread, buffer + 6);
            continue;
        }*/

        printf("[Thread %d] Comanda primita: %s\n", tdL.idThread, buffer);

        char mesaj[1024];
        // TIPURI DE COMENZI
        if (strcmp(buffer, "AFISEAZA SOLDUL") == 0)
        {

            snprintf(mesaj, sizeof(mesaj), "SUCCESS AFISEAZA SOLDUL");
        }
        else if (strcmp(buffer, "ANALIZA FINANCIARA") == 0)
        {

            snprintf(mesaj, sizeof(mesaj), "SUCCESS ANALIZA FINANCIARA");
        }
        else if (strcmp(buffer, "SUGESTII ECONOMISIRE") == 0)
        {

            snprintf(mesaj, sizeof(mesaj), "SUCCESS SUGESTII ECONOMISIRE");
        }
        else if (strcmp(buffer, "LIST_TRANSACTIONS") == 0)
        {

            snprintf(mesaj, sizeof(mesaj), "SUCCESS LIST_TRANSACTIONS");
        }
        else if (strstr(buffer, "CHELTUIALA") != NULL)
        {
            char type[1024], value[1024];

            sscanf(buffer + 15, "%[^:]:%s", type, value);
            float cost = strtof(value, NULL);

            snprintf(mesaj, sizeof(mesaj), "SUCCESS ADD CHELTUIALA: Type = %s, Value = %.2f", type, cost);
        }
        else if (strstr(buffer, "VENIT") != NULL)
        {
            char type[1024], value[1024];

            sscanf(buffer + 10, "%[^:]:%s", type, value);
            float cost = strtof(value, NULL);

            snprintf(mesaj, sizeof(mesaj), "SUCCESS ADD VENIT: Type = %s, Value = %.2f", type, cost);
        }

        else if (strcmp(buffer, "QUIT") == 0)
        {
            snprintf(mesaj, sizeof(mesaj), "Confirmare: dQUIT");
            write(tdL.cl, mesaj, strlen(mesaj) + 1);
            close(tdL.cl);
            free(arg);
            return;
        }
        else
        {
            snprintf(mesaj, sizeof(mesaj), "Comanda necunoscuta.");
        }

        if (write(tdL.cl, mesaj, strlen(mesaj) + 1) <= 0)
        {
            printf("[Thread %d] Eroare la trimiterea mesajului catre client.\n", tdL.idThread);
            break;
        }
    }

    close(tdL.cl);
    free(arg);
}

static void *treat(void *arg)
{
    thData tdL = *((thData *)arg);
    sqlite3 *db = tdL.db;
    pthread_detach(pthread_self());

    char buffer[1024];
    int ok = 0;
    if (read(tdL.cl, &buffer, sizeof(buffer)) <= 0)
    {

        perror("[server] Eroare - read() la inceputul conexiunii.");
        close(tdL.cl);
        free(arg);
        return NULL;
    }
    // printf("Ce e in buffer %s \n", buffer);

    if (strncmp(buffer, "SIGNUP:", 7) == 0)
    {
        char username[1024], password[1024];
        sscanf(buffer + 7, "%[^:]:%s", username, password);
        printf("%s %s", username, password);
        add_user(db, username, password);
        ok = 1;
    }
    else if (strncmp(buffer, "LOGIN:", 6) == 0)
    {
        char username[1024], password[1024];
        sscanf(buffer + 6, "%[^:]:%s", username, password);

        if (login_user(db, username, password))
        {
            ok = 1;
        }
        else
        {
            ok = 0;
        }
    }
    if (ok)
    {
        char mesaj[256];
        strcpy(mesaj, "succes");
        write(tdL.cl, mesaj, strlen(mesaj) + 1);
        
        comenziDeLaClient(arg, tdL.cl, tdL.idThread);
    }

    close(tdL.cl);
    // free(arg);
    return NULL;
}
int main()
{
    struct sockaddr_in server;
    struct sockaddr_in from;
    int sd;
    pthread_t th[100];
    int i = 0;

    sqlite3 *db = init_database("database.db");
    if (db == NULL)
    {
        printf("Eroare la deschiderea bazei de date.\n");
        exit(1);
    }
    create_tables(db);

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("[server] Eroare la socket().\n");
        return errno;
    }

    int on = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    if (bind(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[server] Eroare la bind().\n");
        return errno;
    }

    if (listen(sd, 10) == -1)
    {
        perror("[server] Eroare la listen().\n");
        return errno;
    }

    while (1)
    {
        int client;
        thData *td;
        socklen_t length = sizeof(from);

        printf("[server] Asteptam la portul %d...\n", PORT);
        fflush(stdout);

        if ((client = accept(sd, (struct sockaddr *)&from, &length)) < 0)
        {
            perror("[server] Eroare la accept().\n");
            continue;
        }

        td = (thData *)malloc(sizeof(thData));
        td->idThread = i++;
        td->cl = client;
        td->db = db;

        pthread_create(&th[i], NULL, &treat, td);
    }

    sqlite3_close(db);
}