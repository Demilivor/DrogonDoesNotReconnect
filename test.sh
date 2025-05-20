#!/bin/bash

# sudo apt-get install postgresql-server-dev-all
docker compose -f postgres-compose.yaml down -v
docker compose -f postgres-compose.yaml up -d

# Wait for PostgreSQL to be ready
echo "Waiting for PostgreSQL to start..."
sleep 5

# Create build directory and build the C++ program
mkdir -p build
cd build
CC=clang CXX=clang++ cmake ..
make clean
make -j 32

# Run the C++ program in the background
echo "Starting drogon_postgres_example in the background..."
TSAN_OPTIONS="history_size=7 verbosity=1" ./drogon_postgres_example &
APP_PID=$!

# Store the current directory
CURRENT_DIR=$(pwd)

# Go back to the parent directory for docker commands
cd ..

# Loop to restart PostgreSQL multiple times
echo "Starting PostgreSQL restart test..."
for i in {1..50}; do
    echo "Iteration $i: Stopping PostgreSQL..."
    docker compose -f postgres-compose.yaml stop postgres
    sleep 3
    echo "Iteration $i: Starting PostgreSQL..."
    docker compose -f postgres-compose.yaml start postgres
    sleep 3
done

# Sleep additional time to give drogon some time to reestablish connection
sleep 30

# Send SIGTERM to the application
echo "Sending SIGTERM to the application (PID: $APP_PID)..."
kill -TERM $APP_PID

# Wait for the application to terminate
wait $APP_PID
exit_code=$?

# Stop and remove Docker Compose containers
echo "Stopping Docker containers..."
docker compose -f postgres-compose.yaml down -v

if [ $exit_code -eq 0 ]; then
    echo "TEST_PASSED"
else
    echo "TEST_FAILED"
fi

exit $exit_code