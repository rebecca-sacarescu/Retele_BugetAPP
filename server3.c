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
    int user_id;
} thData;

static void *treat(void *);
void comenziDeLaClient(void *, int client, int nr_thread, sqlite3 *db, int user);

/*                      CHESTII DE BD                            */
sqlite3 *init_database(const char *db_name)
{
    // check_error(sqlite3_exec(db_name, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL), db, "Setare journal_mode");

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
        "password VARCHAR(256) NOT NULL, "
        "budget REAL DEFAULT 0.0);";

    const char *transaction_table_sql =
        "CREATE TABLE IF NOT EXISTS transactions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "user_id INTEGER NOT NULL, "
        "amount REAL NOT NULL, "
        "description TEXT, "
        "type TEXT, "
        "month INTEGER, "
        "FOREIGN KEY (user_id) REFERENCES users(id)); ";

    const char *income_table_sql =
        "CREATE TABLE IF NOT EXISTS incomes ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "user_id INTEGER NOT NULL, "
        "amount REAL NOT NULL, "
        "description TEXT, "
        "FOREIGN KEY (user_id) REFERENCES users(id));";

    check_error(sqlite3_exec(db, income_table_sql, NULL, NULL, NULL), db, "Creare tabel venituri");
    check_error(sqlite3_exec(db, user_table_sql, NULL, NULL, NULL), db, "Creare tabel utilizatori");
    check_error(sqlite3_exec(db, transaction_table_sql, NULL, NULL, NULL), db, "Creare tabel tranzacții");
    printf("Tabelele au fost create/verificate cu succes.\n");
}

/*                                    USERS                             */
void add_user(sqlite3 *db, const char *name, const char *password, int client_socket)
{
    char mesaj[1024];
    if (name == NULL || password == NULL || strlen(name) == 0 || strlen(password) == 0)
    {
        snprintf(mesaj, sizeof(mesaj), "Eroare: Numele de utilizator sau parola sunt invalide.\n");
        if (write(client_socket, mesaj, strlen(mesaj) + 1) <= 0)
    {
        perror("Eroare la trimiterea mesajului către client");
    }
        return;
    }

    const char *sql = "INSERT INTO users (username, password) VALUES(?, ?);";
    sqlite3_stmt *stmt;

    check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query");
    printf("Adăugăm utilizator: %s, %s\n", name, password);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    check_error(sqlite3_step(stmt), db, "Executare query");
    snprintf(mesaj, sizeof(mesaj), "Utilizator adăugat cu succes.\n");
    if (write(client_socket, mesaj, strlen(mesaj) + 1) <= 0)
    {
        perror("Eroare la trimiterea mesajului către client");
    }

    sqlite3_finalize(stmt);
}

int login_user(sqlite3 *db, const char *username, const char *password)
{
    const char *sql = "SELECT COUNT(*) FROM users WHERE username = ? AND password = ?;";
    sqlite3_stmt *stmt;
    int result = 0;

    check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query login");

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);


    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        result = sqlite3_column_int(stmt, 0); // Dacă COUNT(*) > 0, utilizatorul există și parola este corectă
    }

    sqlite3_finalize(stmt);
    return result > 0; // Returnăm 1 dacă utilizatorul este găsit, altfel 0
}

/*                                    TRANZACTII                             */

