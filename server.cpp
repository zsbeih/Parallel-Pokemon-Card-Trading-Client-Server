#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <pthread.h>
#include <sqlite3.h>
#include <unordered_map>

#define SERVER_PORT 7610
#define MAX_PENDING 5
#define MAX_LINE 256

using namespace std;

struct ClientSession {
    int client_socket;
    int user_id; 
    char buffer[MAX_LINE];
    int buffer_used;
    sqlite3* db;

    ClientSession(int socket, int id, sqlite3* database) 
        : client_socket(socket), user_id(id), db(database), buffer_used(0) {
        memset(buffer, 0, MAX_LINE);
    }
};

// Executes SQL code w/ error handling
int execute_sql(sqlite3 *db, const char *sql) {
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        printf("SQL error: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    return 0;
}

// LOGIN command
void process_login_command(sqlite3 *db, char *command, ClientSession *session) {
    char username[MAX_LINE], password[MAX_LINE];
    sscanf(command, "LOGIN %s %s", username, password);


    char sql[MAX_LINE];
    snprintf(sql, sizeof(sql), 
             "SELECT ID FROM Users WHERE user_name = '%s' AND password = '%s';", 
             username, password);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        session->user_id = sqlite3_column_int(stmt, 0);  
        send(session->client_socket, "200 OK", 25, 0);
    } else {
        send(session->client_socket, "403 Wrong UserID or Password\n", 30, 0);  // Login failed
    }
    sqlite3_finalize(stmt);
}

// LOGOUT command
void process_logout_command(ClientSession *session) {
    if (session->user_id == -1) {
        send(session->client_socket, "400 No user logged in\n", 22, 0);
    } else {
        session->user_id = -1;
        send(session->client_socket, "200 OK\nLOGOUT successful\n", 26, 0);
    }
}

// DEPOSIT command
void process_deposit_command(sqlite3 *db, char *command, ClientSession *session) {
    if (session->user_id == -1) {
        send(session->client_socket, "401 Not logged in\n", 19, 0);
        return;
    }

    double amount;
    sscanf(command, "DEPOSIT %lf", &amount);

    char sql[MAX_LINE];
    snprintf(sql, sizeof(sql), "UPDATE Users SET usd_balance = usd_balance + %f WHERE ID = %d;", amount, session->user_id);
    execute_sql(db, sql);

    snprintf(sql, sizeof(sql), "SELECT usd_balance FROM Users WHERE ID = %d;", session->user_id);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        double new_balance = sqlite3_column_double(stmt, 0);
        char response[MAX_LINE];
        snprintf(response, sizeof(response), "200 OK\nDEPOSIT successful. New balance: $%.2f\n", new_balance);
        send(session->client_socket, response, strlen(response), 0);
    }
    sqlite3_finalize(stmt);
}

// WHO command
void process_who_command(sqlite3 *db, ClientSession *session) {
    // Check if the user is logged in
    if (session->user_id == -1) {
        send(session->client_socket, "401 Not logged in\n", 19, 0);
        return;
    }

    // Check if the user is root
    char sql[MAX_LINE];
    snprintf(sql, sizeof(sql), "SELECT is_root FROM Users WHERE ID = %d;", session->user_id);
    
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    
    bool is_root = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        is_root = sqlite3_column_int(stmt, 0) == 1; 
    }
    sqlite3_finalize(stmt);

    // Execute if the user is root
    if (is_root) {
        char sql_users[MAX_LINE] = "SELECT user_name FROM Users WHERE ID > 0;";
        sqlite3_prepare_v2(db, sql_users, -1, &stmt, 0);
        
        char response[MAX_LINE * 10] = "200 OK\nLogged-in users:\n";
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            strcat(response, (const char *)sqlite3_column_text(stmt, 0));
            strcat(response, "\n");
        }
        send(session->client_socket, response, strlen(response), 0);
        sqlite3_finalize(stmt);
    } else {
        send(session->client_socket, "403 Forbidden\n", 15, 0);
    }
}


