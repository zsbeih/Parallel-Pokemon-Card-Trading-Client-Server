# Parallel Pokemon Card Trading Client & Server

## Description
This project is a multithreaded client-server application for online Pokémon card trading. The server manages multiple client connections simultaneously using threading and select(). Clients can execute commands to manage Pokémon cards, user accounts, and perform trading operations. The system ensures efficient and secure communication between clients and the server while supporting robust error handling and user authentication.

## Key Features
Multithreaded Server: Handles multiple clients using threads and a thread pool.
Client Commands: Includes commands like LOGIN, LOGOUT, BUY, SELL, DEPOSIT, LIST, BALANCE, WHO, LOOKUP, and SHUTDOWN.
User Authentication: Supports predefined users with unique IDs and passwords.
Database Integration: Centralized storage of user and card data via SQLite.
Error Handling: Prevents server crashes from invalid input or malicious actions.

## Technology
Language: C++ 
Networking: TCP sockets and select() API
Threading: Pthreads
Database: SQLite

## Running Instructions
Using a terminal instance:
  For the server.cpp execute this first: <gcc -c sqlite3.c -o sqlite3.o>, and then : <g++ server.cpp sqlite3.o -o server -lpthread -ldl>
  For the client: <g++ -o client client.cpp>

## DEMO/Showcase
https://www.youtube.com/watch?v=2TOD-SeL8pA