double calculate_current_spending(sqlite3 *db, int user_id)
{
    const char *sql = "SELECT SUM(amount) FROM transactions WHERE user_id = ?;";
    sqlite3_stmt *stmt;
    double total_spent = 0.0;

    check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query pentru calculul cheltuielilor");
    sqlite3_bind_int(stmt, 1, user_id);

    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        total_spent = sqlite3_column_double(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return total_spent;
}

void add_transaction(sqlite3 *db, int user_id, const char *type, double amount, const char *description, int client_socket)
{
    const char *budget_query = "SELECT budget FROM users WHERE id = ?;";
    sqlite3_stmt *stmt;
    double budget = 0.0;

    srand(time(NULL));
    int random_month = rand() % 12 + 1;

    check_error(sqlite3_prepare_v2(db, budget_query, -1, &stmt, NULL), db, "Pregătire query pentru obținerea bugetului");
    sqlite3_bind_int(stmt, 1, user_id);

    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        budget = sqlite3_column_double(stmt, 0);
    }
    sqlite3_finalize(stmt);

    double current_spent = calculate_current_spending(db, user_id);
    char mesaj[1024];

    if (current_spent + amount > budget)
    {
        snprintf(mesaj, sizeof(mesaj), "Eroare: Tranzacția depășește bugetul disponibil pentru utilizatorul cu ID-ul %d.\n", user_id);
    }
    else
    {
        const char *sql = "INSERT INTO transactions (user_id, type, amount, description, month) VALUES (?, ?, ?, ?, ?);";
        check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query pentru adăugarea tranzacției");

        sqlite3_bind_int(stmt, 1, user_id);
        sqlite3_bind_text(stmt, 2, type, -1, SQLITE_STATIC);
        sqlite3_bind_double(stmt, 3, amount);
        sqlite3_bind_text(stmt, 4, description, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 5, random_month);

        check_error(sqlite3_step(stmt), db, "Executare query pentru adăugarea tranzacției");
        sqlite3_finalize(stmt);

        snprintf(mesaj, sizeof(mesaj), "Tranzacție adăugată cu succes.\n");
    }

    if (write(client_socket, mesaj, strlen(mesaj) + 1) <= 0)
    {
        perror("Eroare la trimiterea mesajului către client");
    }

}



void get_transactions(sqlite3 *db, int user_id, int client_socket)
{
    const char *sql =
        "SELECT id, type, amount, description FROM transactions WHERE user_id = ?;";

    sqlite3_stmt *stmt;

    check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query");
    sqlite3_bind_int(stmt, 1, user_id);

    char buffer_mare[8192] = ""; // Buffer mare pentru toate tranzacțiile.
    char mesaj[1024];
    int exista_tranzactii = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        exista_tranzactii = 1;
        int id = sqlite3_column_int(stmt, 0);
        const char *type = (const char *)sqlite3_column_text(stmt, 1);
        double amount = sqlite3_column_double(stmt, 2);
        const char *description = (const char *)sqlite3_column_text(stmt, 3);

        snprintf(mesaj, sizeof(mesaj), "ID: %d | Tip: %s | Suma: %.2f | Descriere: %s\n", id, type, amount, description);
        strcat(buffer_mare, mesaj);

        // Verificăm să nu depășim dimensiunea bufferului
        if (strlen(buffer_mare) + sizeof(mesaj) >= sizeof(buffer_mare))
        {
            perror("Prea multe tranzacții pentru bufferul alocat");
            break;
        }
    }

    if (!exista_tranzactii)
    {
        snprintf(buffer_mare, sizeof(buffer_mare), "Nu există tranzacții pentru utilizatorul cu ID-ul %d.\n", user_id);
    }

    sqlite3_finalize(stmt);

    if (write(client_socket, buffer_mare, strlen(buffer_mare) + 1) <= 0)
    {
        perror("Eroare la trimiterea mesajului către client");
    }
}


void add_venit(sqlite3 *db, int user_id, double amount, const char *description, int client_socket)
{
    const char *sql = "INSERT INTO incomes (user_id, amount, description) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt;

    check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query pentru adăugarea venitului");

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_double(stmt, 2, amount);
    sqlite3_bind_text(stmt, 3, description, -1, SQLITE_STATIC);

    check_error(sqlite3_step(stmt), db, "Executare query pentru adăugarea venitului");

    char mesaj[1024];
    snprintf(mesaj, sizeof(mesaj), "Venit adăugat cu succes \n");
    write(client_socket, mesaj, strlen(mesaj) + 1);

    sqlite3_finalize(stmt);
}