// LOOKUP command
void process_lookup_command(sqlite3 *db, char *command, ClientSession *session) {
    char card_name[MAX_LINE];
    sscanf(command, "LOOKUP %s", card_name);

    char sql[MAX_LINE];
    snprintf(sql, sizeof(sql), "SELECT card_name, card_type, rarity, count FROM Pokemon_cards WHERE card_name = '%s';", card_name);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc == SQLITE_OK) {
        char response[MAX_LINE * 5] = "200 OK\nCard details:\n";
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            snprintf(response + strlen(response), MAX_LINE,
                     "Name: %s\nType: %s\nRarity: %s\nCount: %d\n",
                     sqlite3_column_text(stmt, 0),
                     sqlite3_column_text(stmt, 1),
                     sqlite3_column_text(stmt, 2),
                     sqlite3_column_int(stmt, 3));
        } else {
            send(session->client_socket, "404 Card not found\n", 19, 0);
            return;
        }
        send(session->client_socket, response, strlen(response), 0);
    }
    sqlite3_finalize(stmt);
}

// LIST command
void process_list_command(sqlite3 *db, ClientSession *session) {
    // Check if the user is logged in
    if (session->user_id == -1) {
        send(session->client_socket, "401 Not logged in\n", 19, 0);
        return;
    }

    // Check if the user is root
    char sql[MAX_LINE];
    snprintf(sql, sizeof(sql), "SELECT is_root FROM Users WHERE ID = %d;", session->user_id);
    
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    
    bool is_root = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        is_root = sqlite3_column_int(stmt, 0) == 1; 
    }
    sqlite3_finalize(stmt); 

    char sql_cards[MAX_LINE];
    if (is_root) {
        // Check if root
        snprintf(sql_cards, sizeof(sql_cards), "SELECT ID, card_name, card_type, rarity, count, owner_id FROM Pokemon_cards;");
    } else {
        // If not root, then show only the user's cards
        snprintf(sql_cards, sizeof(sql_cards), "SELECT ID, card_name, card_type, rarity, count FROM Pokemon_cards WHERE owner_id = %d;", session->user_id);
    }

    sqlite3_prepare_v2(db, sql_cards, -1, &stmt, 0);

    char response[MAX_LINE * 5];
    if (is_root) {
        snprintf(response, sizeof(response), "200 OK\nThe list of records in the cards database:\n");
    } else {
        snprintf(response, sizeof(response), "200 OK\nYour cards:\n");
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        snprintf(response + strlen(response), MAX_LINE,
                 "ID: %d | %s | %s | %s | Count: %d | OwnerID: %d\n",
                 sqlite3_column_int(stmt, 0),
                 sqlite3_column_text(stmt, 1),
                 sqlite3_column_text(stmt, 2),
                 sqlite3_column_text(stmt, 3),
                 sqlite3_column_int(stmt, 4),
                 sqlite3_column_int(stmt, 5));
    }
    send(session->client_socket, response, strlen(response), 0);
    sqlite3_finalize(stmt);
}



// SHUTDOWN command: works only for root
void process_shutdown_command(sqlite3 *db, ClientSession *session) {
    if (session->user_id == -1) {
        send(session->client_socket, "401 Not logged in\n", 19, 0);
        return;
    }

    char sql[MAX_LINE];
    snprintf(sql, sizeof(sql), "SELECT is_root FROM Users WHERE ID = %d;", session->user_id);

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 1) {
        send(session->client_socket, "200 OK\nShutting down server\n", 28, 0);
        exit(0);  // Shut down the server
    } else {
        send(session->client_socket, "401 Non-root user attempt to shut down server\n", 50, 0);  // Non-root users cannot shut down
    }
    sqlite3_finalize(stmt);
}

// Create default user
int check_and_create_default_user(sqlite3 *db) {
    const char *check_user_sql = "SELECT 1 FROM Users LIMIT 1;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, check_user_sql, -1, &stmt, 0);
    int user_exists = 0;

    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            user_exists = 1; 
        }
    }

    sqlite3_finalize(stmt);

    // If no users exist, create a default user with $100 balance
    if (!user_exists) {
        const char *insert_user_sql = 
            "INSERT INTO Users (ID, first_name, last_name, user_name, password, usd_balance, is_root) "
            "VALUES (1, 'Default', 'User', 'default_user', 'password', 100.00, 1);";

        char *err_msg = 0;
        execute_sql(db, insert_user_sql);

        printf("Default user created with $100 balance.\n");
    }

    return user_exists;
}

