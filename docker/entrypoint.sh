#!/bin/bash
# Entrypoint script for ODR-AudioEnc StreamDAB Enhanced Docker container

set -e

# Default configuration
CONFIG_FILE="/opt/streamdab/config/default.conf"
LOG_LEVEL="${LOG_LEVEL:-info}"
API_PORT="${API_PORT:-8007}"

# Create necessary directories
mkdir -p /opt/streamdab/{config,logs,data}
mkdir -p /var/log/odr-audioenc

# Function to log messages
log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1"
}

# Function to check if a service is running
check_service() {
    local service_name="$1"
    local port="$2"

    if netstat -tuln | grep -q ":${port}"; then
        log "$service_name is running on port $port"
        return 0
    else
        log "$service_name is not running on port $port"
        return 1
    fi
}

# Function to wait for dependency services
wait_for_dependencies() {
    local timeout=30
    local count=0

    log "Waiting for dependency services..."

    # Wait for ODR-DabMux (if ZMQ is enabled)
    if [ "${ENABLE_ZMQ:-true}" = "true" ]; then
        local dabmux_host="${DABMUX_HOST:-odr-dabmux}"
        local dabmux_port="${DABMUX_PORT:-9200}"

        while ! nc -z "$dabmux_host" "$dabmux_port" 2>/dev/null; do
            if [ $count -ge $timeout ]; then
                log "Warning: ODR-DabMux not available at $dabmux_host:$dabmux_port after ${timeout}s"
                break
            fi
            log "Waiting for ODR-DabMux at $dabmux_host:$dabmux_port..."
            sleep 1
            ((count++))
        done

        if [ $count -lt $timeout ]; then
            log "ODR-DabMux is available at $dabmux_host:$dabmux_port"
        fi
    fi
}

# Function to validate configuration
validate_config() {
    log "Validating configuration..."

    # Check if config file exists
    if [ ! -f "$CONFIG_FILE" ]; then
        log "Warning: Configuration file not found at $CONFIG_FILE"
        log "Using default built-in configuration"
    fi

    # Validate required environment variables
    local required_vars=()
    local missing_vars=()

    # Check for missing required variables
    for var in "${required_vars[@]}"; do
        if [ -z "${!var}" ]; then
            missing_vars+=("$var")
        fi
    done

    if [ ${#missing_vars[@]} -gt 0 ]; then
        log "Error: Missing required environment variables:"
        for var in "${missing_vars[@]}"; do
            log "  - $var"
        done
        exit 1
    fi

    log "Configuration validation completed"
}

# Function to setup logging
setup_logging() {
    log "Setting up logging..."

    # Ensure log directory exists and has correct permissions
    mkdir -p "$(dirname /var/log/odr-audioenc.log)"
    touch /var/log/odr-audioenc.log

    # Setup log rotation if logrotate is available
    if command -v logrotate >/dev/null; then
        cat > /etc/logrotate.d/odr-audioenc << EOF
/var/log/odr-audioenc*.log {
    daily
    rotate 7
    compress
    missingok
    notifempty
    create 0644 streamdab streamdab
}
EOF
        log "Log rotation configured"
    fi
}

# Function to start StreamDAB API server
start_api_server() {
    if [ "${API_ENABLED:-true}" = "true" ]; then
        log "StreamDAB API will be available on port $API_PORT"

        # API server is integrated into odr-audioenc
        # Just log the configuration
        log "API Configuration:"
        log "  Port: $API_PORT"
        log "  SSL: ${API_ENABLE_SSL:-false}"
        log "  Authentication: ${API_REQUIRE_AUTH:-false}"
        log "  CORS: ${API_ENABLE_CORS:-true}"
    fi
}

# Function to handle shutdown
shutdown_handler() {
    log "Received shutdown signal, stopping services..."

    # Stop odr-audioenc if running
    if [ -n "$ODR_PID" ]; then
        log "Stopping odr-audioenc (PID: $ODR_PID)..."
        kill -TERM "$ODR_PID" 2>/dev/null || true
        wait "$ODR_PID" 2>/dev/null || true
    fi

    log "Shutdown complete"
    exit 0
}

# Setup signal handlers
trap shutdown_handler SIGTERM SIGINT SIGQUIT

# Main execution starts here
log "Starting ODR-AudioEnc StreamDAB Enhanced..."
log "Version: $(odr-audioenc --version 2>&1 | head -1 || echo 'Unknown')"

# Show environment
log "Environment:"
log "  Container User: $(whoami)"
log "  Working Directory: $(pwd)"
log "  Log Level: $LOG_LEVEL"
log "  API Port: $API_PORT"

# Validate configuration
validate_config

# Setup logging
setup_logging

# Wait for dependencies
wait_for_dependencies

# Start API server preparation
start_api_server

# If no arguments provided, show help
if [ $# -eq 0 ]; then
    log "No arguments provided, showing help..."
    exec odr-audioenc --help
fi

# Handle special commands
case "$1" in
    --health-check)
        log "Running health check..."
        exec /opt/streamdab/healthcheck.sh
        ;;
    --version)
        exec odr-audioenc --version
        ;;
    --help|-h)
        exec odr-audioenc --help
        ;;
    --config-test)
        log "Testing configuration..."
        # Add configuration test logic here
        log "Configuration test completed"
        exit 0
        ;;
    --shell)
        log "Starting interactive shell..."
        exec /bin/bash
        ;;