/*                                    BUGET                             */
void update_budget(sqlite3 *db, int user_id, int client_socket)
{
    const char *sql_income = "SELECT SUM(amount) FROM incomes WHERE user_id = ?;";
    sqlite3_stmt *stmt;
    double total_income = 0.0;

    check_error(sqlite3_prepare_v2(db, sql_income, -1, &stmt, NULL), db, "Pregatire query pentru calculul bugetului");
    sqlite3_bind_int(stmt, 1, user_id);

    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        total_income = sqlite3_column_double(stmt, 0); 
    }
    sqlite3_finalize(stmt);

    
    const char *sql_update_budget = "UPDATE users SET budget = ? WHERE id = ?;";
    check_error(sqlite3_prepare_v2(db, sql_update_budget, -1, &stmt, NULL), db, "Pregatire query pentru actualizarea bugetului");

    sqlite3_bind_double(stmt, 1, total_income); 
    sqlite3_bind_int(stmt, 2, user_id);         

    check_error(sqlite3_step(stmt), db, "Executare query pentru actualizarea bugetului");
    sqlite3_finalize(stmt);

    printf("Bugetul a fost actualizat cu succes pentru utilizatorul cu ID-ul %d.\n", user_id);
    /*char mesaj[256];
    snprintf(mesaj, sizeof(mesaj), "Noul buget este: %.2f\n", total_income);

    // Trimite mesajul clientului
    if (write(client_socket, mesaj, strlen(mesaj) + 1) <= 0)
    {
        perror("Eroare la trimiterea mesajului către client");
    }*/
}

void get_budget(sqlite3 *db, int user_id, int client_socket)
{
    const char *sql = "SELECT budget FROM users WHERE id = ?;";
    sqlite3_stmt *stmt;

    check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query pentru obținere buget");
    sqlite3_bind_int(stmt, 1, user_id);

    char buffer_mare[1024] = ""; // Buffer mare pentru toate mesajele.
    char mesaj[256];

    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        double budget = sqlite3_column_double(stmt, 0);
        snprintf(mesaj, sizeof(mesaj), "Buget: %.2f\n", budget);
        strcat(buffer_mare, mesaj);
    }
    else
    {
        snprintf(mesaj, sizeof(mesaj), "[Eroare] Nu există înregistrări pentru user_id: %d\n", user_id);
        strcat(buffer_mare, mesaj);
    }

    sqlite3_finalize(stmt);

    if (write(client_socket, buffer_mare, strlen(buffer_mare) + 1) <= 0)
    {
        perror("Eroare la trimiterea mesajului către client");
    }
}


void transaction_type(sqlite3 *db, int user_id, int client_socket)
{
    const char *sql =
        "SELECT type, SUM(amount) as total "
        "FROM transactions "
        "WHERE user_id = ? "
        "GROUP BY type;";

    sqlite3_stmt *stmt;
    check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query pentru tranzacțiile grupate după tip");
    sqlite3_bind_int(stmt, 1, user_id);

    char buffer_mare[4096] = "";
    char mesaj[256];

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char *type = (const char *)sqlite3_column_text(stmt, 0);
        double total = sqlite3_column_double(stmt, 1);

        snprintf(mesaj, sizeof(mesaj), "Tip: %s | Total: %.2f\n", type, total);
        strcat(buffer_mare, mesaj);
    }

    sqlite3_finalize(stmt);

    if (write(client_socket, buffer_mare, strlen(buffer_mare) + 1) <= 0)
    {
        perror("Eroare la trimiterea rezultatului către client");
    }
}

void transaction_month(sqlite3 *db, int user_id, int client_socket)
{
    const char *sql =
        "SELECT month, SUM(amount) as total "
        "FROM transactions "
        "WHERE user_id = ? "
        "GROUP BY month;";

    sqlite3_stmt *stmt;
    check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query pentru tranzacțiile grupate după lună");
    sqlite3_bind_int(stmt, 1, user_id);

    const char *months[] = {"Ianuarie", "Februarie", "Martie", "Aprilie", "Mai", "Iunie",
                            "Iulie", "August", "Septembrie", "Octombrie", "Noiembrie", "Decembrie"};

    char buffer_mare[4096] = ""; // Buffer mare pentru concatenare
    char mesaj[256]; // Buffer temporar pentru fiecare mesaj

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int month = sqlite3_column_int(stmt, 0);
        double total = sqlite3_column_double(stmt, 1);

        if (month >= 1 && month <= 12)
        {
            snprintf(mesaj, sizeof(mesaj), "Luna: %s | Total: %.2f\n", months[month - 1], total);
            strcat(buffer_mare, mesaj); // Adaugă mesajul în bufferul mare
        }
    }

    sqlite3_finalize(stmt);

    // Trimite tot conținutul bufferului mare o singură dată
    if (write(client_socket, buffer_mare, strlen(buffer_mare) + 1) <= 0)
    {
        perror("Eroare la trimiterea rezultatului către client");
    }
}

