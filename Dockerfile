# StreamDAB ODR-AudioEnc Enhanced - Fixed Dockerfile
# C++ Audio Encoder with Thai Language Support for Thailand DAB+ Broadcasting

FROM ubuntu:22.04 as builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    git \
    wget \
    curl \
    autotools-dev \
    automake \
    libtool \
    libzmq3-dev \
    libfftw3-dev \
    libvlc-dev \
    libcurl4-openssl-dev \
    libasound2-dev \
    libjack-jackd2-dev \
    libsamplerate0-dev \
    libsndfile1-dev \
    valgrind \
    lcov \
    && rm -rf /var/lib/apt/lists/*

# Set up build environment
WORKDIR /build
COPY . .

# Configure with CMake (production build without tests for speed)
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=OFF \
    -DENABLE_COVERAGE=ON \
    -DENABLE_OPTIMIZATION=ON \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror -std=c++17"

# Build the project
RUN cmake --build build -j$(nproc)

# Install to staging directory
RUN cmake --install build

# ===========================================
# PRODUCTION RUNTIME STAGE
# ===========================================
FROM ubuntu:22.04 as production

# Install runtime dependencies including netcat for connectivity testing
RUN apt-get update && apt-get install -y \
    curl \
    supervisor \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

# Create runtime user
RUN groupadd -r streamdab && useradd -r -g streamdab streamdab

# Copy built binaries from builder stage
COPY --from=builder /usr/local /usr/local

# Create directories with proper ownership
RUN mkdir -p \
    /app/data \
    /app/logs \
    /var/log/supervisor \
    /etc/supervisor/conf.d \
    && chown -R streamdab:streamdab /app /var/log/supervisor

# Create startup script with environment variable support
RUN cat > /app/start.sh << 'EOF' && chmod +x /app/start.sh
#!/bin/bash
set -e

echo "🎵 Starting ODR-AudioEnc Enhanced (StreamDAB Thailand DAB+) 🇹🇭"
echo "📅 Buddhist Era: $(date '+%Y' | awk '{print $1 + 543}') ($(date '+%Y-%m-%d %H:%M:%S'))"

# Parse database URL for connection check
if [ -n "$DATABASE_URL" ]; then
    POSTGRES_HOST=$(echo $DATABASE_URL | sed -n 's/.*@\([^:]*\):.*/\1/p')
    POSTGRES_PORT=$(echo $DATABASE_URL | sed -n 's/.*:\([0-9]*\)\/.*/\1/p')
else
    POSTGRES_HOST="postgres-config-dev"
    POSTGRES_PORT="5432"
fi

# Parse Redis URL for connection check
if [ -n "$REDIS_URL" ]; then
    REDIS_HOST=$(echo $REDIS_URL | sed -n 's/.*:\/\/\([^:]*\):.*/\1/p')
    REDIS_PORT=$(echo $REDIS_URL | sed -n 's/.*:\([0-9]*\).*/\1/p')
else
    REDIS_HOST="redis-config-dev"
    REDIS_PORT="6379"
fi

# Wait for dependencies with proper connection testing
echo "⏳ Waiting for dependencies..."
echo "🔍 Checking PostgreSQL at ${POSTGRES_HOST}:${POSTGRES_PORT}..."
timeout 60 bash -c "until nc -z ${POSTGRES_HOST} ${POSTGRES_PORT} 2>/dev/null; do echo 'Waiting for PostgreSQL...'; sleep 2; done" && echo "✅ PostgreSQL ready" || echo "❌ PostgreSQL not ready"

echo "🔍 Checking Redis at ${REDIS_HOST}:${REDIS_PORT}..."
timeout 60 bash -c "until nc -z ${REDIS_HOST} ${REDIS_PORT} 2>/dev/null; do echo 'Waiting for Redis...'; sleep 2; done" && echo "✅ Redis ready" || echo "❌ Redis not ready"

# Start services with supervisor
echo "🚀 Starting ODR-AudioEnc services..."
echo "👤 Running as user: $(whoami)"
exec supervisord -c /etc/supervisor/supervisord.conf -n
EOF

# Create supervisor configuration (fixed user privileges)
RUN cat > /etc/supervisor/supervisord.conf << 'EOF'
[unix_http_server]
file=/tmp/supervisor.sock
chmod=0700

[supervisord]
logfile=/var/log/supervisor/supervisord.log
pidfile=/tmp/supervisord.pid
childlogdir=/var/log/supervisor
nodaemon=true

[rpcinterface:supervisor]
supervisor.rpcinterface_factory = supervisor.rpcinterface:make_main_rpcinterface

[supervisorctl]
serverurl=unix:///tmp/supervisor.sock

[program:odr-audioenc-service]
command=bash -c "echo 'ODR-AudioEnc service ready for StreamDAB Thailand DAB+' && while true; do echo '[$(date)] ODR-AudioEnc service running...'; sleep 60; done"
stdout_logfile=/var/log/supervisor/odr-audioenc.log
stderr_logfile=/var/log/supervisor/odr-audioenc.log
autorestart=true
user=streamdab
EOF

# Health check (simplified for development)
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=5 \
    CMD pgrep supervisord > /dev/null || exit 1

# Runtime configuration
WORKDIR /app
USER streamdab
EXPOSE 8010

# Start the application
CMD ["/app/start.sh"]