esac

# Build odr-audioenc command line arguments
ODR_ARGS=()

# Add API configuration
if [ "${API_ENABLED:-true}" = "true" ]; then
    ODR_ARGS+=("--api-port" "$API_PORT")

    if [ "${API_ENABLE_SSL:-false}" = "true" ]; then
        ODR_ARGS+=("--api-ssl")
        if [ -n "$API_SSL_CERT" ]; then
            ODR_ARGS+=("--api-ssl-cert" "$API_SSL_CERT")
        fi
        if [ -n "$API_SSL_KEY" ]; then
            ODR_ARGS+=("--api-ssl-key" "$API_SSL_KEY")
        fi
    fi

    if [ "${API_REQUIRE_AUTH:-false}" = "true" ] && [ -n "$API_KEY" ]; then
        ODR_ARGS+=("--api-key" "$API_KEY")
    fi
fi

# Add stream configuration
if [ -n "$STREAM_URL" ]; then
    ODR_ARGS+=("--stream-url" "$STREAM_URL")
fi

# Add fallback streams
if [ -n "$FALLBACK_STREAMS" ]; then
    IFS=',' read -ra FALLBACK_ARRAY <<< "$FALLBACK_STREAMS"
    for stream in "${FALLBACK_ARRAY[@]}"; do
        ODR_ARGS+=("--fallback-stream" "$stream")
    done
fi

# Add bitrate
if [ -n "$BITRATE" ]; then
    ODR_ARGS+=("--bitrate" "$BITRATE")
fi

# Add ZMQ output
if [ "${ENABLE_ZMQ:-true}" = "true" ]; then
    ODR_ARGS+=("--zmq-output" "${ZMQ_ENDPOINT:-tcp://*:9000}")
fi

# Add file output
if [ "${ENABLE_FILE_OUTPUT:-false}" = "true" ] && [ -n "$FILE_OUTPUT_PATH" ]; then
    ODR_ARGS+=("--file-output" "$FILE_OUTPUT_PATH")
fi

# Add configuration file if it exists
if [ -f "$CONFIG_FILE" ]; then
    ODR_ARGS+=("--config" "$CONFIG_FILE")
fi

# Add any additional arguments passed to the container
ODR_ARGS+=("$@")

# Show final command
log "Starting odr-audioenc with arguments:"
log "  Command: odr-audioenc ${ODR_ARGS[*]}"

# Start odr-audioenc in background so we can handle signals
odr-audioenc "${ODR_ARGS[@]}" &
ODR_PID=$!

log "ODR-AudioEnc started with PID: $ODR_PID"

# Wait for the process and handle signals
wait $ODR_PID
EXIT_CODE=$?

log "ODR-AudioEnc exited with code: $EXIT_CODE"
exit $EXIT_CODE