void financial_analysis(sqlite3 *db, int user_id, int client_socket)
{
    const char *sql = "SELECT type, SUM(amount) FROM transactions WHERE user_id = ? GROUP BY type;";
    sqlite3_stmt *stmt;
    double total_spent = 0.0;
    double category_sums[3] = {0}; // Pentru Food, Transport, Shopping
    const char *categories[3] = {"Food", "Transport", "Shopping"};
    double max_spent = 0.0;
    const char *max_category = NULL;

    // Pregătește query-ul pentru a calcula suma per categorie
    check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query analiză financiară");
    sqlite3_bind_int(stmt, 1, user_id);

    // Iterează peste rezultate
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char *type = (const char *)sqlite3_column_text(stmt, 0);
        double sum = sqlite3_column_double(stmt, 1);

        total_spent += sum;

        for (int i = 0; i < 3; i++)
        {
            if (strcmp(type, categories[i]) == 0)
            {
                category_sums[i] = sum;
                break;
            }
        }
    }

    // Determină categoria cu cele mai mari cheltuieli
    for (int i = 0; i < 3; i++)
    {
        if (category_sums[i] > max_spent)
        {
            max_spent = category_sums[i];
            max_category = categories[i];
        }
    }

    sqlite3_finalize(stmt);

    // Construim mesajul complet
    char mesaj[4096]; // Buffer mare pentru toate mesajele
    int offset = 0;

    offset += snprintf(mesaj + offset, sizeof(mesaj) - offset, 
                       "Analiză financiară pentru utilizatorul cu ID-ul %d:\n", user_id);

    if (max_category != NULL)
    {
        offset += snprintf(mesaj + offset, sizeof(mesaj) - offset, 
                           "Categoria cu cele mai mari cheltuieli: %s (%.2f RON)\n", 
                           max_category, max_spent);
    }

    for (int i = 0; i < 3; i++)
    {
        double percentage = total_spent > 0 ? (category_sums[i] / total_spent) * 100.0 : 0.0;
        offset += snprintf(mesaj + offset, sizeof(mesaj) - offset, 
                           "Categorie: %s | Cheltuieli: %.2f RON | Procentaj: %.2f%%\n", 
                           categories[i], category_sums[i], percentage);
    }

    // Trimite mesajul complet
    if (write(client_socket, mesaj, strlen(mesaj) + 1) <= 0)
    {
        perror("Eroare la trimiterea mesajului către client");
    }
}



void show_balance(sqlite3 *db, int user_id, int client_socket)
{
    update_budget(db, user_id, client_socket);

    double total_income = 0.0;
    double total_spent = calculate_current_spending(db, user_id);

    
    const char *income_query = "SELECT SUM(amount) FROM incomes WHERE user_id = ?;";
    sqlite3_stmt *stmt;
    check_error(sqlite3_prepare_v2(db, income_query, -1, &stmt, NULL), db, "Pregătire query pentru calculul veniturilor");
    sqlite3_bind_int(stmt, 1, user_id);

    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        total_income = sqlite3_column_double(stmt, 0);
    }
    sqlite3_finalize(stmt);

    
    double balance = total_income - total_spent;

    char mesaj[256];
    //snprintf(mesaj, sizeof(mesaj), "Soldul pentru utilizatorul cu ID-ul %d este: %.2f\n", user_id, balance);
    snprintf(mesaj, sizeof(mesaj), "Soldul  este: %.2f\n", balance);

    if (write(client_socket, mesaj, strlen(mesaj) + 1) <= 0)
    {
        perror("Eroare la trimiterea mesajului către client");
    }
}

/*                                    COMENZI                             */