// Function for BUY command
void process_buy_command(sqlite3 *db, char *command, ClientSession *session) {
    char pokemon_name[MAX_LINE], card_type[MAX_LINE], rarity[MAX_LINE];
    double price;
    int count;

    sscanf(command, "BUY %s %s %s %lf %d", pokemon_name, card_type, rarity, &price, &count);

    int owner_id = session->user_id;

    char sql[MAX_LINE];
    snprintf(sql, sizeof(sql), "SELECT usd_balance FROM Users WHERE ID = %d;", owner_id);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            double balance = sqlite3_column_double(stmt, 0);
            double total_price = price * count;

            if (balance >= total_price) {
                // Deduct balance and add the card to the user's inventory
                balance -= total_price;

                snprintf(sql, sizeof(sql), "UPDATE Users SET usd_balance = %f WHERE ID = %d;", balance, owner_id);
                execute_sql(db, sql);

                snprintf(sql, sizeof(sql),
                         "INSERT INTO Pokemon_cards (card_name, card_type, rarity, count, owner_id) "
                         "VALUES ('%s', '%s', '%s', %d, %d);",
                         pokemon_name, card_type, rarity, count, owner_id);
                execute_sql(db, sql);

                char response[MAX_LINE];
                // Successful purchase
                snprintf(response, sizeof(response), "200 OK\nBOUGHT: New balance: %d %s. User USD balance $%.2f\n", count, pokemon_name, balance);
                send(session->client_socket, response, strlen(response), 0);
            } else {
                send(session->client_socket, "403 Insufficient balance\n", 25, 0); // Insufficient balance
            }
        } else {
            send(session->client_socket, "403 User does not exist\n", 24, 0); // User does not exist
        }
    } else {
        send(session->client_socket, "500 Internal server error\n", 25, 0);
    }

    sqlite3_finalize(stmt);
}


// Function for SELL command
void process_sell_command(sqlite3 *db, char *command, ClientSession *session) {
    char card_name[MAX_LINE];
    int quantity;
    double price;

    sscanf(command, "SELL %s %d %lf", card_name, &quantity, &price);


    int owner_id = session->user_id;

    char sql[MAX_LINE];
    snprintf(sql, sizeof(sql), "SELECT usd_balance FROM Users WHERE ID = %d;", owner_id);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            double balance = sqlite3_column_double(stmt, 0);
            double total_income = price * quantity;

            // Update the user balance
            balance += total_income;
            snprintf(sql, sizeof(sql), "UPDATE Users SET usd_balance = %f WHERE ID = %d;", balance, owner_id);
            execute_sql(db, sql);

            snprintf(sql, sizeof(sql), "UPDATE Pokemon_cards SET count = count - %d WHERE card_name = '%s' AND owner_id = %d;", 
                     quantity, card_name, owner_id);
            execute_sql(db, sql);

            char response[MAX_LINE];
            snprintf(response, sizeof(response), "200 OK\nSOLD: New balance: %d %s. User USD balance $%.2f\n", quantity, card_name, balance);
            send(session->client_socket, response, strlen(response), 0);
        } else {
            send(session->client_socket, "403 User does not exist\n", 24, 0); // User does not exist
        }
    } 

    sqlite3_finalize(stmt);
}



// Function for BALANCE command
void process_balance_command(sqlite3 *db, char *command, ClientSession *session) {
    int owner_id = session->user_id;

    char sql[MAX_LINE];
    snprintf(sql, sizeof(sql), "SELECT usd_balance FROM Users WHERE ID = %d;", owner_id);

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);

    if (rc == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            double balance = sqlite3_column_double(stmt, 0);
            char response[MAX_LINE];
            snprintf(response, sizeof(response), "200 OK\nBalance for user %d: $%.2f\n", owner_id, balance); 
            send(session->client_socket, response, strlen(response), 0);
        } else {
            send(session->client_socket, "403 User does not exist\n", 24, 0); // User does not exist
        }
    } else {
        send(session->client_socket, "500 Internal server error\n", 25, 0); 
    }

    sqlite3_finalize(stmt);
}