void comenziDeLaClient(void *arg, int client, int nr_thread, sqlite3 *db, int user)
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
        printf("[Thread %d] Comanda primita: %s\n", tdL.idThread, buffer);

        char mesaj[1024];
        if (strcmp(buffer, "AFISEAZA SOLDUL") == 0)
        {
            show_balance(db, user, tdL.cl);
        }
        else if (strcmp(buffer, "CATEGORII") == 0)
        {
            transaction_type(db, user, tdL.cl);
        }
        else if (strcmp(buffer, "LUNA") == 0)
        {
            transaction_month(db, user, tdL.cl);
        }
        else if (strcmp(buffer, "AFISEAZA BUDGET") == 0)
        {

            update_budget(db, user, tdL.cl);
            get_budget(db, user, tdL.cl);
        }
        else if (strcmp(buffer, "ANALIZA FINANCIARA") == 0)
        {

            financial_analysis(db, user, tdL.cl);
        }
        else if (strcmp(buffer, "LIST_TRANSACTIONS") == 0)
        {
            get_transactions(db, user, tdL.cl);
        }
        else if (strstr(buffer, "TRANSACTION") != NULL)
        {
            char type[1024], value[1024], description[1024];

            sscanf(buffer + 16, "%[^:]:%[^:]:%[^\n]", type, value, description);

            double amount = strtod(value, NULL);

            update_budget(db, user, tdL.cl);
            add_transaction(db, user, type, amount, description, tdL.cl);
        }
        else if (strstr(buffer, "VENIT") != NULL)
        {
            char description[1024], value[1024];

            sscanf(buffer + 10, "%[^:]:%s", value, description);
            double amount = strtod(value, NULL);

            add_venit(db, user, amount, description, tdL.cl);
        }
        else if (strcmp(buffer, "SUGESTII ECONOMISIRE") == 0)
        {
        }
        else if (strcmp(buffer, "QUIT") == 0)
        {
            snprintf(mesaj, sizeof(mesaj), "Confirmare: dQUIT");
            tdL.user_id = -1;
            write(tdL.cl, mesaj, strlen(mesaj) + 1);
            close(tdL.cl);
            free(arg);
            return;
        }
       /* else
        {
            snprintf(mesaj, sizeof(mesaj), "Comanda necunoscuta.");
        }

        if (write(tdL.cl, mesaj, strlen(mesaj) + 1) <= 0)
        {
            printf("[Thread %d] Eroare la trimiterea mesajului catre client.\n", tdL.idThread);
            break;
        }*/
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
    int user_id;
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
        add_user(db, username, password, tdL.cl);

        const char *sql = "SELECT id FROM users WHERE username = ?;";
        sqlite3_stmt *stmt;
        check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Căutare user_id după signup");
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            tdL.user_id = sqlite3_column_int(stmt, 0);
            printf("[Thread %d] Utilizator creat cu succes. ID: %d\n", tdL.idThread, tdL.user_id);
        }
        sqlite3_finalize(stmt);

        ok = 1;
    }
    else if (strncmp(buffer, "LOGIN:", 6) == 0)
    {
        char username[1024], password[1024];
        sscanf(buffer + 6, "%[^:]:%s", username, password);

        if (login_user(db, username, password))
        {
            // Căutăm user_id în baza de date
            const char *sql = "SELECT id FROM users WHERE username = ?;";
            sqlite3_stmt *stmt;
            check_error(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL), db, "Pregătire query login");

            sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

            user_id = -1;
            if (sqlite3_step(stmt) == SQLITE_ROW)
            {
                user_id = sqlite3_column_int(stmt, 0);
            }

            sqlite3_finalize(stmt);

            if (user_id != -1)
            {
                tdL.user_id = user_id; // Stocăm user_id în sesiunea clientului
                printf("[Thread %d] Utilizatorul %s s-a autentificat cu succes. ID: %d\n", tdL.idThread, username, user_id);
                ok = 1;
            }
            else
            {
                printf("[Thread %d] Eroare la autentificare: utilizator sau parolă incorectă.\n", tdL.idThread);
                ok = 0;
            }
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

        comenziDeLaClient(arg, tdL.cl, tdL.idThread, db, user_id);
    }
    else
    {
        char mesaj[256];
        snprintf(mesaj, sizeof(mesaj), "Autentificare esuata");
        write(tdL.cl, mesaj, strlen(mesaj) + 1);
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