void* handle_client(void* arg) {
    ClientSession* session = (ClientSession*)arg;
    int client_socket = session->client_socket;
    char buf[MAX_LINE];
    int buf_len;

    while (true) {
        buf_len = recv(client_socket, buf, sizeof(buf), 0);
        if (buf_len <= 0) {
            if (buf_len < 0 && errno == EAGAIN) {

                continue;
            } else {
                printf("Client disconnected on socket %d\n", client_socket);
                break;
            }
        }

        buf[buf_len] = '\0';
        printf("Received command: %s\n", buf);

        if (strcmp(buf, "BUY") == 0) {
            process_buy_command(session->db, buf, session);
        } else if (strcmp(buf, "SELL") == 0) {
            process_sell_command(session->db, buf, session);
        } else if (strcmp(buf, "BALANCE") == 0) {
            process_balance_command(session->db, buf, session);
        } else if (strcmp(buf, "LOGIN") == 0) {
            process_login_command(session->db, buf, session);
        } else if (strcmp(buf, "LOGOUT") == 0) {
            process_logout_command(session);
        } else if (strcmp(buf, "DEPOSIT") == 0) {
            process_deposit_command(session->db, buf, session);
        } else if (strcmp(buf, "WHO") == 0) {
            process_who_command(session->db, session);
        } else if (strcmp(buf, "LOOKUP") == 0) {
            process_lookup_command(session->db, buf, session);
        } else if (strcmp(buf, "LIST") == 0) {
            process_list_command(session->db, session);
        } else if (strcmp(buf, "SHUTDOWN") == 0) {
            process_shutdown_command(session->db, session);
        } else if (strcmp(buf, "QUIT") == 0) {
            send(session->client_socket, "200 OK\n", 7, 0);
            break;
        } else {
            send(session->client_socket, "200 OK\n", 7, 0);
        }
    }

    close(client_socket);
    delete session;  
    pthread_exit(NULL);
}

int main() {
    struct sockaddr_in sin;
    int listen_sock, client_sock;
    fd_set master_set, working_set;
    int max_fd;
    unordered_map<int, pthread_t> client_threads;
    
    sqlite3* db;
    int rc = sqlite3_open("pokemon_store.db", &db);
    if (rc != SQLITE_OK) {
        printf("Cannot open database: %s\n", sqlite3_errmsg(db));
        exit(1);
    }

    const char* sql = "CREATE TABLE IF NOT EXISTS Users (ID INTEGER PRIMARY KEY, first_name TEXT, last_name TEXT, user_name TEXT NOT NULL, password TEXT, usd_balance DOUBLE NOT NULL, is_root INTEGER NOT NULL DEFAULT 0);"
                      "CREATE TABLE IF NOT EXISTS Pokemon_cards (ID INTEGER PRIMARY KEY, card_name TEXT NOT NULL, card_type TEXT NOT NULL, rarity TEXT NOT NULL, count INTEGER, owner_id INTEGER, FOREIGN KEY (owner_id) REFERENCES Users(ID));";
    execute_sql(db, sql);
    check_and_create_default_user(db);
    
    // Sample data 
    const char *delete_users_sql = "DELETE FROM Users;";
    execute_sql(db, delete_users_sql);
    const char *sample_users_sql = 
        "INSERT INTO Users (ID, first_name, last_name, user_name, password, usd_balance, is_root) "
        "VALUES (1, 'root', 'user', 'root', 'root01', 100000, 1), "
        "(2, 'mary', 'smith', 'mary', 'mary01', 0, 0), "
        "(3, 'john', 'brown', 'john', 'john01', 0, 0), "
        "(4, 'moe', 'van', 'moe', 'moe01', 0, 0) ";
    rc = sqlite3_exec(db, sample_users_sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        printf("Failed to insert sample users: %s\n", sqlite3_errmsg(db));
    } else {
        printf("Sample data inserted successfully.\n");
    }
    bzero((char*)&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(SERVER_PORT);

    if ((listen_sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("server: socket");
        exit(1);
    }

    if (bind(listen_sock, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
        perror("server: bind");
        exit(1);
    }

    if (listen(listen_sock, MAX_PENDING) < 0) {
        perror("server: listen");
        exit(1);
    }

    FD_ZERO(&master_set);
    FD_SET(listen_sock, &master_set);
    max_fd = listen_sock;

    printf("Server started. Waiting for connections...\n");

    while (true) {
        working_set = master_set;
        if (select(max_fd + 1, &working_set, NULL, NULL, NULL) < 0) {
            perror("select");
            exit(1);
        }

        if (FD_ISSET(listen_sock, &working_set)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);

            if (client_sock < 0) {
                perror("accept");
                continue;
            }

            printf("New connection on socket %d\n", client_sock);

            int flags = fcntl(client_sock, F_GETFL, 0);
            fcntl(client_sock, F_SETFL, flags | O_NONBLOCK);

            ClientSession* session = new ClientSession(client_sock, -1, db);

            pthread_t thread_id;
            if (pthread_create(&thread_id, nullptr, handle_client, (void*)session) != 0) {
                perror("pthread_create");
                close(client_sock);
                delete session;
                continue;
            }

            pthread_detach(thread_id);
            client_threads[client_sock] = thread_id;
        }
    }

    close(listen_sock);
    sqlite3_close(db);
    return 0;
}

